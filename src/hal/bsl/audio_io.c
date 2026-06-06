#ifdef TMS320_C5515

#include "audio_io.h"
#include "../../algo/pipeline.h"
#include "../../hal/memmap.h"
#include <csl_i2s.h>
#include <csl_dma.h>
#include <csl_intc.h>
#include <csl_sysctrl.h>
#include <csl_types.h>

#pragma DATA_SECTION(g_dma_in,  ".daram")
#pragma DATA_SECTION(g_dma_out, ".daram")
q15_t g_dma_in [HOP * 2];
q15_t g_dma_out[HOP * 2];
volatile int g_audio_half = 0;

static CSL_I2sHandle  hI2s   = NULL;
static CSL_DMA_Handle hDmaRx = NULL;
static CSL_DMA_Handle hDmaTx = NULL;

int audio_io_init(void) {
    CSL_I2sConfig i2sCfg; CSL_DMA_Config dmaCfg; CSL_Status status;

    hI2s = I2S_open(I2S_INSTANCE0, DMA_PING_PONG, I2S_CHAN_MONO);
    if (!hI2s) return -1;
    i2sCfg.datatype=I2S_DATATYPE_16BIT; i2sCfg.loopBack=I2S_LOOPBACK_DISABLE;
    i2sCfg.rpack=I2S_RPACK_DISABLE;     i2sCfg.rlsb=I2S_LSB_MSB;
    i2sCfg.wsize=I2S_WORDLENGTH_16;     i2sCfg.sclkDiv=97u;
    i2sCfg.fsDiv=0u;                    i2sCfg.clkSrc=I2S_CLK_INTERNAL;
    i2sCfg.dataDelay=I2S_DATADELAY_ONEBIT; i2sCfg.chanDir=I2S_CHAN_BIDIR;
    if (I2S_setup(hI2s, &i2sCfg) != CSL_SOK) return -1;

    hDmaRx = DMA_open(CSL_DMA_CHAN4, &status);
    if (!hDmaRx || status != CSL_SOK) return -1;
    dmaCfg.pingPongMode=CSL_DMA_PING_PONG_ENABLE;
    dmaCfg.autoMode=CSL_DMA_AUTORELOAD_ENABLE;
    dmaCfg.burstLen=CSL_DMA_TXBURST_1WORD;
    dmaCfg.trigger=CSL_DMA_EVENT_TRIGGER;
    dmaCfg.dmaEvt=CSL_DMA_EVT_I2S0_RX;
    dmaCfg.dmaInt=CSL_DMA_INTERRUPT_ENABLE;
    dmaCfg.chanDir=CSL_DMA_READ;
    dmaCfg.trfType=CSL_DMA_TRANSFER_IO_MEMORY;
    dmaCfg.dataLen=(Uint32)(HOP * sizeof(q15_t));
    dmaCfg.srcAddr=(Uint32)(&CSL_I2S0_REGS->I2SRXLT0);
    dmaCfg.destAddr=(Uint32)g_dma_in;
    if (DMA_config(hDmaRx, &dmaCfg) != CSL_SOK) return -1;

    hDmaTx = DMA_open(CSL_DMA_CHAN5, &status);
    if (!hDmaTx || status != CSL_SOK) return -1;
    dmaCfg.dmaEvt=CSL_DMA_EVT_I2S0_TX;
    dmaCfg.dmaInt=CSL_DMA_INTERRUPT_DISABLE;
    dmaCfg.chanDir=CSL_DMA_WRITE;
    dmaCfg.srcAddr=(Uint32)g_dma_out;
    dmaCfg.destAddr=(Uint32)(&CSL_I2S0_REGS->I2STXLT0);
    if (DMA_config(hDmaTx, &dmaCfg) != CSL_SOK) return -1;

    IRQ_globalDisable(); IRQ_clearAll();
    IRQ_enable(INTERRUPT_DMA); IRQ_globalEnable();
    return 0;
}

void audio_io_start(void) {
    DMA_start(hDmaRx); DMA_start(hDmaTx);
    I2S_transEnable(hI2s, TRUE);
}

interrupt void audio_dma_isr(void) {
    const q15_t *in  = &g_dma_in [g_audio_half * HOP];
    q15_t       *out = &g_dma_out[g_audio_half * HOP];
    pipeline_process(in, out);
    g_audio_half ^= 1;
    DMA_clearInterrupt(hDmaRx);
    IRQ_clear(INTERRUPT_DMA);
}

#endif /* TMS320_C5515 */