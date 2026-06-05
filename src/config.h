/* ============================================================
 * config.h  --  All tunable parameters from matlab/compress.m
 *
 * This file is the single source of truth for algorithm
 * constants. Any change here propagates to every module.
 * ============================================================ */

#ifndef TMS320_CONFIG_H
#define TMS320_CONFIG_H

#include <stdint.h>

/* ---- STFT parameters ---- */
#define N_FFT            1024
#define HOP              256          /* 75% overlap                       */
#define N_BINS           513          /* N_FFT/2 + 1, half-spectrum         */
#define LOG2_N_FFT       10           /* needed by radix-2 FFT loop         */

/* ---- Audio format ---- */
#define FS               16000        /* Hz, mono                           */
#define BITS_PER_SAMPLE  16

/* ---- Frequency-transposition bands (Hz) ----
 * Source band is the high-frequency region the user cannot hear;
 * energy here is compressed and mapped into the target band. */
#define SOURCE_FREQ_LO   4000
#define SOURCE_FREQ_HI   8000
#define TARGET_FREQ_LO   2000
#define TARGET_FREQ_HI   4000

/* gamma = target_bw / source_bw = 2000/4000 = 0.5
 * Stored as a rational to avoid Q15 round trip. */
#define GAMMA_NUM        1
#define GAMMA_DEN        2

/* ---- High/Low-band Energy Ratio (HLR) decision band (Hz) ---- */
#define HLR_LOW_LO       1000
#define HLR_LOW_HI       3000

/* ---- Wiener filter parameters ---- */
#define NOISE_PROFILE_MS    200
#define WIENER_ALPHA_Q15    32113     /* 0.98 in Q15                        */

/* ---- VAD / crossfade ---- */
#define HLR_THRESHOLD_Q15   16384     /* 0.5 in Q15                         */
#define TRANSITION_LEN_MS   32

/* ---- Derived constants (compile-time) ---- */
/* Number of frames used for noise profile.
 * ceil(200 * 16000 / (1000 * 256)) = ceil(12.5) = 13 */
#define NOISE_PROFILE_FRAMES   13

/* round(32 * 16000 / (1000 * 256)) = round(2.0) = 2 */
#define TRANSITION_FRAMES      2

/* Frequency-to-bin conversion: bin = round(freq * N_FFT / FS).
 * With FS=16000, N_FFT=1024, the bin width is 15.625 Hz. */
#define FREQ_TO_BIN(hz)        (((hz) * N_FFT + FS/2) / FS)

#define SOURCE_LO_BIN          FREQ_TO_BIN(SOURCE_FREQ_LO)   /* 256 */
#define SOURCE_HI_BIN          FREQ_TO_BIN(SOURCE_FREQ_HI)   /* 512 */
#define TARGET_LO_BIN          FREQ_TO_BIN(TARGET_FREQ_LO)   /* 128 */
#define TARGET_HI_BIN          FREQ_TO_BIN(TARGET_FREQ_HI)   /* 256 */
#define HLR_LOW_LO_BIN         FREQ_TO_BIN(HLR_LOW_LO)       /*  64 */
#define HLR_LOW_HI_BIN         FREQ_TO_BIN(HLR_LOW_HI)       /* 192 */

#endif /* TMS320_CONFIG_H */
