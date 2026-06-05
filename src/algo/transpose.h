/* ============================================================
 * transpose.h  --  Frequency transposition with linear
 *                  interpolation, plus per-frame mix with the
 *                  clean spectrum.
 *
 * Mirrors matlab/compress.m lines 89-107.
 * ============================================================ */

#ifndef TMS320_TRANSPOSE_H
#define TMS320_TRANSPOSE_H

#include "../config.h"
#include "../util/fixed_point.h"

/* Apply frequency transposition with linear interpolation, then mix
 * with the input by `mix_ratio` (Q15).
 *
 * mix_ratio = 0          => output identical to clean
 * mix_ratio = Q15_ONE    => output is the fully-transposed spectrum
 * between                => linear crossfade per bin
 *
 * `output` may NOT alias `clean`.
 */
void transpose_apply(const q15_cplx_t clean[N_BINS],
                     q15_cplx_t       output[N_BINS],
                     q15_t            mix_ratio);

#endif /* TMS320_TRANSPOSE_H */
