/* ============================================================
 * wav_io.c  --  Minimal RIFF/WAVE I/O implementation
 *
 * Endianness note: this project targets little-endian hosts
 * (Linux on x86_64/aarch64) and a little-endian on-disk format.
 * We therefore fread/fwrite raw int16_t samples and treat
 * multi-byte header fields as LE without byte-swapping. If you
 * ever port to a big-endian host, add swaps in the appropriate
 * helpers below.
 * ============================================================ */

#include "wav_io.h"
#include "../config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Header helpers ---- */

static int read_u32_le(FILE *f, uint32_t *out) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static int read_u16_le(FILE *f, uint16_t *out) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return -1;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 0;
}

static int write_u32_le(FILE *f, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v        & 0xFF);
    b[1] = (uint8_t)((v >> 8)  & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    return (fwrite(b, 1, 4, f) == 4) ? 0 : -1;
}

static int write_u16_le(FILE *f, uint16_t v) {
    uint8_t b[2];
    b[0] = (uint8_t)(v       & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    return (fwrite(b, 1, 2, f) == 2) ? 0 : -1;
}

static int read_fourcc(FILE *f, char out[4]) {
    return (fread(out, 1, 4, f) == 4) ? 0 : -1;
}

/* ---- Reader ---- */

int wav_read_open(wav_reader_t *r, const char *path) {
    char     id[4];
    uint32_t u32;
    uint16_t u16;
    uint16_t audio_format;
    uint32_t fmt_chunk_size;
    uint32_t data_chunk_size;

    if (!r || !path) return -1;
    memset(r, 0, sizeof(*r));

    r->f = fopen(path, "rb");
    if (!r->f) {
        fprintf(stderr, "[wav] cannot open '%s' for reading\n", path);
        return -1;
    }

    /* RIFF header */
    if (read_fourcc(r->f, id) != 0 || memcmp(id, "RIFF", 4) != 0) {
        fprintf(stderr, "[wav] not a RIFF file: %s\n", path);
        goto fail;
    }
    if (read_u32_le(r->f, &u32) != 0) goto fail; /* riff size, ignore */
    if (read_fourcc(r->f, id) != 0 || memcmp(id, "WAVE", 4) != 0) {
        fprintf(stderr, "[wav] not a WAVE file: %s\n", path);
        goto fail;
    }

    /* Find 'fmt ' chunk (it should be next, but allow skipping). */
    fmt_chunk_size = 0;
    for (;;) {
        if (read_fourcc(r->f, id) != 0) {
            fprintf(stderr, "[wav] unexpected EOF looking for 'fmt '\n");
            goto fail;
        }
        if (read_u32_le(r->f, &fmt_chunk_size) != 0) goto fail;
        if (memcmp(id, "fmt ", 4) == 0) break;
        /* skip unknown chunk */
        if (fseek(r->f, (long)fmt_chunk_size, SEEK_CUR) != 0) goto fail;
    }

    if (fmt_chunk_size < 16) {
        fprintf(stderr, "[wav] 'fmt ' chunk too small (%u)\n", fmt_chunk_size);
        goto fail;
    }

    if (read_u16_le(r->f, &audio_format)        != 0) goto fail;
    if (read_u16_le(r->f, &r->num_channels)     != 0) goto fail;
    if (read_u32_le(r->f, &r->sample_rate)      != 0) goto fail;
    if (read_u32_le(r->f, &u32)                 != 0) goto fail; /* byte rate */
    if (read_u16_le(r->f, &u16)                 != 0) goto fail; /* block align */
    if (read_u16_le(r->f, &r->bits_per_sample)  != 0) goto fail;

    /* Skip any extra bytes inside 'fmt ' chunk */
    if (fmt_chunk_size > 16) {
        if (fseek(r->f, (long)(fmt_chunk_size - 16), SEEK_CUR) != 0) goto fail;
    }

    if (audio_format != 1) {
        fprintf(stderr, "[wav] not PCM (audio_format=%u): %s\n",
                (unsigned)audio_format, path);
        goto fail;
    }
    if (r->num_channels != 1) {
        fprintf(stderr, "[wav] not mono (channels=%u): %s\n",
                (unsigned)r->num_channels, path);
        goto fail;
    }
    if (r->bits_per_sample != 16) {
        fprintf(stderr, "[wav] not 16-bit (bps=%u): %s\n",
                (unsigned)r->bits_per_sample, path);
        goto fail;
    }
    if (r->sample_rate != (uint32_t)FS) {
        fprintf(stderr, "[wav] unexpected sample rate %u (expected %d): %s\n",
                (unsigned)r->sample_rate, FS, path);
        goto fail;
    }

    /* Find 'data' chunk (skip 'LIST' or anything else). */
    for (;;) {
        if (read_fourcc(r->f, id) != 0) {
            fprintf(stderr, "[wav] unexpected EOF looking for 'data'\n");
            goto fail;
        }
        if (read_u32_le(r->f, &data_chunk_size) != 0) goto fail;
        if (memcmp(id, "data", 4) == 0) break;
        if (fseek(r->f, (long)data_chunk_size, SEEK_CUR) != 0) goto fail;
    }

    r->total_samples  = data_chunk_size / (uint32_t)sizeof(int16_t);
    r->samples_read   = 0;
    r->data_start_pos = ftell(r->f);
    if (r->data_start_pos < 0) goto fail;

    return 0;

fail:
    if (r->f) {
        fclose(r->f);
        r->f = NULL;
    }
    return -1;
}

int wav_read_samples(wav_reader_t *r, q15_t *buf, int n) {
    size_t got;
    if (!r || !r->f || !buf || n <= 0) return 0;

    /* Don't read past declared data length. */
    uint32_t remain = (r->total_samples > r->samples_read)
                    ? (r->total_samples - r->samples_read) : 0;
    if ((uint32_t)n > remain) n = (int)remain;
    if (n == 0) return 0;

    got = fread(buf, sizeof(int16_t), (size_t)n, r->f);
    r->samples_read += (uint32_t)got;
    return (int)got;
}

int wav_read_rewind(wav_reader_t *r) {
    if (!r || !r->f) return -1;
    if (fseek(r->f, r->data_start_pos, SEEK_SET) != 0) {
        fprintf(stderr, "[wav] rewind failed\n");
        return -1;
    }
    r->samples_read = 0;
    return 0;
}

void wav_read_close(wav_reader_t *r) {
    if (!r) return;
    if (r->f) {
        fclose(r->f);
        r->f = NULL;
    }
}

/* ---- Writer ---- */

int wav_write_open(wav_writer_t *w, const char *path, uint32_t sample_rate) {
    if (!w || !path) return -1;
    memset(w, 0, sizeof(*w));

    w->f = fopen(path, "wb");
    if (!w->f) {
        fprintf(stderr, "[wav] cannot open '%s' for writing\n", path);
        return -1;
    }
    w->sample_rate     = sample_rate;
    w->samples_written = 0;

    /* RIFF header with placeholder sizes (patched in wav_write_close). */
    if (fwrite("RIFF", 1, 4, w->f) != 4) goto fail;
    w->riff_size_pos = ftell(w->f);
    if (w->riff_size_pos < 0)              goto fail;
    if (write_u32_le(w->f, 0)        != 0) goto fail; /* RIFF size placeholder */
    if (fwrite("WAVE", 1, 4, w->f) != 4)   goto fail;

    /* 'fmt ' chunk */
    if (fwrite("fmt ", 1, 4, w->f) != 4)   goto fail;
    if (write_u32_le(w->f, 16)        != 0) goto fail; /* PCM fmt size */
    if (write_u16_le(w->f, 1)         != 0) goto fail; /* audio_format = PCM */
    if (write_u16_le(w->f, 1)         != 0) goto fail; /* mono */
    if (write_u32_le(w->f, sample_rate) != 0) goto fail;
    if (write_u32_le(w->f, sample_rate * 2u) != 0) goto fail; /* byte rate = sr * 2 */
    if (write_u16_le(w->f, 2)         != 0) goto fail; /* block align = 2 */
    if (write_u16_le(w->f, 16)        != 0) goto fail; /* bps */

    /* 'data' chunk header (size patched at close). */
    if (fwrite("data", 1, 4, w->f) != 4)   goto fail;
    w->data_size_pos = ftell(w->f);
    if (w->data_size_pos < 0)              goto fail;
    if (write_u32_le(w->f, 0)         != 0) goto fail; /* data size placeholder */

    return 0;

fail:
    fprintf(stderr, "[wav] failed to write WAV header\n");
    if (w->f) {
        fclose(w->f);
        w->f = NULL;
    }
    return -1;
}

int wav_write_samples(wav_writer_t *w, const q15_t *buf, int n) {
    size_t wrote;
    if (!w || !w->f || !buf || n <= 0) return 0;
    wrote = fwrite(buf, sizeof(int16_t), (size_t)n, w->f);
    w->samples_written += (uint32_t)wrote;
    return (int)wrote;
}

void wav_write_close(wav_writer_t *w) {
    uint32_t data_bytes;
    uint32_t riff_bytes;

    if (!w || !w->f) return;

    data_bytes = w->samples_written * (uint32_t)sizeof(int16_t);
    /* RIFF size = total file size - 8 (RIFF + size field).
     * File = 8 (RIFF + size) + 4 (WAVE) + 8 + 16 (fmt) + 8 + data_bytes.
     * RIFF size field = total - 8 = 4 + 8 + 16 + 8 + data_bytes = 36 + data_bytes. */
    riff_bytes = 36u + data_bytes;

    if (fseek(w->f, w->riff_size_pos, SEEK_SET) == 0) {
        (void)write_u32_le(w->f, riff_bytes);
    }
    if (fseek(w->f, w->data_size_pos, SEEK_SET) == 0) {
        (void)write_u32_le(w->f, data_bytes);
    }
    fclose(w->f);
    w->f = NULL;
}
