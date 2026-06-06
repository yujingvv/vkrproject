#include "fft_hwa.h"
#include "cycle_counter.h"
#include "memmap.h"
#include <string.h>

#ifdef TMS320_C5515

#include <hwafft.h>

#ifndef HWAFFT_SCALE
#define HWAFFT_SCALE (1u << 1)
#endif
#ifndef HWAFFT_BREV
#define HWAFFT_BREV  (1u << 2)
#endif
#ifndef HWAFFT_IFFT
#define HWAFFT_IFFT  (1u << 0)
#endif
#define HWAFFT_FORWARD (HWAFFT_SCALE | HWAFFT_BREV)

static q15_cplx_t g_hwa_work   [N_FFT] MEM_DARAM;
static q15_cplx_t g_hwa_scratch[N_FFT] MEM_DARAM;
static uint32_t   g_last_cycles = 0;

void fft_hwa_init(void) { g_last_cycles = 0; }

void fft_hwa_forward(const q15_t time_in[N_FFT], q15_cplx_t freq_out[N_FFT]) {
    int i; DATA *result;
    for (i = 0; i < N_FFT; ++i) { g_hwa_work[i].re = time_in[i]; g_hwa_work[i].im = 0; }
    result = hwafft_1024pt((DATA *)g_hwa_work, (DATA *)g_hwa_scratch, HWAFFT_FORWARD);
    if ((q15_cplx_t *)result != freq_out) memcpy(freq_out, result, sizeof(g_hwa_work));
    g_last_cycles = CYC_FFT_1024; cycles_add(CYC_FFT_1024);
}

void fft_hwa_inverse(const q15_cplx_t freq_in[N_FFT], q15_t time_out[N_FFT]) {
    int i; DATA *result;
    for (i = 0; i < N_FFT; ++i) {
        g_hwa_work[i].re = freq_in[i].re;
        g_hwa_work[i].im = (freq_in[i].im == (q15_t)-32768) ? (q15_t)32767 : (q15_t)(-freq_in[i].im);
    }
    result = hwafft_1024pt((DATA *)g_hwa_work, (DATA *)g_hwa_scratch, HWAFFT_FORWARD);
    { const q15_cplx_t *r = (const q15_cplx_t *)result; for (i = 0; i < N_FFT; ++i) time_out[i] = r[i].re; }
    g_last_cycles = CYC_FFT_1024; cycles_add(CYC_FFT_1024);
}

uint32_t fft_hwa_last_cycles(void) { return g_last_cycles; }

#else /* PC 软件仿真 fallback */

#include "../util/twiddle_table.h"

static uint32_t g_last_cycles = 0;
static uint16_t g_brev[N_FFT] MEM_SARAM;
static int      g_brev_initialised = 0;

static void brev_init(void) {
    int i;
    for (i = 0; i < N_FFT; ++i) {
        uint32_t x=(uint32_t)i, r=0; int b;
        for (b=0; b<LOG2_N_FFT; ++b) { r=(r<<1)|(x&1u); x>>=1; }
        g_brev[i] = (uint16_t)r;
    }
    g_brev_initialised = 1;
}

void fft_hwa_init(void) { if (!g_brev_initialised) brev_init(); g_last_cycles = 0; }

static void fft_forward_inplace(q15_cplx_t buf[N_FFT]) {
    int s;
    for (s=1; s<=LOG2_N_FFT; ++s) {
        const int m=1<<s, half=m>>1, wstride=N_FFT/m; int k;
        for (k=0; k<N_FFT; k+=m) {
            int j;
            for (j=0; j<half; ++j) {
                const q15_t w_re=twiddle_re_q15[j*wstride], w_im=twiddle_im_q15[j*wstride];
                const q15_cplx_t a=buf[k+j], b=buf[k+j+half];
                const q31_t pre=(q31_t)w_re*b.re-(q31_t)w_im*b.im;
                const q31_t pim=(q31_t)w_re*b.im+(q31_t)w_im*b.re;
                const q31_t tre=(pre+(1<<14))>>15, tim=(pim+(1<<14))>>15;
                buf[k+j     ].re=sat_q15(((q31_t)a.re+tre)>>1);
                buf[k+j     ].im=sat_q15(((q31_t)a.im+tim)>>1);
                buf[k+j+half].re=sat_q15(((q31_t)a.re-tre)>>1);
                buf[k+j+half].im=sat_q15(((q31_t)a.im-tim)>>1);
            }
        }
    }
}

static void fft_inverse_inplace(q15_cplx_t buf[N_FFT]) {
    int i;
    for (i=0; i<N_FFT; ++i) {
        if (buf[i].im==(q15_t)-32768) buf[i].im=32767;
        else buf[i].im=(q15_t)(-buf[i].im);
    }
    static q31_t re[N_FFT] MEM_DARAM, im[N_FFT] MEM_DARAM;
    for (i=0; i<N_FFT; ++i) { re[i]=buf[i].re; im[i]=buf[i].im; }
    int s;
    for (s=1; s<=LOG2_N_FFT; ++s) {
        const int m=1<<s, half=m>>1, wstride=N_FFT/m; int k;
        for (k=0; k<N_FFT; k+=m) {
            int j;
            for (j=0; j<half; ++j) {
                const q15_t w_re=twiddle_re_q15[j*wstride], w_im=twiddle_im_q15[j*wstride];
                const q31_t a_re=re[k+j], a_im=im[k+j], b_re=re[k+j+half], b_im=im[k+j+half];
                const q63_t pre=(q63_t)w_re*b_re-(q63_t)w_im*b_im;
                const q63_t pim=(q63_t)w_re*b_im+(q63_t)w_im*b_re;
                const q31_t tre=(q31_t)((pre+(1LL<<14))>>15), tim=(q31_t)((pim+(1LL<<14))>>15);
                re[k+j]=a_re+tre; im[k+j]=a_im+tim;
                re[k+j+half]=a_re-tre; im[k+j+half]=a_im-tim;
            }
        }
    }
    for (i=0; i<N_FFT; ++i) { buf[i].re=sat_q15(re[i]); buf[i].im=sat_q15(-im[i]); }
}

void fft_hwa_forward(const q15_t time_in[N_FFT], q15_cplx_t freq_out[N_FFT]) {
    static q15_cplx_t work[N_FFT] MEM_DARAM; int i;
    if (!g_brev_initialised) brev_init();
    for (i=0; i<N_FFT; ++i) { work[g_brev[i]].re=time_in[i]; work[g_brev[i]].im=0; }
    fft_forward_inplace(work);
    memcpy(freq_out, work, sizeof(work));
    g_last_cycles=CYC_FFT_1024; cycles_add(CYC_FFT_1024);
}

void fft_hwa_inverse(const q15_cplx_t freq_in[N_FFT], q15_t time_out[N_FFT]) {
    static q15_cplx_t work[N_FFT] MEM_DARAM; int i;
    if (!g_brev_initialised) brev_init();
    for (i=0; i<N_FFT; ++i) work[g_brev[i]]=freq_in[i];
    fft_inverse_inplace(work);
    for (i=0; i<N_FFT; ++i) time_out[i]=work[i].re;
    g_last_cycles=CYC_FFT_1024; cycles_add(CYC_FFT_1024);
}

uint32_t fft_hwa_last_cycles(void) { return g_last_cycles; }

#endif /* TMS320_C5515 */