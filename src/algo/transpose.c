/* ============================================================
 * transpose.c  --  Frequency transposition + mix.
 *
 * Reference: matlab/compress.m lines 89-107.
 *
 * For every target-band bin k_t in [TARGET_LO_BIN, TARGET_HI_BIN),
 * we compute a fractional source-bin index
 *     k_s = SOURCE_LO_BIN + (k_t - TARGET_LO_BIN) * (GAMMA_DEN/GAMMA_NUM)
 * and linearly interpolate the clean spectrum between
 * clean[floor(k_s)] and clean[floor(k_s)+1].
 *
 * Source bins are zeroed in the transposed spectrum. The final
 * output is a per-bin Q15 crossfade between the clean and the
 * transposed spectra, weighted by mix_ratio.
 * ============================================================ */

#include "transpose.h"
#include "../hal/memmap.h"

/* The transposed working buffer is ~2 KB; promoting to file-scope
 * static avoids a stack allocation per call and lets the linker place
 * it in a named region.  transpose_apply is not re-entrant; this is
 * intentional (the whole pipeline is single-threaded). */
static q15_cplx_t g_transposed[N_BINS] MEM_SARAM;

void transpose_apply(const q15_cplx_t clean[N_BINS],
                     q15_cplx_t       output[N_BINS],
                     q15_t            mix_ratio)
{
    q15_cplx_t *transposed = g_transposed;
    int k;

    /* ---- 1. Build the transposed spectrum ---- */

    /* Bins below the target band: copy through. */
    for (k = 0; k < TARGET_LO_BIN; ++k) {
        transposed[k] = clean[k];
    }

    /* Bins inside the target band: linear interpolation of the
     * source-band content. */
    for (k = TARGET_LO_BIN; k < TARGET_HI_BIN; ++k) {
        int dk      = k - TARGET_LO_BIN;
        int num     = dk * GAMMA_DEN;            /* numerator of k_s offset  */
        int k_s_int = SOURCE_LO_BIN + num / GAMMA_NUM;
        int k_s_rem = num % GAMMA_NUM;           /* fractional numerator     */

        q15_t re, im;
        if (k_s_rem == 0) {
            /* Exact integer hit -- no interpolation needed. */
            if (k_s_int >= N_BINS) {
                /* Extrapolation guard: clamp to last available bin. */
                re = clean[N_BINS - 1].re;
                im = clean[N_BINS - 1].im;
            } else {
                re = clean[k_s_int].re;
                im = clean[k_s_int].im;
            }
        } else if (k_s_int + 1 >= N_BINS) {
            /* Would need a sample past the spectrum end; clamp. */
            re = clean[N_BINS - 1].re;
            im = clean[N_BINS - 1].im;
        } else {
            q15_t frac_q15 =
                (q15_t)(((q31_t)k_s_rem * Q15_ONE) / GAMMA_NUM);
            q15_t one_minus_frac_q15 = Q15_ONE - frac_q15;

            q31_t acc_re =
                (q31_t)mul_q15(one_minus_frac_q15, clean[k_s_int].re)
              + (q31_t)mul_q15(frac_q15,           clean[k_s_int + 1].re);
            q31_t acc_im =
                (q31_t)mul_q15(one_minus_frac_q15, clean[k_s_int].im)
              + (q31_t)mul_q15(frac_q15,           clean[k_s_int + 1].im);
            re = sat_q15(acc_re);
            im = sat_q15(acc_im);
        }
        transposed[k].re = re;
        transposed[k].im = im;
    }

    /* Bins between the end of the target band and the start of the
     * source band (here this region is empty when TARGET_HI_BIN ==
     * SOURCE_LO_BIN, but we still write the loop for generality). */
    for (k = TARGET_HI_BIN; k < SOURCE_LO_BIN; ++k) {
        transposed[k] = clean[k];
    }

    /* Source band: cleared in the transposed spectrum. */
    for (k = SOURCE_LO_BIN; k < SOURCE_HI_BIN; ++k) {
        transposed[k].re = 0;
        transposed[k].im = 0;
    }

    /* Bins above the source band: copy through. */
    for (k = SOURCE_HI_BIN; k < N_BINS; ++k) {
        transposed[k] = clean[k];
    }

    /* ---- 2. Per-bin crossfade between clean and transposed. ---- */
    {
        q15_t one_minus_mix = (q15_t)(Q15_ONE - mix_ratio);
        for (k = 0; k < N_BINS; ++k) {
            q31_t re = (q31_t)mul_q15(one_minus_mix, clean[k].re)
                     + (q31_t)mul_q15(mix_ratio,    transposed[k].re);
            q31_t im = (q31_t)mul_q15(one_minus_mix, clean[k].im)
                     + (q31_t)mul_q15(mix_ratio,    transposed[k].im);
            output[k].re = sat_q15(re);
            output[k].im = sat_q15(im);
        }
    }
}
