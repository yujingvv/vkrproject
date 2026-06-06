#ifndef BSL_AUDIO_IO_H
#define BSL_AUDIO_IO_H

#include "../../config.h"
#include "../../util/fixed_point.h"

extern q15_t        g_dma_in [HOP * 2];
extern q15_t        g_dma_out[HOP * 2];
extern volatile int g_audio_half;

int  audio_io_init(void);
void audio_io_start(void);

#endif