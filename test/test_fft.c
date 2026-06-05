/* ============================================================
 * test_fft.c  --  Standalone tests for fft_hwa stub
 *
 * Build:
 *   gcc -std=c99 -O2 -Isrc test/test_fft.c src/hal/fft_hwa.c \
 *       src/hal/cycle_counter.c -o /tmp/test_fft -lm
 * Run:
 *   /tmp/test_fft
 * ============================================================ */

#include "../src/hal/fft_hwa.h"
#include "../src/hal/cycle_counter.h"
#include "../src/config.h"
#include "../src/util/fixed_point.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_pass_count = 0;
static int g_fail_count = 0;

static void test_pass(const char *what) {
    printf("[fft] PASS: %s\n", what);
    g_pass_count++;
}

static void test_fail(const char *what, const char *reason) {
    printf("[fft] FAIL: %s (%s)\n", what, reason);
    g_fail_count++;
}

/* ---------- Test 1: peak detection ---------- */
static void test_peak_detection(void) {
    static q15_t x[N_FFT];
    static q15_cplx_t X[N_FFT];
    int n;
    const int peak_bin_expected = 37;

    /* x[n] = round(0.5 * 32768 * cos(2π * 37 * n / 1024)) */
    for (n = 0; n < N_FFT; ++n) {
        double v = 0.5 * 32768.0 * cos(2.0 * M_PI * peak_bin_expected * n / (double)N_FFT);
        if (v >  32767.0) v =  32767.0;
        if (v < -32768.0) v = -32768.0;
        x[n] = (q15_t)(v >= 0 ? floor(v + 0.5) : -floor(-v + 0.5));
    }

    fft_hwa_forward(x, X);

    /* Find max-magnitude bin over the lower half (k=0..N/2). */
    double peak_mag = -1.0;
    int    peak_k   = -1;
    double sum_other_sq = 0.0;
    int    other_cnt = 0;
    int k;
    for (k = 0; k <= N_FFT / 2; ++k) {
        double re = (double)X[k].re;
        double im = (double)X[k].im;
        double m  = sqrt(re*re + im*im);
        if (m > peak_mag) { peak_mag = m; peak_k = k; }
    }
    for (k = 0; k <= N_FFT / 2; ++k) {
        if (abs(k - peak_k) <= 2) continue;
        double re = (double)X[k].re;
        double im = (double)X[k].im;
        sum_other_sq += re*re + im*im;
        other_cnt++;
    }
    double noise_rms = sqrt(sum_other_sq / (other_cnt > 0 ? other_cnt : 1));

    printf("[fft] peak detection: peak_bin=%d expected=%d peak_mag=%.2f noise_rms=%.4f\n",
           peak_k, peak_bin_expected, peak_mag, noise_rms);

    if (peak_k == peak_bin_expected) {
        test_pass("peak detection at bin 37");
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "peak at bin %d, expected %d", peak_k, peak_bin_expected);
        test_fail("peak detection at bin 37", buf);
    }
}

/* ---------- Test 2: conjugate symmetry ---------- */
static void test_conjugate_symmetry(void) {
    static q15_t x[N_FFT];
    static q15_cplx_t X[N_FFT];
    int n;
    int worst_re = 0, worst_im = 0;
    int worst_k_re = 0, worst_k_im = 0;

    /* Same real input as test 1 -- spectrum should be conjugate symmetric. */
    for (n = 0; n < N_FFT; ++n) {
        double v = 0.5 * 32768.0 * cos(2.0 * M_PI * 37 * n / (double)N_FFT);
        if (v >  32767.0) v =  32767.0;
        if (v < -32768.0) v = -32768.0;
        x[n] = (q15_t)(v >= 0 ? floor(v + 0.5) : -floor(-v + 0.5));
    }

    fft_hwa_forward(x, X);

    int k;
    int fail = 0;
    for (k = 1; k <= N_FFT / 2 - 1; ++k) {
        int dre = (int)X[k].re - (int)X[N_FFT - k].re;
        int dim = (int)X[k].im - (int)(-X[N_FFT - k].im);
        int adre = dre < 0 ? -dre : dre;
        int adim = dim < 0 ? -dim : dim;
        if (adre > worst_re) { worst_re = adre; worst_k_re = k; }
        if (adim > worst_im) { worst_im = adim; worst_k_im = k; }
        if (adre > 3 || adim > 3) {
            fail = 1;
        }
    }

    printf("[fft] conj sym: worst |dRe|=%d (k=%d) worst |dIm|=%d (k=%d), tol=3 LSB\n",
           worst_re, worst_k_re, worst_im, worst_k_im);

    if (!fail) {
        test_pass("conjugate symmetry X[k] == conj(X[N-k])");
    } else {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "worst |dRe|=%d at k=%d, |dIm|=%d at k=%d, exceeds 3 LSB",
                 worst_re, worst_k_re, worst_im, worst_k_im);
        test_fail("conjugate symmetry X[k] == conj(X[N-k])", buf);
    }
}

/* ---------- Test 3: round-trip ---------- */
/* Deterministic PRNG (xorshift32). */
static uint32_t prng_state = 0xC5515u;
static uint32_t prng_next(void) {
    uint32_t s = prng_state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    prng_state = s;
    return s;
}

static void test_round_trip(void) {
    static q15_t x_in[N_FFT];
    static q15_t x_out[N_FFT];
    static q15_cplx_t X[N_FFT];
    int n;
    int peak_in = 0;

    /* Random-ish input, modest amplitude so forward path doesn't saturate.
     * The HWA forward scaling is 1/N, so even full-scale fits, but we keep
     * inputs around +/-8000 so that the inverse output (no per-stage shift)
     * never hits the saturation limit. */
    prng_state = 0xC5515u;
    for (n = 0; n < N_FFT; ++n) {
        /* Random samples in roughly +/-16000 (half full-scale) so the
         * forward path has plenty of headroom but the per-bin SNR is
         * good enough for a < 10% round-trip RMS. */
        int32_t r = (int32_t)(prng_next() & 0x7FFFu) - 0x4000;  /* +/-16384 */
        x_in[n] = (q15_t)r;
        int a = r < 0 ? -r : r;
        if (a > peak_in) peak_in = a;
    }

    fft_hwa_forward(x_in, X);
    fft_hwa_inverse(X, x_out);

    /* RMS of (in - out) / peak(in). */
    double sum_sq = 0.0;
    int worst_diff = 0;
    for (n = 0; n < N_FFT; ++n) {
        int d = (int)x_in[n] - (int)x_out[n];
        int ad = d < 0 ? -d : d;
        if (ad > worst_diff) worst_diff = ad;
        sum_sq += (double)d * (double)d;
    }
    double rms = sqrt(sum_sq / (double)N_FFT);
    double rel = rms / (double)peak_in;

    printf("[fft] round-trip: peak_in=%d worst|diff|=%d rms=%.3f rel_rms=%.5f\n",
           peak_in, worst_diff, rms, rel);

    if (rel < 0.10) {
        test_pass("round-trip rel-RMS < 0.10");
    } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "rel_rms=%.4f >= 0.10", rel);
        test_fail("round-trip rel-RMS < 0.10", buf);
    }
}

int main(void) {
    fft_hwa_init();
    cycles_reset();

    test_peak_detection();
    test_conjugate_symmetry();
    test_round_trip();

    printf("[fft] last_cycles=%u total_cycles=%llu\n",
           (unsigned)fft_hwa_last_cycles(),
           (unsigned long long)cycles_total());

    if (g_fail_count == 0) {
        printf("[fft] ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("[fft] SOME TESTS FAILED (%d pass, %d fail)\n",
               g_pass_count, g_fail_count);
        return 1;
    }
}
