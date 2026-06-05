/* ============================================================
 * test_wiener.c  --  Standalone tests for the Wiener module.
 *
 * Build:
 *   gcc -std=c99 -O2 -Isrc test/test_wiener.c src/algo/wiener.c \
 *       -o /tmp/test_wiener -lm
 * Run:
 *   /tmp/test_wiener
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "algo/wiener.h"
#include "config.h"
#include "util/fixed_point.h"

static int g_failed = 0;

#define CHECK(cond, msg) do {                                           \
    if (!(cond)) {                                                      \
        fprintf(stderr, "  FAIL: %s  (line %d)\n", (msg), __LINE__);    \
        g_failed = 1;                                                   \
    } else {                                                            \
        fprintf(stdout, "  ok:   %s\n", (msg));                         \
    }                                                                   \
} while (0)

/* Fill a spectrum with low-magnitude pseudo-random noise. */
static void fill_noise(q15_cplx_t spec[N_BINS], int amp)
{
    for (int k = 0; k < N_BINS; ++k) {
        int r1 = rand();
        int r2 = rand();
        /* range [-amp, +amp) */
        spec[k].re = (q15_t)((r1 % (2 * amp + 1)) - amp);
        spec[k].im = (q15_t)((r2 % (2 * amp + 1)) - amp);
    }
}

static double total_energy(const q15_cplx_t spec[N_BINS])
{
    double e = 0.0;
    for (int k = 0; k < N_BINS; ++k) {
        double r = (double)spec[k].re;
        double i = (double)spec[k].im;
        e += r * r + i * i;
    }
    return e;
}

/* -------------------- Test 1: init -------------------- */
static void test_init(void)
{
    fprintf(stdout, "[test_init]\n");
    /* wiener_state_t is ~8 KB (Q63 noise_accum) so keep it off the
     * stack; safe here on PC, mandatory on a future on-chip test. */
    static wiener_state_t s;
    /* poison */
    memset(&s, 0xAA, sizeof(s));
    wiener_init(&s);

    int all_zero_np = 1, all_zero_pc = 1;
    for (int k = 0; k < N_BINS; ++k) {
        if (s.noise_power[k]      != 0) all_zero_np = 0;
        if (s.prev_clean_power[k] != 0) all_zero_pc = 0;
    }
    CHECK(all_zero_np,                  "noise_power[] all zero after init");
    CHECK(all_zero_pc,                  "prev_clean_power[] all zero after init");
    CHECK(s.noise_frame_count == 0,     "noise_frame_count == 0 after init");
    CHECK(s.noise_ready       == 0,     "noise_ready == 0 after init");
}

/* -------------------- Test 2: noise estimation phase -------------------- */
static void test_noise_estimation(wiener_state_t *s)
{
    fprintf(stdout, "[test_noise_estimation]\n");
    srand(0xC0FFEE);

    q15_cplx_t spec[N_BINS];
    const int amp = 200;  /* small noise */

    int became_ready = 0;
    for (int f = 0; f < NOISE_PROFILE_FRAMES; ++f) {
        fill_noise(spec, amp);
        /* Snapshot to confirm pass-through */
        q15_cplx_t snap[N_BINS];
        memcpy(snap, spec, sizeof(snap));

        wiener_process(s, spec);

        if (memcmp(snap, spec, sizeof(snap)) != 0) {
            fprintf(stderr, "  FAIL: spectrum modified during noise-estimation frame %d\n", f);
            g_failed = 1;
        }
        if (f == NOISE_PROFILE_FRAMES - 1 && s->noise_ready) {
            became_ready = 1;
        }
    }

    CHECK(became_ready,             "noise_ready becomes 1 after 13th frame");
    CHECK(s->noise_frame_count == NOISE_PROFILE_FRAMES,
                                    "noise_frame_count == NOISE_PROFILE_FRAMES");

    int all_nonzero = 1;
    for (int k = 0; k < N_BINS; ++k) {
        if (s->noise_power[k] == 0) { all_nonzero = 0; break; }
    }
    CHECK(all_nonzero,              "noise_power[k] non-zero for all bins");
}

/* -------------------- Test 3: pure-noise attenuation -------------------- */
static void test_noise_attenuation(wiener_state_t *s)
{
    fprintf(stdout, "[test_noise_attenuation]\n");
    /* Keep seeding consistent so noise statistics match what was estimated. */
    /* Use same amp as estimation phase. */
    const int amp = 200;
    const int n_frames = 50;

    q15_cplx_t spec[N_BINS];

    /* Measure average energy in vs out across n_frames. */
    double sum_in = 0.0, sum_out = 0.0;
    for (int f = 0; f < n_frames; ++f) {
        fill_noise(spec, amp);
        double e_in = total_energy(spec);
        wiener_process(s, spec);
        double e_out = total_energy(spec);
        sum_in  += e_in;
        sum_out += e_out;
    }

    double mean_in  = sum_in  / n_frames;
    double mean_out = sum_out / n_frames;
    double db_drop  = 10.0 * log10(mean_in / (mean_out + 1e-30));

    fprintf(stdout, "  mean energy in : %.3e\n", mean_in);
    fprintf(stdout, "  mean energy out: %.3e\n", mean_out);
    fprintf(stdout, "  attenuation    : %.2f dB\n", db_drop);

    CHECK(db_drop > 12.0,
          "pure-noise output energy drops > 12 dB after Wiener");
}

/* -------------------- Test 4: signal preservation -------------------- */
static void test_signal_preservation(wiener_state_t *s)
{
    fprintf(stdout, "[test_signal_preservation]\n");
    const int amp = 200;
    const int signal_bin = 100;
    const q15_t signal_re = 20000;

    q15_cplx_t spec[N_BINS];
    fill_noise(spec, amp);
    spec[signal_bin].re = signal_re;
    spec[signal_bin].im = 0;

    /* Capture input magnitude squared at the signal bin. */
    double in_mag_sq = (double)signal_re * signal_re;

    /* Capture energy at a representative noise-only bin for compare. */
    int noise_bin = 50;
    double noise_in_mag_sq = (double)spec[noise_bin].re * spec[noise_bin].re
                           + (double)spec[noise_bin].im * spec[noise_bin].im;

    wiener_process(s, spec);

    double out_mag_sq = (double)spec[signal_bin].re * spec[signal_bin].re
                      + (double)spec[signal_bin].im * spec[signal_bin].im;
    double noise_out_mag_sq = (double)spec[noise_bin].re * spec[noise_bin].re
                            + (double)spec[noise_bin].im * spec[noise_bin].im;

    double ratio_signal = sqrt(out_mag_sq / in_mag_sq);
    /* Attenuation at noise bin: out/in ratio (smaller = better attenuation) */
    double ratio_noise  = (noise_in_mag_sq > 0.0)
                          ? sqrt(noise_out_mag_sq / noise_in_mag_sq)
                          : 0.0;

    fprintf(stdout, "  signal bin %d |out|/|in| = %.4f\n", signal_bin, ratio_signal);
    fprintf(stdout, "  noise  bin %d |out|/|in| = %.4f\n", noise_bin,  ratio_noise);

    CHECK(ratio_signal > 0.5,
          "magnitude at signal bin preserved (out > 0.5 * in)");
    CHECK(ratio_noise  < ratio_signal,
          "noise bin attenuated more than signal bin");
}

int main(void)
{
    fprintf(stdout, "==== Wiener module tests ====\n");

    /* Test 1: init on a fresh state */
    test_init();

    /* Tests 2-4 share one state so the Wiener filter sees coherent stats.
     * Static (off-stack) since the struct is ~8 KB. */
    static wiener_state_t s;
    wiener_init(&s);
    test_noise_estimation(&s);
    test_noise_attenuation(&s);
    test_signal_preservation(&s);

    if (g_failed) {
        fprintf(stdout, "[wiener] SOME TESTS FAILED\n");
        return 1;
    }
    fprintf(stdout, "[wiener] ALL TESTS PASSED\n");
    return 0;
}
