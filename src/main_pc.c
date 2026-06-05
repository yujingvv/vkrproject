/* ============================================================
 * main_pc.c  --  PC simulator entry point (host-only)
 *
 * Usage:   ./tms320_sim INPUT.wav OUTPUT.wav
 *
 * Mirrors the high-level flow of matlab/compress.m:
 *   1. Pass 1: scan input to find peak |x| (lines 117-119 in compress.m).
 *   2. Pass 2: read input HOP samples at a time, run the per-frame
 *      pipeline, accumulate output in a heap buffer.
 *   3. After EOF, push 3 extra hops of zero input to drain the OLA tail.
 *   4. Scale accumulated output to match input peak, write to output WAV.
 *
 * Why this file is host-only:
 *   - calloc() of the whole output stream (~MB for any real audio)
 *     is impossible on the C5515's tiny RAM.
 *   - Two-pass peak scan requires rewinding the source, which a real
 *     ADC stream cannot do.
 *   - File I/O has no equivalent in the on-chip ISR-driven path.
 *
 * The on-chip entry-point lives in app_main.c.  Both files include
 * the same pipeline.h, so the algorithm code is shared verbatim.
 * ============================================================ */

#include "config.h"
#include "util/fixed_point.h"
#include "util/wav_io.h"
#include "hal/cycle_counter.h"
#include "algo/pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Helpers ---- */

static uint32_t scan_peak(const char *path) {
    wav_reader_t r;
    q15_t        buf[HOP];
    int          got;
    uint32_t     peak = 0;

    if (wav_read_open(&r, path) != 0) {
        fprintf(stderr, "[main] cannot open input '%s' for pass 1\n", path);
        return 0;
    }
    while ((got = wav_read_samples(&r, buf, HOP)) > 0) {
        for (int i = 0; i < got; ++i) {
            int32_t v = buf[i];
            uint32_t a = (uint32_t)(v < 0 ? -v : v);
            if (a > peak) peak = a;
        }
    }
    wav_read_close(&r);
    return peak;
}

/* ---- main ---- */

int main(int argc, char **argv) {
    const char  *in_path;
    const char  *out_path;
    wav_reader_t reader;
    wav_writer_t writer;
    uint32_t     peak_in;
    q15_t       *out_accum  = NULL;
    size_t       out_cap    = 0;
    size_t       out_len    = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT.wav OUTPUT.wav\n", argv[0]);
        return 1;
    }
    in_path  = argv[1];
    out_path = argv[2];

    cycles_reset();
    pipeline_init();   /* explicit init so first frame is not the one that
                          builds the FFT bit-reversal table etc. */

    /* ---- Pass 1: peak scan ---- */
    peak_in = scan_peak(in_path);
    if (peak_in == 0) {
        fprintf(stderr, "[main] input is empty or silent; writing silent output\n");
    }

    /* ---- Pass 2: open input again, accumulate output in RAM ---- */
    if (wav_read_open(&reader, in_path) != 0) {
        fprintf(stderr, "[main] pass-2 open failed\n");
        return 1;
    }

    /* Allocate output buffer sized to input + 3 hops of drain tail. */
    out_cap = (size_t)reader.total_samples + 3u * (size_t)HOP;
    out_accum = (q15_t *)calloc(out_cap, sizeof(q15_t));
    if (!out_accum) {
        fprintf(stderr, "[main] out_accum alloc failed (%zu samples)\n", out_cap);
        wav_read_close(&reader);
        return 1;
    }

    q15_t in_hop[HOP];
    q15_t out_hop[HOP];
    int   got;
    int   drain_hops_remaining = 3;

    for (;;) {
        got = wav_read_samples(&reader, in_hop, HOP);
        if (got > 0 && got < HOP) {
            /* Zero-pad the last partial hop. */
            for (int i = got; i < HOP; ++i) in_hop[i] = 0;
            got = HOP;
        }
        if (got == 0) {
            /* EOF: push zero hops to drain OLA pipeline. */
            if (drain_hops_remaining-- <= 0) break;
            memset(in_hop, 0, sizeof(in_hop));
        }

        pipeline_process(in_hop, out_hop);

        /* Append to accumulator (guarded by capacity). */
        if (out_len + HOP <= out_cap) {
            memcpy(&out_accum[out_len], out_hop, sizeof(out_hop));
            out_len += HOP;
        }
    }

    wav_read_close(&reader);

    /* ---- Peak normalization (MATLAB lines 117-119) ---- */
    uint32_t peak_out = 0;
    for (size_t i = 0; i < out_len; ++i) {
        int32_t v = out_accum[i];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > peak_out) peak_out = a;
    }

    if (peak_out > 0 && peak_in > 0 && peak_out != peak_in) {
        /* Scale = peak_in / peak_out.  We compute in 32-bit per sample.
         * To avoid floating-point in main, use a Q16 multiplier:
         *   scale_q16 = (peak_in << 16) / peak_out
         *   y = (x * scale_q16) >> 16, then saturate to Q15. */
        uint64_t scale_q16 = ((uint64_t)peak_in << 16) / (uint64_t)peak_out;
        for (size_t i = 0; i < out_len; ++i) {
            int64_t v = (int64_t)out_accum[i] * (int64_t)scale_q16;
            v >>= 16;
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            out_accum[i] = (q15_t)v;
        }
    }

    /* ---- Write output WAV ---- */
    if (wav_write_open(&writer, out_path, (uint32_t)FS) != 0) {
        fprintf(stderr, "[main] cannot open '%s' for writing\n", out_path);
        free(out_accum);
        return 1;
    }
    /* Trim trailing drain hops back so output length matches input length
     * (compress.m line 112 trims the ISTFT result to the input length).
     * reader.total_samples was populated by wav_read_open; the struct
     * stays valid after wav_read_close (we only nulled the FILE*). */
    size_t write_len = out_len;
    if ((size_t)reader.total_samples < write_len) {
        write_len = (size_t)reader.total_samples;
    }
    wav_write_samples(&writer, out_accum, (int)write_len);
    wav_write_close(&writer);

    free(out_accum);

    fprintf(stdout, "[main] done. peak_in=%u peak_out=%u out_samples=%zu\n",
            (unsigned)peak_in, (unsigned)peak_out, write_len);
    fprintf(stdout, "[main] estimated cycles = %llu\n",
            (unsigned long long)cycles_total());
    return 0;
}
