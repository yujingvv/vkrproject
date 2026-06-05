/* ============================================================
 * test_wav.c  --  Unit tests for util/wav_io
 *
 * Build:
 *   gcc -std=c99 -O2 -Isrc test/test_wav.c src/util/wav_io.c \
 *       -o /tmp/test_wav
 *
 * Tests:
 *   1. Write 1000 samples of a 440 Hz sinusoid to /tmp/twav.wav.
 *   2. Re-open and read; verify sample-by-sample exact match.
 *   3. Verify reader reports 16000 Hz, mono, 16-bit, 1000 samples.
 * ============================================================ */

#include "config.h"
#include "util/wav_io.h"
#include "util/fixed_point.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_SAMPS 1000

static int test_roundtrip(void) {
    const char *path = "/tmp/twav.wav";
    q15_t       gold[N_SAMPS];
    q15_t       readback[N_SAMPS];
    int         failures = 0;

    /* Generate the reference waveform. */
    for (int n = 0; n < N_SAMPS; ++n) {
        double s = 8000.0 * sin(2.0 * 3.14159265358979323846 * 440.0 * n / 16000.0);
        long   v = (long)(s >= 0 ? s + 0.5 : s - 0.5);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        gold[n] = (q15_t)v;
    }

    /* Write. */
    wav_writer_t w;
    if (wav_write_open(&w, path, 16000u) != 0) {
        fprintf(stderr, "[wav] writer open failed\n");
        return 1;
    }
    int wrote = wav_write_samples(&w, gold, N_SAMPS);
    if (wrote != N_SAMPS) {
        fprintf(stderr, "[wav] wrote %d != %d\n", wrote, N_SAMPS);
        failures++;
    }
    wav_write_close(&w);

    /* Read. */
    wav_reader_t r;
    if (wav_read_open(&r, path) != 0) {
        fprintf(stderr, "[wav] reader open failed\n");
        return 1;
    }

    if (r.sample_rate != 16000u) {
        fprintf(stderr, "[wav] sample_rate=%u expected 16000\n",
                (unsigned)r.sample_rate);
        failures++;
    }
    if (r.num_channels != 1) {
        fprintf(stderr, "[wav] num_channels=%u expected 1\n",
                (unsigned)r.num_channels);
        failures++;
    }
    if (r.bits_per_sample != 16) {
        fprintf(stderr, "[wav] bps=%u expected 16\n",
                (unsigned)r.bits_per_sample);
        failures++;
    }
    if (r.total_samples != (uint32_t)N_SAMPS) {
        fprintf(stderr, "[wav] total_samples=%u expected %d\n",
                (unsigned)r.total_samples, N_SAMPS);
        failures++;
    }

    /* Read in two chunks to also exercise samples_read tracking. */
    memset(readback, 0, sizeof(readback));
    int g1 = wav_read_samples(&r, readback,        500);
    int g2 = wav_read_samples(&r, readback + 500,  500);
    if (g1 != 500 || g2 != 500) {
        fprintf(stderr, "[wav] partial read got %d + %d\n", g1, g2);
        failures++;
    }

    int g3 = wav_read_samples(&r, readback, 1);
    if (g3 != 0) {
        fprintf(stderr, "[wav] EOF read returned %d, expected 0\n", g3);
        failures++;
    }

    for (int n = 0; n < N_SAMPS; ++n) {
        if (readback[n] != gold[n]) {
            fprintf(stderr, "[wav] mismatch at n=%d: got %d expected %d\n",
                    n, readback[n], gold[n]);
            failures++;
            if (failures > 5) break;
        }
    }

    /* Exercise rewind. */
    if (wav_read_rewind(&r) != 0) {
        fprintf(stderr, "[wav] rewind failed\n");
        failures++;
    } else {
        q15_t first;
        int rr = wav_read_samples(&r, &first, 1);
        if (rr != 1 || first != gold[0]) {
            fprintf(stderr, "[wav] post-rewind sample[0]=%d expected %d (rr=%d)\n",
                    first, gold[0], rr);
            failures++;
        }
    }

    wav_read_close(&r);
    return failures;
}

int main(void) {
    int failures = 0;
    failures += test_roundtrip();

    if (failures == 0) {
        printf("[wav] ALL TESTS PASSED\n");
        return 0;
    }
    printf("[wav] SOME TESTS FAILED (%d)\n", failures);
    return 1;
}
