/* ============================================================
 * pipeline.h  --  Per-frame pipeline orchestrator
 *
 * Chains STFT framing -> forward FFT -> Wiener denoise -> VAD
 * mix-ratio -> frequency transposition -> inverse FFT -> OLA
 * synthesis into a single streaming HOP-in / HOP-out call.
 *
 * Spec reference: docs/specs/2026-05-14-tms320-port-design.md
 *                 sections 6.6 and 7.
 * ============================================================ */

#ifndef TMS320_PIPELINE_H
#define TMS320_PIPELINE_H

#include "../config.h"
#include "../util/fixed_point.h"

/* Initialize all internal state. Call once before the first
 * pipeline_process call. Safe to call multiple times (resets state). */
void pipeline_init(void);

/* Process one HOP=256-sample input frame, produce one HOP-sample output
 * frame. The pipeline introduces inherent latency of (N_FFT/HOP - 1)
 * frames = 3 frames before steady-state output appears. */
void pipeline_process(const q15_t in[HOP], q15_t out[HOP]);

#endif /* TMS320_PIPELINE_H */
