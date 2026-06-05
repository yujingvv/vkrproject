/* ============================================================
 * test_transpose.c  --  Standalone tests for transpose_apply().
 *
 * Build:
 *   gcc -std=c99 -O2 -Isrc test/test_transpose.c \
 *       src/algo/transpose.c -o /tmp/test_transpose -lm
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algo/transpose.h"
#include "config.h"
#include "util/fixed_point.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (!(cond)) {                                              \
            ++g_failures;                                           \
            fprintf(stderr,                                         \
                    "  FAIL: %s (%s:%d)\n",                         \
                    (msg), __FILE__, __LINE__);                     \
        }                                                           \
    } while (0)

static int abs_diff(int a, int b) { return a > b ? a - b : b - a; }

/* ---------------------------------------------------------------- */
/* Test 1: mix=0 must reproduce the input exactly.                  */
/* ---------------------------------------------------------------- */
static void test_mix_zero_identity(void)
{
    q15_cplx_t in[N_BINS];
    q15_cplx_t out[N_BINS];

    srand(0xC0FFEEu);
    for (int k = 0; k < N_BINS; ++k) {
        /* Span the full Q15 range, including negatives. */
        in[k].re = (q15_t)((rand() & 0xFFFF) - 32768);
        in[k].im = (q15_t)((rand() & 0xFFFF) - 32768);
    }

    transpose_apply(in, out, 0);

    int mismatched = 0;
    for (int k = 0; k < N_BINS; ++k) {
        /* With mix_ratio = 0, one_minus_mix = Q15_ONE = 32767.
         * mul_q15(32767, x) = (32767 * x) >> 15. For x = -32768
         * this returns -32767 (not -32768) -- a single-LSB
         * artefact of the Q15 representation, not a real
         * mismatch. Accept |error| <= 1. */
        if (abs_diff(out[k].re, in[k].re) > 1 ||
            abs_diff(out[k].im, in[k].im) > 1) {
            ++mismatched;
        }
    }
    CHECK(mismatched == 0, "mix=0 should reproduce input (<=1 LSB)");

    printf("[test 1] mix=0 identity: %s (mismatched=%d)\n",
           mismatched == 0 ? "PASS" : "FAIL", mismatched);
}

/* ---------------------------------------------------------------- */
/* Test 2: single source impulse at bin 320 with mix=Q15_ONE.       */
/* ---------------------------------------------------------------- */
static void test_impulse_full_transpose(void)
{
    q15_cplx_t in[N_BINS];
    q15_cplx_t out[N_BINS];
    memset(in, 0, sizeof(in));

    const int k_src = 320;        /* 5 kHz region            */
    const int k_tgt = TARGET_LO_BIN + (k_src - SOURCE_LO_BIN) / 2; /* 160 */
    in[k_src].re = 4000;
    in[k_src].im = 0;

    transpose_apply(in, out, Q15_ONE);

    /* (a) source bin must be zero in the output. */
    CHECK(out[k_src].re == 0 && out[k_src].im == 0,
          "source bin should be zeroed");

    /* (b) target bin should be ~4000. mul_q15(Q15_ONE, 4000)
     *     = (32767 * 4000) >> 15 = 3999. Allow +/- 5. */
    int diff_re = abs_diff(out[k_tgt].re, 4000);
    CHECK(diff_re <= 5, "target bin should be approx. 4000");

    /* (c) every other bin in the target band must be 0. With
     *     gamma=1/2, integer source bins map to integer target
     *     bins -- no fractional spillover. */
    int spurious = 0;
    for (int k = TARGET_LO_BIN; k < TARGET_HI_BIN; ++k) {
        if (k == k_tgt) continue;
        if (out[k].re != 0 || out[k].im != 0) ++spurious;
    }
    CHECK(spurious == 0, "no spillover in target band");

    printf("[test 2] impulse mix=Q15_ONE: k_tgt=%d out.re=%d "
           "diff=%d spurious=%d\n",
           k_tgt, out[k_tgt].re, diff_re, spurious);
}

/* ---------------------------------------------------------------- */
/* Test 3: source band filled, mix=Q15_ONE => source bins == 0.     */
/* ---------------------------------------------------------------- */
static void test_source_band_cleared(void)
{
    q15_cplx_t in[N_BINS];
    q15_cplx_t out[N_BINS];
    memset(in, 0, sizeof(in));

    for (int k = SOURCE_LO_BIN; k < SOURCE_HI_BIN; ++k) {
        in[k].re = 5000;
        in[k].im = 5000;
    }

    transpose_apply(in, out, Q15_ONE);

    int nonzero = 0;
    for (int k = SOURCE_LO_BIN; k < SOURCE_HI_BIN; ++k) {
        if (out[k].re != 0 || out[k].im != 0) ++nonzero;
    }
    CHECK(nonzero == 0, "all source bins should be zero");

    printf("[test 3] source band cleared: nonzero=%d\n", nonzero);
}

/* ---------------------------------------------------------------- */
/* Test 4: partial mix (mix_ratio = Q15_HALF).                      */
/* ---------------------------------------------------------------- */
static void test_partial_mix(void)
{
    q15_cplx_t in[N_BINS];
    q15_cplx_t out[N_BINS];
    memset(in, 0, sizeof(in));

    const int k_src = 320;
    const int k_tgt = TARGET_LO_BIN + (k_src - SOURCE_LO_BIN) / 2; /* 160 */
    in[k_src].re = 4000;
    in[k_src].im = 0;

    transpose_apply(in, out, Q15_HALF);

    /* Target bin: 0.5 * clean(=0) + 0.5 * transposed(=4000) ~ 2000 */
    int diff_tgt = abs_diff(out[k_tgt].re, 2000);
    CHECK(diff_tgt <= 5, "target bin ~ 2000 at mix=0.5");

    /* Source bin: 0.5 * clean(=4000) + 0.5 * transposed(=0) ~ 2000 */
    int diff_src = abs_diff(out[k_src].re, 2000);
    CHECK(diff_src <= 5, "source bin ~ 2000 at mix=0.5");

    printf("[test 4] partial mix: out[%d].re=%d (~2000), "
           "out[%d].re=%d (~2000)\n",
           k_tgt, out[k_tgt].re, k_src, out[k_src].re);
}

/* ---------------------------------------------------------------- */
int main(void)
{
    printf("=== transpose unit tests ===\n");
    test_mix_zero_identity();
    test_impulse_full_transpose();
    test_source_band_cleared();
    test_partial_mix();

    if (g_failures == 0) {
        printf("[transpose] ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("[transpose] SOME TESTS FAILED (%d failure%s)\n",
               g_failures, g_failures == 1 ? "" : "s");
        return 1;
    }
}
