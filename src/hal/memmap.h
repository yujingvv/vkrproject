/* ============================================================
 * memmap.h  --  TMS320C5515 memory region declarations
 *
 * On real silicon this header maps named regions to physical
 * memory via section attributes and the linker command file:
 *
 *   .saram  : 160 KB on-chip RAM (single-access, fastest)
 *   .daram  :  64 KB dual-access RAM (FFT HWA scratch lives here)
 *   .ext    :  external SDRAM (large, slow)
 *
 * In the PC simulation we keep the attributes as no-ops so the
 * code compiles unchanged in either environment.  The placement
 * annotations remain as documentation for the linker phase.
 * ============================================================ */

#ifndef TMS320_MEMMAP_H
#define TMS320_MEMMAP_H

#ifdef TMS320_C5515
  /* Real hardware: use GCC/TI section attributes. */
  #define MEM_SARAM      __attribute__((section(".saram")))
  #define MEM_SARAM_RO   __attribute__((section(".saram.const")))
  #define MEM_DARAM      __attribute__((section(".daram")))
  #define MEM_EXT        __attribute__((section(".ext")))
#else
  /* PC simulation: no-op, but document intent. */
  #define MEM_SARAM
  #define MEM_SARAM_RO
  #define MEM_DARAM
  #define MEM_EXT
#endif

#endif /* TMS320_MEMMAP_H */
