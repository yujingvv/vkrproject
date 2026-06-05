/* ============================================================
 * wiener.h  --  Decision-directed Wiener filter (per-bin state)
 *
 * Mirrors matlab/compress.m lines 47-58 with one streaming deviation:
 *
 *   MATLAB pre-computes noise_power from frames 1..N_NOISE and then
 *   applies Wiener gain starting at frame 1.  That requires N_NOISE
 *   frames of look-ahead, which a real-time stream cannot afford.
 *
 *   This implementation passes the first NOISE_PROFILE_FRAMES (=13)
 *   frames through UNMODIFIED while accumulating the noise estimate;
 *   gain is applied from frame 14 onward.  Practical consequence:
 *   the leading 13*HOP/FS = ~208 ms of output is denoised in MATLAB
 *   but pass-through here.  Acceptable because the spec assumes the
 *   first 200 ms is noise-only (no speech), so the visible difference
 *   is on the noise tail, not on speech content.  If speech is present
 *   in the first 208 ms, expect a brief difference vs MATLAB at the
 *   start.  No fix needed for streaming use.
 *
 * Q-format strategy:
 *   - State: noise_power[k], prev_clean_power[k] stored as q31_t
 *     (Q30, since |Z|^2 of a Q15 complex is bounded by 2^31-1).
 *   - noise_accum[k] is q63 used ONLY during the estimation phase
 *     to avoid q31 saturation when summing up to 13 frames of
 *     near-full-scale |Z|^2.
 *   - SNR ratios computed in pure Q-format (q63 intermediate, q31
 *     state, q15 gain).  No floating-point in the hot path: a single
 *     q63 division per bin produces the gain directly.  Acceptable
 *     for C5515 which has a 32/32 hardware divide path.
 *   - Wiener gain in [0,1) -> Q15.
 * ============================================================ */

#ifndef TMS320_WIENER_H
#define TMS320_WIENER_H

#include "../config.h"
#include "../util/fixed_point.h"

typedef struct {
    /* Per-bin state, q31 (Q30 representation of |Z|^2). */
    q31_t noise_power[N_BINS];        /* averaged noise PSD            */
    q31_t prev_clean_power[N_BINS];   /* |Zxx_denoised[k]|^2 last frame */
    /* Used only during the noise-estimation phase. q63 to safely
     * accumulate up to NOISE_PROFILE_FRAMES * (2^31 - 1).  Idle after
     * noise_ready becomes 1 but the bytes remain in the struct. */
    q63_t noise_accum[N_BINS];
    int   noise_frame_count;          /* 0..NOISE_PROFILE_FRAMES        */
    int   noise_ready;                /* 0 during noise estimation, 1 after */
} wiener_state_t;

void wiener_init(wiener_state_t *s);

/* In-place: applies Wiener gain to spectrum[0..N_BINS-1].
 * During noise-estimation phase (first NOISE_PROFILE_FRAMES frames),
 * spectrum is left unchanged but noise_power is accumulated. */
void wiener_process(wiener_state_t *s, q15_cplx_t spectrum[N_BINS]);

#endif /* TMS320_WIENER_H */
