/* ============================================================
 * stft.h  --  STFT framing + overlap-add reconstruction
 *
 * Provides a streaming front-end that converts a contiguous
 * Q15 input stream into N_FFT-point windowed frames at a
 * HOP-sample stride (75% overlap with N_FFT=1024, HOP=256),
 * and a matching back-end that windows + overlap-adds an
 * inverse-FFT frame back into a contiguous output stream.
 *
 * Spec reference: docs/specs/2026-05-14-tms320-port-design.md
 *                 section 6.2.
 * ============================================================ */

#ifndef TMS320_STFT_H
#define TMS320_STFT_H

#include "../config.h"
#include "../util/fixed_point.h"

typedef struct {
    q15_t in_history[N_FFT];   /* last N_FFT input samples (most recent at high indices) */
    q15_t ola_buf[N_FFT];      /* overlap-add accumulator */
} stft_state_t;

/* Zero all internal buffers. */
void stft_init(stft_state_t *s);

/* Accept HOP new samples; produce a windowed N_FFT-point frame
 * ready to feed to FFT.
 *   frame_out[i] = in_history_AFTER_SHIFT[i] * hamming_q15[i]   (Q15 multiply)
 * where the shift slides the buffer left by HOP and copies hop_in
 * into the last HOP slots. */
void stft_push_hop(stft_state_t *s,
                   const q15_t hop_in[HOP],
                   q15_t frame_out[N_FFT]);

/* Accept an inverse-FFT N_FFT-point frame; apply synthesis window,
 * accumulate into ola_buf, return the next HOP samples that are now
 * fully formed (the leftmost HOP samples of ola_buf).
 *   ola_buf[i] += frame_in[i] * hamming_q15[i]
 * then pop HOP samples (left-shift, zero-pad tail). */
void stft_overlap_add(stft_state_t *s,
                      const q15_t frame_in[N_FFT],
                      q15_t hop_out[HOP]);

#endif /* TMS320_STFT_H */
