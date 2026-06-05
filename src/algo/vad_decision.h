/* ============================================================
 * vad_decision.h  --  HLR-based VAD + causal crossfade
 *
 * Implements the high-/low-band energy ratio (HLR) decision
 * described in matlab/compress.m (lines 62-87) and produces
 * a Q15 mix ratio used by the transposition stage.
 *
 *   mix_ratio = 0      => denoised spectrum passes through unchanged
 *   mix_ratio = 32767  => use fully transposed spectrum
 *   in between         => smooth (causal) crossfade
 * ============================================================ */

#ifndef TMS320_VAD_DECISION_H
#define TMS320_VAD_DECISION_H

#include "../config.h"
#include "../util/fixed_point.h"

typedef struct {
    int   prev_switch;     /* last frame's hard decision (0/1)    */
    int   fade_remaining;  /* frames left in current crossfade    */
    int   fade_direction;  /* +1 fading in, -1 fading out, 0 hold */
    q15_t current_mix;     /* current mix ratio in Q15            */
} vad_state_t;

/* Reset the VAD state to a clean (no-transposition) condition. */
void  vad_init(vad_state_t *s);

/* Process one frame. Returns the mix ratio in Q15 (0..32767) to
 * be applied by the transposition module. */
q15_t vad_compute_mix_ratio(vad_state_t *s, const q15_cplx_t spectrum[N_BINS]);

#endif /* TMS320_VAD_DECISION_H */
