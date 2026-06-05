/* ============================================================
 * wiener.c  --  Decision-directed Wiener filter implementation
 *
 * See wiener.h and docs/specs/2026-05-14-tms320-port-design.md
 * section 6.3 for the algorithmic spec. MATLAB reference is
 * matlab/compress.m lines 47-58.
 *
 * Streaming adaptation:
 *   frames 1..NOISE_PROFILE_FRAMES : accumulate |Z|^2, pass-through
 *   frame  NOISE_PROFILE_FRAMES    : divide accumulator by N
 *                                    -> noise_power ready
 *   frames NOISE_PROFILE_FRAMES+1+ : apply Wiener gain in-place
 * ============================================================ */

#include "wiener.h"
#include <string.h>

/* (1 - alpha) in Q15.  WIENER_ALPHA_Q15 = 32113 -> 32767 - 32113 = 654. */
#define ONE_MINUS_ALPHA_Q15  ((q15_t)(Q15_ONE - WIENER_ALPHA_Q15))

/* Minimum noise floor.  q31_t value of 1 corresponds to ~1/2^30 of
 * full scale; tiny but enough to avoid division by zero. */
#define NOISE_FLOOR_MIN      ((q31_t)1)

void wiener_init(wiener_state_t *s)
{
    memset(s->noise_power,      0, sizeof(s->noise_power));
    memset(s->prev_clean_power, 0, sizeof(s->prev_clean_power));
    memset(s->noise_accum,      0, sizeof(s->noise_accum));
    s->noise_frame_count = 0;
    s->noise_ready       = 0;
}

void wiener_process(wiener_state_t *s, q15_cplx_t spectrum[N_BINS])
{
    int k;

    if (!s->noise_ready) {
        /* ---- Noise-estimation phase: accumulate |Z|^2 in q63 ----
         * cplx_mag_sq_q30 returns up to ~2^31 - 2 per bin.  Summing 13
         * frames can reach ~2^35, so accumulate into q63 without
         * saturation; saturate only at finalize. */
        for (k = 0; k < N_BINS; ++k) {
            s->noise_accum[k] += (q63_t)cplx_mag_sq_q30(spectrum[k]);
        }

        s->noise_frame_count++;

        if (s->noise_frame_count >= NOISE_PROFILE_FRAMES) {
            for (k = 0; k < N_BINS; ++k) {
                q63_t avg = s->noise_accum[k] / NOISE_PROFILE_FRAMES;
                if (avg > 0x7FFFFFFFLL) avg = 0x7FFFFFFFLL;
                q31_t v = (q31_t)avg;
                if (v < NOISE_FLOOR_MIN) v = NOISE_FLOOR_MIN;
                s->noise_power[k] = v;
                /* Initialize prev_clean_power with noise estimate
                 * (mirrors MATLAB: prev_clean_power_sq = noise_power_spectrum) */
                s->prev_clean_power[k] = v;
            }
            s->noise_ready = 1;
        }
        /* Pass-through during estimation: spectrum unmodified. */
        return;
    }

    /* ---- Wiener filtering phase (pure fixed-point) ----
     *
     * Algebraic refactor that avoids per-bin division entirely except
     * for the single ratio that produces the gain:
     *
     *   snr_prior = alpha * (prev_clean / noise)
     *             + (1-alpha) * max(noisy/noise - 1, 0)
     *
     *             = [ alpha * prev_clean + (1-alpha) * max(noisy-noise, 0) ]
     *               / noise
     *
     *             = numerator / noise
     *
     *   gain      = snr_prior / (snr_prior + 1)
     *             = numerator / (numerator + noise)
     *
     * One q63 division per bin replaces three float divisions in the
     * original implementation.  Numerator and denominator both fit in
     * q63 with room to spare; the quotient is in [0, 1) so a multiply
     * by Q15_ONE before the divide places the gain naturally in Q15.
     */
    for (k = 0; k < N_BINS; ++k) {
        q31_t noise_pk     = s->noise_power[k];
        if (noise_pk < NOISE_FLOOR_MIN) noise_pk = NOISE_FLOOR_MIN;
        q31_t noisy_pk     = cplx_mag_sq_q30(spectrum[k]);
        q31_t prev_clean_p = s->prev_clean_power[k];

        /* max(noisy - noise, 0) */
        q31_t excess = (noisy_pk > noise_pk) ? (noisy_pk - noise_pk) : 0;

        /* numerator = (alpha * prev_clean + (1-alpha) * excess) >> 15
         * Both terms fit easily in q63: max(q15) * max(q31) = ~2^46. */
        q63_t num64 = ((q63_t)WIENER_ALPHA_Q15  * (q63_t)prev_clean_p
                     + (q63_t)ONE_MINUS_ALPHA_Q15 * (q63_t)excess) >> 15;
        /* num64 fits in q31 (max ~ prev_clean + excess < 2^32, after shift). */
        if (num64 > 0x7FFFFFFFLL) num64 = 0x7FFFFFFFLL;
        q31_t numerator = (q31_t)num64;

        /* gain_q15 = numerator * Q15_ONE / (numerator + noise)
         * Both q63 multiplicands keep the division well-defined; the
         * result is guaranteed to be in [0, Q15_ONE]. */
        q63_t denom = (q63_t)numerator + (q63_t)noise_pk;
        q63_t gain  = ((q63_t)numerator * (q63_t)Q15_ONE) / denom;
        if (gain < 0)        gain = 0;
        if (gain > Q15_ONE)  gain = Q15_ONE;
        q15_t gain_q15 = (q15_t)gain;

        /* Apply gain to spectrum in place. */
        q15_t re_out = mul_q15(spectrum[k].re, gain_q15);
        q15_t im_out = mul_q15(spectrum[k].im, gain_q15);
        spectrum[k].re = re_out;
        spectrum[k].im = im_out;

        /* Update prev_clean_power with |Z_denoised|^2 (q63 intermediate
         * to avoid the same UB we fixed in cplx_mag_sq_q30, then
         * saturate-down to q31 in case both axes are at Q15 negative
         * full-scale post-gain). */
        q63_t mag_sq = (q63_t)re_out * re_out + (q63_t)im_out * im_out;
        s->prev_clean_power[k] = sat_q31(mag_sq);
    }
}
