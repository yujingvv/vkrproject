/* ============================================================
 * fft_hwa.c  --  TMS320C5515 HWA FFT stub (radix-2 DIT)
 *
 * Design notes
 * ------------
 * The on-chip C5515 FFT HWA performs a block-floating-point
 * radix-2 transform that effectively divides the forward result
 * by N (one >>1 per stage * 10 stages == /1024 for N=1024).
 * We mimic that behaviour exactly here so downstream Q15 modules
 * see the same scaling on PC and on chip.
 *
 *   Forward (per stage):
 *             W    = (twiddle_re[k], twiddle_im[k])
 *             temp = W * b              (rounded Q15 multiply)
 *             a'   = (a + temp) >> 1
 *             b'   = (a - temp) >> 1
 *
 *   Inverse: implemented as
 *             y = conj( UnscaledForwardFFT( conj(X) ) )
 *     i.e. we conjugate the input, run the forward butterflies
 *     WITHOUT the per-stage >>1, then conjugate the output.  The
 *     un-scaled forward of conj(X) is conj(N * IFFT(X)), so the
 *     final conj gives N * IFFT(X).  Since the spectrum X we get
 *     from fft_hwa_forward is already pre-divided by N, the net
 *     end-to-end gain is 1 and ifft(fft(x)) approximately equals x.
 *
 *     Why "conj-FFT-conj" instead of the textbook "DIT with
 *     conjugated twiddles"?  Because at non-power-of-two bin
 *     indices the naive conjugated-twiddle DIT introduces
 *     systematic sign errors on certain output samples (a
 *     well-known pitfall).  Reusing the forward kernel sidesteps
 *     the issue entirely and matches what real DSPLIB drivers do.
 *
 * Scaling choice (option (a) in the spec)
 * ---------------------------------------
 * No per-stage shift is applied on the inverse path, so the
 * inverse output is essentially un-scaled.  Combined with the
 * forward's 1/N pre-scaling, the round-trip gain is unity.
 * Intermediate butterfly values on the inverse can exceed the
 * Q15 range, so we accumulate in Q31 and saturate only on the
 * final writeback.
 * ============================================================ */

#include "fft_hwa.h"
#include "cycle_counter.h"
#include "memmap.h"
#include "../util/twiddle_table.h"

#include <string.h>

static uint32_t g_last_cycles = 0;

/* Bit-reverse permutation table.  Built once at init.  On chip the
 * HWA computes bit-reversal in hardware so this becomes unused when
 * fft_hwa_forward/inverse are replaced with CSL FFT_run() calls --
 * the table is dead code in that build. */
static uint16_t g_brev[N_FFT] MEM_SARAM;
static int      g_brev_initialised = 0;

static void brev_init(void) {
    int i;
    for (i = 0; i < N_FFT; ++i) {
        uint32_t x = (uint32_t)i;
        uint32_t r = 0;
        int b;
        for (b = 0; b < LOG2_N_FFT; ++b) {
            r = (r << 1) | (x & 1u);
            x >>= 1;
        }
        g_brev[i] = (uint16_t)r;
    }
    g_brev_initialised = 1;
}

void fft_hwa_init(void) {
    if (!g_brev_initialised) {
        brev_init();
    }
    g_last_cycles = 0;
}

/* ---- Internal forward radix-2 DIT, in-place over q15_cplx_t ----
 * Each stage applies a per-stage >>1 to prevent overflow, matching
 * the C5515 HWA block-floating-point scaling. */
static void fft_forward_inplace(q15_cplx_t buf[N_FFT]) {
    int s;
    for (s = 1; s <= LOG2_N_FFT; ++s) {
        const int m    = 1 << s;
        const int half = m >> 1;
        const int wstride = N_FFT / m;
        int k;
        for (k = 0; k < N_FFT; k += m) {
            int j;
            for (j = 0; j < half; ++j) {
                const int widx = j * wstride;
                const q15_t w_re = twiddle_re_q15[widx];
                const q15_t w_im = twiddle_im_q15[widx];

                const q15_cplx_t a = buf[k + j];
                const q15_cplx_t b = buf[k + j + half];

                /* Rounded twiddle multiply (add 1<<14 before >>15)
                 * to avoid systematic truncation that biases small
                 * values toward zero. */
                const q31_t pre = (q31_t)w_re * b.re - (q31_t)w_im * b.im;
                const q31_t pim = (q31_t)w_re * b.im + (q31_t)w_im * b.re;
                const q31_t tre = (pre + (1 << 14)) >> 15;
                const q31_t tim = (pim + (1 << 14)) >> 15;

                /* (a +/- temp) >> 1  -- per-stage scale by 2. */
                buf[k + j       ].re = sat_q15(((q31_t)a.re + tre) >> 1);
                buf[k + j       ].im = sat_q15(((q31_t)a.im + tim) >> 1);
                buf[k + j + half].re = sat_q15(((q31_t)a.re - tre) >> 1);
                buf[k + j + half].im = sat_q15(((q31_t)a.im - tim) >> 1);
            }
        }
    }
}

/* ---- Internal inverse radix-2 ----
 *
 * Implementation note: we use the identity
 *   IFFT(X)[n] = (1/N) * conj( FFT( conj(X) ) [n] )
 *
 * This lets us reuse the SAME forward-FFT machinery (and the same
 * twiddle table indexing) and avoid the well-known sign-error
 * trap that hits naive "DIT-with-conjugated-twiddles" inverses
 * at non-power-of-2 bin indices.
 *
 * We accumulate in Q31 (no per-stage shift) so that the only
 * scaling applied is the explicit final >>0 here -- when the
 * spectrum being inverted was produced by our forward FFT
 * (which has already pre-divided by N), the net round trip is
 * a perfect unity gain (modulo Q15 rounding).
 *
 * The forward butterfly running on conj(X) computes (unscaled):
 *     Y_unscaled[n] = sum_k conj(X)[k] * exp(-j 2π k n / N)
 *                   = conj( sum_k X[k] * exp(+j 2π k n / N) )
 *                   = conj( N * x_true[n] )
 * So conj(Y_unscaled) = N * x_true[n], which is exactly what we
 * want when X was X_true/N from the forward path. */
static void fft_inverse_inplace(q15_cplx_t buf[N_FFT]) {
    /* Conjugate the input. */
    int i;
    for (i = 0; i < N_FFT; ++i) {
        if (buf[i].im == (q15_t)-32768) buf[i].im = 32767;  /* sat */
        else                            buf[i].im = (q15_t)(-buf[i].im);
    }

    /* Promote to Q31 and run the forward DIT butterflies, but
     * WITHOUT the per-stage >>1 (so the inverse gets a x N gain
     * relative to a properly-normalised IFFT, which compensates
     * the 1/N our forward path applied).  These scratch arrays are
     * 4 KB each; placed in DARAM on chip for fastest access. */
    static q31_t re[N_FFT] MEM_DARAM;
    static q31_t im[N_FFT] MEM_DARAM;
    for (i = 0; i < N_FFT; ++i) {
        re[i] = (q31_t)buf[i].re;
        im[i] = (q31_t)buf[i].im;
    }

    int s;
    for (s = 1; s <= LOG2_N_FFT; ++s) {
        const int m    = 1 << s;
        const int half = m >> 1;
        const int wstride = N_FFT / m;
        int k;
        for (k = 0; k < N_FFT; k += m) {
            int j;
            for (j = 0; j < half; ++j) {
                const int widx = j * wstride;
                const q15_t w_re = twiddle_re_q15[widx];
                const q15_t w_im = twiddle_im_q15[widx];  /* forward twiddle */

                const q31_t a_re = re[k + j];
                const q31_t a_im = im[k + j];
                const q31_t b_re = re[k + j + half];
                const q31_t b_im = im[k + j + half];

                /* Rounded Q15 twiddle multiply over 64-bit intermediates. */
                const q63_t pre = (q63_t)w_re * b_re - (q63_t)w_im * b_im;
                const q63_t pim = (q63_t)w_re * b_im + (q63_t)w_im * b_re;
                const q31_t tre = (q31_t)((pre + (1LL << 14)) >> 15);
                const q31_t tim = (q31_t)((pim + (1LL << 14)) >> 15);

                re[k + j       ] = a_re + tre;
                im[k + j       ] = a_im + tim;
                re[k + j + half] = a_re - tre;
                im[k + j + half] = a_im - tim;
            }
        }
    }

    /* Conjugate output (= take real part as-is, negate imag) and
     * saturate back to Q15.  No final shift: see header comment. */
    for (i = 0; i < N_FFT; ++i) {
        buf[i].re = sat_q15( re[i]);
        buf[i].im = sat_q15(-im[i]);
    }
}

void fft_hwa_forward(const q15_t time_in[N_FFT], q15_cplx_t freq_out[N_FFT]) {
    /* 4 KB DARAM scratch -- HWA uses ping-pong DMA in/out of DARAM. */
    static q15_cplx_t work[N_FFT] MEM_DARAM;
    int i;

    if (!g_brev_initialised) brev_init();

    /* Bit-reverse permutation of real input (im = 0). */
    for (i = 0; i < N_FFT; ++i) {
        work[g_brev[i]].re = time_in[i];
        work[g_brev[i]].im = 0;
    }

    fft_forward_inplace(work);

    memcpy(freq_out, work, sizeof(work));

    g_last_cycles = CYC_FFT_1024;
    cycles_add(CYC_FFT_1024);
}

void fft_hwa_inverse(const q15_cplx_t freq_in[N_FFT], q15_t time_out[N_FFT]) {
    static q15_cplx_t work[N_FFT] MEM_DARAM;
    int i;

    if (!g_brev_initialised) brev_init();

    /* Bit-reverse permutation of complex input. */
    for (i = 0; i < N_FFT; ++i) {
        work[g_brev[i]] = freq_in[i];
    }

    fft_inverse_inplace(work);

    /* Output real part; tiny imaginary residue is discarded. */
    for (i = 0; i < N_FFT; ++i) {
        time_out[i] = work[i].re;
    }

    g_last_cycles = CYC_FFT_1024;
    cycles_add(CYC_FFT_1024);
}

uint32_t fft_hwa_last_cycles(void) {
    return g_last_cycles;
}
