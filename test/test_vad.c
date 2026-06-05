/* ============================================================
 * test_vad.c  --  Unit test for vad_decision
 *
 * Build:
 *   gcc -std=c99 -O2 -Isrc test/test_vad.c src/algo/vad_decision.c \
 *       -o /tmp/test_vad -lm
 * ============================================================ */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "util/fixed_point.h"
#include "algo/vad_decision.h"

static int g_failures = 0;

#define EXPECT(cond, msg) do {                                       \
    if (!(cond)) {                                                   \
        printf("[vad] FAIL: %s  (line %d)\n", (msg), __LINE__);      \
        ++g_failures;                                                \
    } else {                                                         \
        printf("[vad] ok  : %s\n", (msg));                           \
    }                                                                \
} while (0)

static void make_low_energy(q15_cplx_t spec[N_BINS]) {
    memset(spec, 0, sizeof(q15_cplx_t) * N_BINS);
    /* Put energy ONLY in HLR_LOW band (bins 64..191): high mag */
    for (int k = HLR_LOW_LO_BIN; k < HLR_LOW_HI_BIN; ++k) {
        spec[k].re = 10000;
    }
}

static void make_high_energy(q15_cplx_t spec[N_BINS]) {
    memset(spec, 0, sizeof(q15_cplx_t) * N_BINS);
    /* Put energy ONLY in SOURCE band (bins 256..511): high mag */
    for (int k = SOURCE_LO_BIN; k < SOURCE_HI_BIN; ++k) {
        spec[k].re = 10000;
    }
}

int main(void)
{
    vad_state_t  s;
    q15_cplx_t   spec[N_BINS];
    q15_t        r;

    /* ---- 1. Init clears state ---- */
    /* Pre-dirty to make sure init actually zeros everything. */
    s.prev_switch    = 7;
    s.fade_remaining = 9;
    s.fade_direction = -3;
    s.current_mix    = 12345;
    vad_init(&s);
    EXPECT(s.prev_switch    == 0, "init: prev_switch == 0");
    EXPECT(s.fade_remaining == 0, "init: fade_remaining == 0");
    EXPECT(s.fade_direction == 0, "init: fade_direction == 0");
    EXPECT(s.current_mix    == 0, "init: current_mix == 0");

    /* ---- 2. Five low-energy frames -> mix stays 0 ---- */
    make_low_energy(spec);
    for (int i = 0; i < 5; ++i) {
        r = vad_compute_mix_ratio(&s, spec);
        EXPECT(r == 0, "low-energy frame returns 0");
    }

    /* ---- 3. Sudden switch to high energy: ramp up ---- */
    make_high_energy(spec);
    r = vad_compute_mix_ratio(&s, spec);
    /* After first step, expect ~Q15_ONE/2. With Q15_ONE=32767 and
     * TRANSITION_FRAMES=2, step = 16383, so current_mix == 16383. */
    printf("[vad] info: first ramp value = %d (expect ~16384)\n", r);
    EXPECT(r > 16000 && r < 17000, "first ramp step ~= Q15_ONE/2");

    r = vad_compute_mix_ratio(&s, spec);
    EXPECT(r == Q15_ONE, "second ramp step reaches Q15_ONE");

    /* ---- 4. Sustained high energy -> hold at Q15_ONE ---- */
    for (int i = 0; i < 3; ++i) {
        r = vad_compute_mix_ratio(&s, spec);
        EXPECT(r == Q15_ONE, "sustained high holds at Q15_ONE");
    }

    /* ---- 5. Drop to low: ramp down to 0 ---- */
    make_low_energy(spec);
    q15_t prev = Q15_ONE;
    r = vad_compute_mix_ratio(&s, spec);
    printf("[vad] info: first ramp-down value = %d\n", r);
    EXPECT(r < prev, "ramp-down: first step decreases");
    prev = r;
    r = vad_compute_mix_ratio(&s, spec);
    EXPECT(r == 0, "ramp-down: second step reaches 0");
    r = vad_compute_mix_ratio(&s, spec);
    EXPECT(r == 0, "ramp-down: held at 0");

    if (g_failures == 0) {
        printf("[vad] ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("[vad] SOME TESTS FAILED (%d)\n", g_failures);
        return 1;
    }
}
