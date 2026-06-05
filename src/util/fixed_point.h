/* ============================================================
 * fixed_point.h  --  Q15 / Q31 helpers
 *
 * Mirrors the conventions of TI's IQmath / DSPLIB:
 *   Q15  : int16_t in [-1.0, 1.0)   = value / 32768
 *   Q31  : int32_t in [-1.0, 1.0)   = value / 2^31
 * ============================================================ */

#ifndef TMS320_FIXED_POINT_H
#define TMS320_FIXED_POINT_H

#include <stdint.h>

typedef int16_t q15_t;
typedef int32_t q31_t;
typedef int64_t q63_t;

typedef struct {
    q15_t re;
    q15_t im;
} q15_cplx_t;

#define Q15_ONE      ((q15_t)32767)
#define Q15_NEG_ONE  ((q15_t)-32768)
#define Q15_HALF     ((q15_t)16384)

#define Q31_ONE      ((q31_t)0x7FFFFFFF)

/* ---- Saturation ---- */
static inline q15_t sat_q15(q31_t x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return (q15_t)x;
}

static inline q31_t sat_q31(q63_t x) {
    if (x >  0x7FFFFFFFLL) return  0x7FFFFFFF;
    if (x < -0x80000000LL) return (q31_t)0x80000000;
    return (q31_t)x;
}

/* ---- Q15 multiply ----
 * (a * b) interpreted as Q15 * Q15 -> Q30, shifted back to Q15. */
static inline q15_t mul_q15(q15_t a, q15_t b) {
    return (q15_t)(((q31_t)a * (q31_t)b) >> 15);
}

/* Q15 * Q15 -> Q15 with rounding before shift. */
static inline q15_t mul_q15_round(q15_t a, q15_t b) {
    q31_t p = (q31_t)a * (q31_t)b;
    return (q15_t)((p + (1 << 14)) >> 15);
}

/* Q15 * Q15 -> Q30 (no shift), useful for accumulating energy. */
static inline q31_t mul_q15_to_q30(q15_t a, q15_t b) {
    return (q31_t)a * (q31_t)b;
}

/* Magnitude squared of a Q15 complex number, result in Q30.
 * Promote the intermediate sum to q63 to avoid signed-int32 overflow
 * UB when both re and im equal -32768: re*re + im*im = 2^31 which is
 * one past INT32_MAX.  The actual maximum (2 * 32767^2 = 2*2^30 - eps)
 * still fits in q31_t so the truncated return is safe. */
static inline q31_t cplx_mag_sq_q30(q15_cplx_t c) {
    q63_t sum = (q63_t)c.re * c.re + (q63_t)c.im * c.im;
    return (q31_t)sum;
}

/* ---- Q31 helpers ---- */
static inline q31_t mul_q31(q31_t a, q31_t b) {
    return (q31_t)(((q63_t)a * (q63_t)b) >> 31);
}

/* Q31 * Q15 -> Q31 */
static inline q31_t mul_q31_q15(q31_t a, q15_t b) {
    return (q31_t)(((q63_t)a * (q63_t)b) >> 15);
}

/* ---- Complex Q15 multiply ----
 * (a.re + j a.im) * (b.re + j b.im) with per-stage scale by 1/2
 * to prevent overflow in radix-2 FFT butterflies. */
static inline q15_cplx_t cmul_q15(q15_cplx_t a, q15_cplx_t b) {
    q15_cplx_t out;
    /* re = a.re*b.re - a.im*b.im, then >> 15 to return to Q15 */
    out.re = (q15_t)(((q31_t)a.re * b.re - (q31_t)a.im * b.im) >> 15);
    out.im = (q15_t)(((q31_t)a.re * b.im + (q31_t)a.im * b.re) >> 15);
    return out;
}

/* ---- Float <-> Q15 (debug / table generation only; not used on chip) ---- */
static inline q15_t float_to_q15(float x) {
    float y = x * 32768.0f;
    if (y >  32767.0f) y =  32767.0f;
    if (y < -32768.0f) y = -32768.0f;
    return (q15_t)(y >= 0 ? (y + 0.5f) : (y - 0.5f));
}

static inline float q15_to_float(q15_t x) {
    return (float)x / 32768.0f;
}

#endif /* TMS320_FIXED_POINT_H */
