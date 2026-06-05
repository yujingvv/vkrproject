/* ============================================================
 * vad_decision.c  --  HLR decision + causal streaming crossfade
 *
 * Reference: matlab/compress.m lines 62-87.
 *
 * Energies are accumulated as q63_t to safely fit
 * N_BINS * (Q15_max)^2 ~= 513 * 2^30 < 2^40.
 * ============================================================ */

#include "vad_decision.h"

/* With TRANSITION_FRAMES = 2 the ramp must traverse Q15_ONE in
 * TRANSITION_FRAMES steps, i.e. step = Q15_ONE / TRANSITION_FRAMES. */
#define VAD_FADE_STEP   (Q15_ONE / TRANSITION_FRAMES)

void vad_init(vad_state_t *s)
{
    s->prev_switch    = 0;
    s->fade_remaining = 0;
    s->fade_direction = 0;
    s->current_mix    = 0;
}

q15_t vad_compute_mix_ratio(vad_state_t *s, const q15_cplx_t spectrum[N_BINS])
{
    /* ---- 1. Accumulate band energies in Q30 ---- */
    q63_t energy_low  = 0;
    q63_t energy_high = 0;
    int   k;

    for (k = HLR_LOW_LO_BIN; k < HLR_LOW_HI_BIN; ++k) {
        energy_low += (q63_t)cplx_mag_sq_q30(spectrum[k]);
    }
    for (k = SOURCE_LO_BIN; k < SOURCE_HI_BIN; ++k) {
        energy_high += (q63_t)cplx_mag_sq_q30(spectrum[k]);
    }

    /* ---- 2. Hard decision: energy_high > threshold * energy_low ----
     * Multiplying q63_t accumulator by HLR_THRESHOLD_Q15 then >> 15. */
    q63_t scaled_low = (energy_low * (q63_t)HLR_THRESHOLD_Q15) >> 15;
    int   sw = (energy_high > scaled_low) ? 1 : 0;

    /* ---- 3. Edge detection -> start a fade ---- */
    if (s->prev_switch == 0 && sw == 1) {
        /* rising edge: start fading in */
        s->fade_direction = +1;
        s->fade_remaining = TRANSITION_FRAMES;
    } else if (s->prev_switch == 1 && sw == 0) {
        /* falling edge: start fading out */
        s->fade_direction = -1;
        s->fade_remaining = TRANSITION_FRAMES;
    }

    /* ---- 4. Apply fade step, then clamp / hold ---- */
    if (s->fade_remaining > 0) {
        q31_t next = (q31_t)s->current_mix
                   + (q31_t)s->fade_direction * (q31_t)VAD_FADE_STEP;
        if (next > Q15_ONE) next = Q15_ONE;
        if (next < 0)       next = 0;
        s->current_mix = (q15_t)next;
        s->fade_remaining--;

        if (s->fade_remaining == 0) {
            /* Snap to the rail exactly to avoid step-size drift. */
            s->current_mix    = (s->fade_direction > 0) ? Q15_ONE : 0;
            s->fade_direction = 0;
        }
    } else {
        /* Hold rail dictated by current hard decision. */
        s->current_mix = sw ? Q15_ONE : 0;
    }

    s->prev_switch = sw;
    return s->current_mix;
}
