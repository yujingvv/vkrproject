/* ============================================================
 * stft.c  --  STFT framing + overlap-add reconstruction
 *
 * Implementation choice: plain memmove-based shift register for
 * clarity on PC. On the actual TMS320C5515 this should become a
 * ring buffer to avoid the O(N_FFT) copy each hop.
 *
 * Fix (2026-05-24): stft_overlap_add now uses synthesis_hamming_q15[]
 * instead of hamming_q15[] for the synthesis window.  The analysis
 * window in stft_push_hop is unchanged (still hamming_q15[]).
 *
 * Background: with 75% overlap (HOP=256, N_FFT=1024) and a symmetric
 * Hamming window used for BOTH analysis and synthesis, the steady-state
 * OLA gain is
 *
 *   sum_{k=0}^{3} w[i + k*HOP]^2  =  1.5896   (+4.03 dB)
 *
 * instead of the required 1.0.  This makes the pass-through output
 * 1.59x louder than the input, which the MATLAB post-process peak-
 * normalisation hides on PC but would cause +4 dB clipping on the
 * DSP where no such normalisation is performed.
 *
 * The synthesis window w_s[n] = w_a[n] / 1.5896 restores unity gain:
 *
 *   sum_{k=0}^{3} w_a[i+k*H] * w_s[i+k*H]  =  1.0  for all i
 * ============================================================ */

#include "stft.h"
#include "../util/window_table.h"
#include "../util/synthesis_window_table.h"

#include <string.h>

void stft_init(stft_state_t *s)
{
    memset(s->in_history, 0, sizeof(s->in_history));
    memset(s->ola_buf,    0, sizeof(s->ola_buf));
}

void stft_push_hop(stft_state_t *s,
                   const q15_t hop_in[HOP],
                   q15_t frame_out[N_FFT])
{
    /* Shift left by HOP: drop the oldest HOP samples, keep N_FFT-HOP. */
    memmove(&s->in_history[0],
            &s->in_history[HOP],
            (N_FFT - HOP) * sizeof(q15_t));

    /* Append the new HOP samples at the tail. */
    memcpy(&s->in_history[N_FFT - HOP],
           hop_in,
           HOP * sizeof(q15_t));

    /* Apply analysis window: frame_out[i] = in_history[i] * w_a[i].
     * Analysis window is unchanged: hamming_q15[]. */
    for (int i = 0; i < N_FFT; ++i) {
        frame_out[i] = mul_q15(s->in_history[i], hamming_q15[i]);
    }
}

void stft_overlap_add(stft_state_t *s,
                      const q15_t frame_in[N_FFT],
                      q15_t hop_out[HOP])
{
    /* Window the inverse-FFT frame with the SYNTHESIS window and
     * accumulate into the OLA buffer.
     *
     * synthesis_hamming_q15[i] = hamming_q15[i] / 1.5896 (Q15).
     * With this choice, the steady-state sum of w_a * w_s over the
     * four overlapping frames equals exactly 1.0, giving unity
     * pass-through gain (verified to < 0.004% error in Q15).
     *
     * Accumulation uses q31_t to safely hold two Q15 values before
     * saturation back to Q15. */
    for (int i = 0; i < N_FFT; ++i) {
        q15_t w_in = mul_q15(frame_in[i], synthesis_hamming_q15[i]);
        q31_t acc  = (q31_t)s->ola_buf[i] + (q31_t)w_in;
        s->ola_buf[i] = sat_q15(acc);
    }

    /* Pop the leftmost HOP samples -- they are fully formed now. */
    memcpy(hop_out, &s->ola_buf[0], HOP * sizeof(q15_t));

    /* Shift the remaining N_FFT-HOP samples left by HOP. */
    memmove(&s->ola_buf[0],
            &s->ola_buf[HOP],
            (N_FFT - HOP) * sizeof(q15_t));

    /* Zero-pad the tail HOP slots so future accumulation starts clean. */
    memset(&s->ola_buf[N_FFT - HOP], 0, HOP * sizeof(q15_t));
}
