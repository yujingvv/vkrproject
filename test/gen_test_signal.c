/*
 * gen_test_signal.c
 *
 * Standalone synthetic test signal generator for the TMS320C5515
 * hearing-aid DSP port. Emits a 3-second 16 kHz mono 16-bit PCM WAV
 * containing:
 *   - 0.0-0.2 s : low-level white noise (Wiener noise-estimation phase)
 *   - 0.2-1.5 s : speech-like formant content (200/500/800 Hz)
 *   - 1.5-2.5 s : same formants + 5-7 kHz chirp (HLR/transposition trigger)
 *   - 2.5-3.0 s : near-silence + light noise floor
 *
 * Build : gcc -std=c99 -O2 test/gen_test_signal.c -o gen_test_signal -lm
 * Usage : ./gen_test_signal output.wav
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE   16000
#define DURATION_SEC  3.0
#define TOTAL_SAMPLES ((int)(SAMPLE_RATE * DURATION_SEC))

/* Simple LCG for reproducible "white noise". */
static uint32_t lcg_state = 0xC0FFEEu;
static double frand_uniform(void)
{
    /* Numerical Recipes LCG. */
    lcg_state = lcg_state * 1664525u + 1013904223u;
    /* Map to [-1, 1). */
    return ((double)lcg_state / (double)0xFFFFFFFFu) * 2.0 - 1.0;
}

static int16_t clamp_i16(double x)
{
    if (x >  32767.0) return  32767;
    if (x < -32768.0) return -32768;
    return (int16_t)lrint(x);
}

/* Write 32-bit little-endian unsigned. */
static void put_u32_le(FILE *f, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)( v        & 0xFF);
    b[1] = (uint8_t)((v >>  8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, f);
}

/* Write 16-bit little-endian unsigned. */
static void put_u16_le(FILE *f, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)( v        & 0xFF);
    b[1] = (uint8_t)((v >>  8) & 0xFF);
    fwrite(b, 1, 2, f);
}

static void write_wav_header(FILE *f, uint32_t num_samples,
                             uint32_t sample_rate, uint16_t channels,
                             uint16_t bits_per_sample)
{
    uint32_t byte_rate    = sample_rate * channels * (bits_per_sample / 8);
    uint16_t block_align  = (uint16_t)(channels * (bits_per_sample / 8));
    uint32_t data_bytes   = num_samples * channels * (bits_per_sample / 8);
    uint32_t riff_size    = 36 + data_bytes;

    fwrite("RIFF", 1, 4, f);
    put_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    put_u32_le(f, 16);            /* PCM fmt chunk size */
    put_u16_le(f, 1);             /* PCM format = 1 */
    put_u16_le(f, channels);
    put_u32_le(f, sample_rate);
    put_u32_le(f, byte_rate);
    put_u16_le(f, block_align);
    put_u16_le(f, bits_per_sample);

    fwrite("data", 1, 4, f);
    put_u32_le(f, data_bytes);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s output.wav\n", argv[0]);
        return 1;
    }
    const char *out_path = argv[1];

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "[gen_test_signal] cannot open %s for writing\n",
                out_path);
        return 1;
    }

    write_wav_header(f, (uint32_t)TOTAL_SAMPLES, SAMPLE_RATE, 1, 16);

    const double dt = 1.0 / (double)SAMPLE_RATE;

    /* Formant frequencies and amplitudes. */
    const double f1 = 200.0,  a1 = 6000.0;
    const double f2 = 500.0,  a2 = 4000.0;
    const double f3 = 800.0,  a3 = 3000.0;

    /* Chirp state (phase integrator). */
    double chirp_phase = 0.0;

    /* Noise amplitudes (int16) for each segment. */
    const double n_init   = 1000.0; /* ~-30 dBFS */
    const double n_speech =  600.0; /* ~-35 dBFS */
    const double n_tail   =  300.0;

    for (int n = 0; n < TOTAL_SAMPLES; ++n) {
        double t = (double)n * dt;
        double s = 0.0;

        if (t < 0.2) {
            /* Wiener noise-estimation phase: white noise only. */
            s = n_init * frand_uniform();
        } else if (t < 1.5) {
            /* Speech-like content: formants + slow envelope + noise. */
            double env = sin(2.0 * M_PI * 2.0 * t) * 0.5 + 0.5;
            double formants =
                  a1 * cos(2.0 * M_PI * f1 * t)
                + a2 * cos(2.0 * M_PI * f2 * t)
                + a3 * cos(2.0 * M_PI * f3 * t);
            s = env * formants + n_speech * frand_uniform();
        } else if (t < 2.5) {
            /* Formants + high-frequency chirp (5 kHz -> 7 kHz). */
            double env = sin(2.0 * M_PI * 2.0 * t) * 0.5 + 0.5;
            double formants =
                  a1 * cos(2.0 * M_PI * f1 * t)
                + a2 * cos(2.0 * M_PI * f2 * t)
                + a3 * cos(2.0 * M_PI * f3 * t);

            double f_inst = 5000.0 + 2000.0 * (t - 1.5);
            chirp_phase += 2.0 * M_PI * f_inst * dt;
            double chirp = 5000.0 * sin(chirp_phase);

            s = env * formants + chirp + n_speech * frand_uniform();
        } else {
            /* Silence + light noise floor. */
            s = n_tail * frand_uniform();
        }

        int16_t sample = clamp_i16(s);
        put_u16_le(f, (uint16_t)sample);
    }

    fclose(f);

    fprintf(stderr,
            "[gen_test_signal] wrote %d samples (%.3f seconds) to %s\n",
            TOTAL_SAMPLES, (double)TOTAL_SAMPLES / (double)SAMPLE_RATE,
            out_path);
    return 0;
}
