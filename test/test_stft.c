/* ============================================================
 * test_stft.c  --  Unit tests for src/algo/stft.[ch]
 *
 * Build:
 *   gcc -std=c99 -O2 -Isrc test/test_stft.c src/algo/stft.c \
 *       -o /tmp/test_stft -lm
 *
 * Run:
 *   /tmp/test_stft
 * ============================================================ */

#include "algo/stft.h"
#include "util/fixed_point.h"
#include "util/window_table.h"
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures = 0;

#define CHECK(cond, msg) do {                                           \
    if (!(cond)) {                                                      \
        fprintf(stderr, "[stft] FAIL: %s  (%s:%d)\n",                   \
                msg, __FILE__, __LINE__);                               \
        g_failures++;                                                   \
    }                                                                   \
} while (0)

/* ------------------------------------------------------------ */
/* Test 1: stft_init zeroes both buffers.                       */
/* ------------------------------------------------------------ */
static void test_init_zeros(void)
{
    stft_state_t s;
    /* Deliberately fill with non-zero garbage first. */
    memset(&s, 0xA5, sizeof(s));

    stft_init(&s);

    int ok_hist = 1, ok_ola = 1;
    for (int i = 0; i < N_FFT; ++i) {
        if (s.in_history[i] != 0) ok_hist = 0;
        if (s.ola_buf[i]    != 0) ok_ola  = 0;
    }
    CHECK(ok_hist, "in_history not zeroed by stft_init");
    CHECK(ok_ola,  "ola_buf not zeroed by stft_init");
}

/* ------------------------------------------------------------ */
/* Test 2: Push-shift correctness.                              */
/*                                                              */
/* Push four hops carrying values 1..256, 257..512, 513..768,   */
/* 769..1024. After the 4th push, in_history (pre-window)       */
/* should be exactly [1..1024]. We verify by checking           */
/* frame_out[i] == mul_q15(i+1, hamming_q15[i]) for sampled i.  */
/* ------------------------------------------------------------ */
static void test_push_shift(void)
{
    stft_state_t s;
    stft_init(&s);

    q15_t hop[HOP];
    q15_t frame[N_FFT];

    /* Hop 1: samples 1..256 */
    for (int i = 0; i < HOP; ++i) hop[i] = (q15_t)(i + 1);
    stft_push_hop(&s, hop, frame);
    /* After hop1, in_history = [0...0, 1..256] -- nothing to check yet. */

    /* Hop 2: samples 257..512 */
    for (int i = 0; i < HOP; ++i) hop[i] = (q15_t)(HOP + i + 1);
    stft_push_hop(&s, hop, frame);

    /* Hop 3: samples 513..768 */
    for (int i = 0; i < HOP; ++i) hop[i] = (q15_t)(2 * HOP + i + 1);
    stft_push_hop(&s, hop, frame);

    /* Hop 4: samples 769..1024.  After this push, in_history==[1..1024]. */
    for (int i = 0; i < HOP; ++i) hop[i] = (q15_t)(3 * HOP + i + 1);
    stft_push_hop(&s, hop, frame);

    /* Verify a handful of indices across the buffer. */
    const int probes[] = { 0, 1, 100, 255, 256, 512, 768, 1000, 1022, 1023 };
    const int n_probes = (int)(sizeof(probes) / sizeof(probes[0]));

    int ok = 1;
    for (int k = 0; k < n_probes; ++k) {
        int   i        = probes[k];
        q15_t expected = mul_q15((q15_t)(i + 1), hamming_q15[i]);
        if (frame[i] != expected) {
            fprintf(stderr,
                    "[stft]   probe i=%d: got %d, expected %d "
                    "(history=%d, w=%d)\n",
                    i, frame[i], expected, (int)(i + 1),
                    (int)hamming_q15[i]);
            ok = 0;
        }
    }
    CHECK(ok, "push-shift produced wrong frame_out values");

    /* Also: in_history itself should literally contain [1..1024]. */
    int ok_hist = 1;
    for (int i = 0; i < N_FFT; ++i) {
        if (s.in_history[i] != (q15_t)(i + 1)) { ok_hist = 0; break; }
    }
    CHECK(ok_hist, "in_history contents after 4 hops not [1..1024]");
}

/* ------------------------------------------------------------ */
/* Test 3: Identity reconstruction.                             */
/*                                                              */
/* Drive with a 200 Hz cosine sampled at 16 kHz. For each       */
/* frame: push hop, then feed the windowed frame straight       */
/* through to stft_overlap_add (no FFT/iFFT, the synthesis      */
/* window then squares the analysis window).                    */
/*                                                              */
/* After warm-up, the output should match the input up to a     */
/* COLA scaling factor (~0.54 for Hamming^2 at 75% overlap).    */
/* ------------------------------------------------------------ */
static void test_identity_reconstruction(void)
{
    stft_state_t s;
    stft_init(&s);

    const int   N_FRAMES = 30;
    const int   WARMUP   = 5;
    const float FREQ_HZ  = 200.0f;
    const float AMP      = 0.5f;  /* Q15 amplitude headroom */

    /* Generate the input signal: enough samples for N_FRAMES hops. */
    const int N_SAMPLES = N_FRAMES * HOP;
    q15_t *in_signal  = (q15_t*)malloc(sizeof(q15_t) * N_SAMPLES);
    q15_t *out_signal = (q15_t*)malloc(sizeof(q15_t) * N_SAMPLES);
    if (!in_signal || !out_signal) {
        CHECK(0, "malloc failed");
        free(in_signal); free(out_signal);
        return;
    }

    for (int n = 0; n < N_SAMPLES; ++n) {
        float x = AMP * cosf(2.0f * (float)M_PI * FREQ_HZ * (float)n
                             / (float)FS);
        in_signal[n] = float_to_q15(x);
    }

    /* Stream through analysis + (windowed) synthesis. */
    q15_t frame[N_FFT];
    q15_t hop_out[HOP];

    for (int f = 0; f < N_FRAMES; ++f) {
        stft_push_hop(&s, &in_signal[f * HOP], frame);
        /* Identity path: feed analysis-windowed frame back in.
         * overlap_add will multiply by the window again, so the
         * effective per-frame envelope is w^2[n]. */
        stft_overlap_add(&s, frame, hop_out);
        memcpy(&out_signal[f * HOP], hop_out, HOP * sizeof(q15_t));
    }

    /* The analysis/synthesis pipeline has inherent latency: each
     * stft_push_hop fills in_history[N_FFT-1] with the newest sample,
     * but stft_overlap_add emits the LEFTMOST HOP samples of ola_buf.
     * Those leftmost samples correspond to input that arrived
     * (N_FFT/HOP - 1) = 3 hops ago. So we align out_signal[n] with
     * in_signal[n - 3*HOP] for comparison. */
    const int DELAY = (N_FFT / HOP - 1) * HOP;   /* 3 * HOP = 768 */
    const int s_lo = WARMUP * HOP;
    const int s_hi = (N_FRAMES - 1) * HOP;

    double num = 0.0, den = 0.0;
    for (int n = s_lo; n < s_hi; ++n) {
        if (n - DELAY < 0) continue;
        double x = q15_to_float(in_signal[n - DELAY]);
        double y = q15_to_float(out_signal[n]);
        num += x * y;
        den += x * x;
    }
    double scale = (den > 0.0) ? (num / den) : 0.0;

    /* Relative RMS error after applying the optimal scale. */
    double err_sq = 0.0, sig_sq = 0.0;
    for (int n = s_lo; n < s_hi; ++n) {
        if (n - DELAY < 0) continue;
        double x = q15_to_float(in_signal[n - DELAY]);
        double y = q15_to_float(out_signal[n]);
        double e = scale * x - y;
        err_sq += e * e;
        sig_sq += (scale * x) * (scale * x);
    }
    double rel_rms = (sig_sq > 0.0) ? sqrt(err_sq / sig_sq) : 1.0;

    printf("[stft]   identity test: scale=%.4f  rel_rms=%.4f  "
           "(expected scale ~ 0.54)\n", scale, rel_rms);

    CHECK(rel_rms < 0.30,
          "identity reconstruction relative RMS error >= 0.30");

    free(in_signal);
    free(out_signal);
}

/* ------------------------------------------------------------ */
int main(void)
{
    test_init_zeros();
    test_push_shift();
    test_identity_reconstruction();

    if (g_failures == 0) {
        printf("[stft] ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("[stft] SOME TESTS FAILED (%d failures)\n", g_failures);
        return 1;
    }
}
