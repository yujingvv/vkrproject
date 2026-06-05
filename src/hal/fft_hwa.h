/* ============================================================
 * fft_hwa.h  --  TMS320C5515 hardware FFT accelerator stub
 *
 * Simulates the C5515 HWA forward / inverse 1024-pt FFT on a
 * PC host.  Uses a radix-2 decimation-in-time algorithm with a
 * per-stage scale-by-2 (right shift by 1) on the forward path
 * to match the on-chip block-floating-point behaviour.
 *
 * Forward FFT scaling:
 *   X[k] ~= numpy.fft.fft(x)[k] / N_FFT
 * (i.e. the forward transform is divided by N due to the 10
 * per-stage >>1 operations: 2^10 == N_FFT == 1024).
 *
 * Inverse FFT does NOT apply the per-stage >>1 scaling, so
 * inverse(forward(x)) ~= x (with at most ~1 LSB attenuation
 * from forward-path rounding).
 * ============================================================ */

#ifndef TMS320_FFT_HWA_H
#define TMS320_FFT_HWA_H

#include "../config.h"
#include "../util/fixed_point.h"
#include <stdint.h>

/* Initialise the HWA stub (currently a no-op; kept for API
 * symmetry with the on-chip driver). */
void     fft_hwa_init(void);

/* Forward FFT: N_FFT real Q15 samples in, N_FFT Q15-complex bins out
 * (full spectrum, conjugate-symmetric for real input).
 * Internally each radix-2 stage shifts results right by 1 (per-stage
 * scale-by-2 to prevent overflow, matching C5515 HWA). Net result is
 * X[k] approximately numpy.fft.fft(x)[k] / N. */
void     fft_hwa_forward(const q15_t time_in[N_FFT], q15_cplx_t freq_out[N_FFT]);

/* Inverse FFT: N_FFT Q15 complex bins in, N_FFT real Q15 samples out.
 * Uses the same twiddle table, conjugating the imag part on the fly. */
void     fft_hwa_inverse(const q15_cplx_t freq_in[N_FFT], q15_t time_out[N_FFT]);

/* Estimated cycle count of the last FFT call (constant for now). */
uint32_t fft_hwa_last_cycles(void);

#endif /* TMS320_FFT_HWA_H */
