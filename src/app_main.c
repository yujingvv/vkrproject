/* ============================================================
 * app_main.c  --  On-chip entry point (TMS320C5515)
 *
 * This is the production-side counterpart to main_pc.c.  It is a
 * STUB / TEMPLATE: most of the symbols it references (CSL drivers,
 * BSL audio codec init, ISR vectors) only exist when building with
 * the TI toolchain against the C5515 BSL/CSL libraries.  This file
 * is excluded from the host build (the Makefile only compiles it
 * when TMS320_C5515 is defined).
 *
 * Architecture
 * ------------
 *   AIC3204 codec  --(I2S/McBSP)--> 16-bit samples arrive in DMA
 *   buffer (ping-pong, HOP samples per half).  When the DMA "buffer
 *   half complete" interrupt fires, the ISR pushes one HOP into the
 *   pipeline, gets a HOP of output, and DMA streams it back out to
 *   the codec.
 *
 *   No file I/O, no calloc, no rewinding.  All buffers are static
 *   in named memory regions (see memmap.h).
 *
 * Real-time budget
 * ----------------
 *   FS=16000, HOP=256 -> one interrupt every 16 ms.
 *   CPU at 100 MHz -> 1.6M cycles available per hop.
 *
 *   With the HWA FFT in use, the two FFT calls contribute 2*4150
 *   = 8300 cycles; Wiener + VAD + transpose + STFT bookkeeping add
 *   maybe 7-10K cycles depending on optimisation -- call it
 *   ~15-20K cycles/hop end-to-end (~1-1.3% load).  Confirm during
 *   profiling.  Without HWA the software FFT fallback dominates:
 *   2*70K = 140K cycles for FFTs, total ~150K cycles/hop, ~9%
 *   load -- still comfortably real-time.
 *
 * Bring-up checklist (do this in CCS)
 * -----------------------------------
 *   1. Add this file to the CCS project.
 *   2. Configure linker .cmd to honour the .saram/.daram/.ext
 *      sections used by memmap.h.
 *   3. Replace src/hal/fft_hwa.c's software implementation with a
 *      CSL FFT_run() wrapper for the on-chip HWA.  No other module
 *      should change.
 *   4. Stub the BSL/CSL prototypes below with the real library
 *      headers (csl_audioClass.h, csl_dma.h, csl_intc.h, etc.).
 *   5. Hook the audio_isr() to the McBSP/DMA half-buffer interrupt.
 *
 * ============================================================ */

#ifdef TMS320_C5515

#include "config.h"
#include "util/fixed_point.h"
#include "algo/pipeline.h"

/* ---- BSL/CSL stub declarations (replace with real headers) ---- */
extern void  audio_codec_init(uint32_t sample_rate);
extern void  dma_audio_start(q15_t *in_buf, q15_t *out_buf, int half_size);
extern void  intc_enable(int irq_id);
extern void  cpu_idle_until_irq(void);

/* ---- Ping-pong audio buffers (DMA-accessible DARAM) ----------- */
#include "hal/memmap.h"
static q15_t g_dma_in[HOP * 2]  MEM_DARAM;   /* ping-pong          */
static q15_t g_dma_out[HOP * 2] MEM_DARAM;
static volatile int g_active_half = 0;        /* 0 or 1            */

/* Interrupt handler invoked by the DMA "half-buffer complete" IRQ.
 * Reads the half that just finished filling on the input side,
 * pushes it through the pipeline, writes into the matching half on
 * the output side.  Keep this lean: cycle budget is hard. */
void audio_isr(void)
{
    const q15_t *in_hop  = &g_dma_in [g_active_half * HOP];
    q15_t       *out_hop = &g_dma_out[g_active_half * HOP];
    pipeline_process(in_hop, out_hop);
    g_active_half ^= 1;
}

/* ---- main ---- */
int main(void)
{
    pipeline_init();
    audio_codec_init(FS);
    dma_audio_start(g_dma_in, g_dma_out, HOP);
    intc_enable(/* audio_isr IRQ id */ 0);

    /* Background loop: the work happens in audio_isr.  Idle here
     * lets the CPU enter a low-power state until the next IRQ. */
    for (;;) {
        cpu_idle_until_irq();
    }
}

#endif /* TMS320_C5515 */
