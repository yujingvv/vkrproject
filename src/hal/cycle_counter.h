/* ============================================================
 * cycle_counter.h  --  Estimated cycle accounting
 *
 * The PC simulation cannot give real cycle counts but it can
 * accumulate published per-operation estimates from the C5515
 * data sheet.  Each module reports its own cost; the counter
 * runs as a single global accumulator that main.c prints at
 * the end of a run as a real-time-load budget.
 *
 * Reference values (TMS320C5515 data sheet, fclk = 100 MHz):
 *   FFT HWA 1024-pt           ~  4150 cycles  (hardware accelerator)
 *   FFT HWA   512-pt          ~  2200 cycles
 *   Vector mul N taps         ~ N cycles (single MAC)
 *   Hardware divider 32/32    ~ 36 cycles
 *
 * Important caveat
 * ----------------
 * CYC_FFT_HWA_1024 is the *hardware accelerator* cycle count.  The
 * software FFT implemented in fft_hwa.c on PC takes roughly an order
 * of magnitude more cycles (~70K) when run on real silicon without
 * the accelerator.  When you replace the stub with a CSL FFT_run()
 * call on chip, the data-sheet HWA number applies.  Until then, the
 * total reported by cycles_total() represents the *theoretical* load
 * assuming the HWA is in use, NOT the load of the bare software
 * fallback.
 *
 * To inspect the software fallback's realistic load on chip, use
 * CYC_FFT_SW_1024 instead.
 * ============================================================ */

#ifndef TMS320_CYCLE_COUNTER_H
#define TMS320_CYCLE_COUNTER_H

#include <stdint.h>

void     cycles_reset(void);
void     cycles_add(uint32_t n);
uint64_t cycles_total(void);

/* Published estimates from the C5515 data sheet (see header doc). */
#define CYC_FFT_HWA_1024     4150u
#define CYC_FFT_HWA_512      2200u

/* Software radix-2 FFT on C5500 core, rough estimate per FFT call.
 * Used when the HWA is NOT present in the build (when fft_hwa.c is
 * the active implementation rather than a CSL wrapper). */
#define CYC_FFT_SW_1024     70000u
#define CYC_FFT_SW_512      30000u

/* Use the HWA constant on hardware builds, the SW estimate otherwise.
 * Switch by defining TMS320_C5515 when targeting the chip. */
#ifdef TMS320_C5515
  #define CYC_FFT_1024  CYC_FFT_HWA_1024
  #define CYC_FFT_512   CYC_FFT_HWA_512
#else
  #define CYC_FFT_1024  CYC_FFT_SW_1024
  #define CYC_FFT_512   CYC_FFT_SW_512
#endif

#endif /* TMS320_CYCLE_COUNTER_H */
