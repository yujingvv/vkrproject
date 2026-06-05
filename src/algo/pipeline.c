/* ============================================================
 * pipeline.c  --  Per-frame pipeline orchestrator
 *
 * Glues together STFT framing, the FFT HWA, Wiener denoise,
 * HLR-based VAD, frequency transposition, and OLA synthesis.
 *
 * Layout per HOP=256 input samples:
 *   1. stft_push_hop   -> windowed N_FFT=1024 frame
 *   2. fft_hwa_forward -> N_FFT=1024 complex bins
 *   3. extract bins 0..N_BINS-1 (half-spectrum) into a working buffer
 *   4. wiener_process  -> in-place denoise on the 513-bin buffer
 *   5. vad_compute_mix_ratio -> Q15 mix ratio
 *   6. transpose_apply -> writes a separate 513-bin transposed/mixed
 *                          spectrum
 *   7. rebuild full 1024-bin spectrum by conjugate symmetry
 *   8. fft_hwa_inverse -> N_FFT=1024 real samples
 *   9. stft_overlap_add -> emit HOP output samples
 * ============================================================ */

#include "pipeline.h"
#include "stft.h"
#include "wiener.h"
#include "vad_decision.h"
#include "transpose.h"
#include "../hal/fft_hwa.h"
#include "../hal/memmap.h"

#include <string.h>

/* Module state.  All buffers are file-scope static (not stack) so the
 * per-call stack footprint stays well under typical C5515 stack sizes
 * (~1-4 KB).  Working buffers total ~14 KB and are explicitly placed in
 * SARAM.  Pipeline is single-threaded, not re-entrant. */
static stft_state_t   g_stft   MEM_SARAM;
static wiener_state_t g_wiener MEM_SARAM;
static vad_state_t    g_vad    MEM_SARAM;

/* Working buffers, reused every frame.  See section 5/10 of the spec
 * for placement recommendations (SARAM for algorithm state, DARAM for
 * FFT scratch when HWA is in use). */
static q15_t      g_windowed[N_FFT]   MEM_SARAM;
static q15_cplx_t g_full_spec[N_FFT]  MEM_SARAM;
static q15_cplx_t g_half_spec[N_BINS] MEM_SARAM;
static q15_cplx_t g_pipe_transposed[N_BINS] MEM_SARAM;
static q15_t      g_time_out[N_FFT]   MEM_SARAM;

void pipeline_init(void) {
    fft_hwa_init();
    stft_init(&g_stft);
    wiener_init(&g_wiener);
    vad_init(&g_vad);
}

void pipeline_process(const q15_t in[HOP], q15_t out[HOP]) {
    q15_t      *windowed   = g_windowed;
    q15_cplx_t *full_spec  = g_full_spec;
    q15_cplx_t *half_spec  = g_half_spec;
    q15_cplx_t *transposed = g_pipe_transposed;
    q15_t      *time_out   = g_time_out;
    q15_t       mix_ratio;

    /* 1. window + frame from rolling input history */
    stft_push_hop(&g_stft, in, windowed);

    /* 2. forward FFT (full 1024-bin output) */
    fft_hwa_forward(windowed, full_spec);

    /* 3. extract half-spectrum (bins 0..N_BINS-1 inclusive) */
    memcpy(half_spec, full_spec, sizeof(g_half_spec));

    /* 4. Wiener denoise in-place on half-spectrum */
    wiener_process(&g_wiener, half_spec);

    /* 5. VAD: read-only on the denoised spectrum, returns Q15 mix ratio */
    mix_ratio = vad_compute_mix_ratio(&g_vad, half_spec);

    /* 6. Frequency transposition + crossfade with clean spectrum.
     *    transpose_apply forbids aliasing of clean and output, so we
     *    write into a separate buffer. */
    transpose_apply(half_spec, transposed, mix_ratio);

    /* 7. Rebuild the full 1024-bin spectrum.
     *    Positive half (bins 0..N_FFT/2): use the processed (mixed)
     *    half-spectrum so the transposition takes effect.
     *    Negative half (bins N_FFT/2+1..N_FFT-1): mirror from the
     *    UN-TRANSPOSED Wiener-cleaned half-spectrum.  This matches
     *    MATLAB compress.m which only modifies positive-frequency
     *    source/target bins; the negative-frequency mirror of
     *    Zxx_final equals clean_frame's mirror because
     *      (1-r)*clean.neg + r*clean.neg = clean.neg
     *    (transposed_frame leaves negative-freq bins untouched).
     *    Mirroring from transposed[] instead would over-attenuate the
     *    source band by ~6 dB and double-count the transposed energy. */
    memcpy(full_spec, transposed, sizeof(g_pipe_transposed));
    for (int k = 1; k < N_FFT / 2; ++k) {
        full_spec[N_FFT - k].re =  half_spec[k].re;
        full_spec[N_FFT - k].im = -half_spec[k].im;
    }
    /* DC and Nyquist must be real-valued for an all-real time-domain
     * reconstruction. The forward FFT of a real signal already has
     * these as real, but transposition can leave residuals. */
    full_spec[0].im         = 0;
    full_spec[N_FFT / 2].im = 0;

    /* 8. Inverse FFT */
    fft_hwa_inverse(full_spec, time_out);

    /* 9. Overlap-add synthesis: window + accumulate, pop HOP samples. */
    stft_overlap_add(&g_stft, time_out, out);
}
