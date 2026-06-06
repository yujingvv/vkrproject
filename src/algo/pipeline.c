#include "pipeline.h"
#include "stft.h"
#include "wiener.h"
#include "vad_decision.h"
#include "transpose.h"
#include "../hal/fft_hwa.h"
#include "../hal/memmap.h"
#include <string.h>

static stft_state_t   g_stft   MEM_SARAM;
static wiener_state_t g_wiener MEM_SARAM;
static vad_state_t    g_vad    MEM_SARAM;

static q15_t      g_windowed[N_FFT]         MEM_SARAM;
static q15_cplx_t g_full_spec[N_FFT]        MEM_SARAM;
static q15_cplx_t g_half_spec[N_BINS]       MEM_SARAM;
static q15_cplx_t g_pipe_transposed[N_BINS] MEM_SARAM;
static q15_t      g_time_out[N_FFT]         MEM_SARAM;

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

    stft_push_hop(&g_stft, in, windowed);
    fft_hwa_forward(windowed, full_spec);
    memcpy(half_spec, full_spec, sizeof(g_half_spec));
    wiener_process(&g_wiener, half_spec);
    mix_ratio = vad_compute_mix_ratio(&g_vad, half_spec);
    transpose_apply(half_spec, transposed, mix_ratio);

    /* 用 transposed[] 重建共轭对称谱（不能用 half_spec[]，
     * 否则源频带能量会通过负频率镜像重新注入） */
    memcpy(full_spec, transposed, sizeof(g_pipe_transposed));
    for (int k = 1; k < N_FFT / 2; ++k) {
        full_spec[N_FFT - k].re =  transposed[k].re;
        full_spec[N_FFT - k].im = -transposed[k].im;
    }
    full_spec[0].im         = 0;
    full_spec[N_FFT / 2].im = 0;

    fft_hwa_inverse(full_spec, time_out);
    stft_overlap_add(&g_stft, time_out, out);
}