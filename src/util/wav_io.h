/* ============================================================
 * wav_io.h  --  Minimal RIFF/WAVE I/O for 16-bit PCM mono
 *
 * Supports:
 *   - PCM16, mono, configurable sample rate (validated by caller)
 *   - Skips unknown chunks (e.g. 'LIST') between 'fmt ' and 'data'
 *   - Little-endian on-disk (project targets LE hosts)
 *
 * Q15 samples are bit-identical to PCM16 samples, so no
 * conversion is performed on read/write.
 * ============================================================ */

#ifndef TMS320_WAV_IO_H
#define TMS320_WAV_IO_H

#include "fixed_point.h"
#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE   *f;
    uint32_t sample_rate;     /* required: 16000 */
    uint16_t num_channels;    /* required: 1     */
    uint16_t bits_per_sample; /* required: 16    */
    uint32_t total_samples;
    uint32_t samples_read;
    long     data_start_pos;
} wav_reader_t;

typedef struct {
    FILE   *f;
    uint32_t sample_rate;
    uint32_t samples_written;
    long     riff_size_pos;
    long     data_size_pos;
} wav_writer_t;

/* Returns 0 on success, non-zero on failure (and prints stderr message). */
int  wav_read_open(wav_reader_t *r, const char *path);
/* Returns number of samples actually read (may be < n at EOF). */
int  wav_read_samples(wav_reader_t *r, q15_t *buf, int n);
int  wav_read_rewind(wav_reader_t *r);  /* seeks back to start of data */
void wav_read_close(wav_reader_t *r);

int  wav_write_open(wav_writer_t *w, const char *path, uint32_t sample_rate);
int  wav_write_samples(wav_writer_t *w, const q15_t *buf, int n);
void wav_write_close(wav_writer_t *w);  /* patches RIFF/data sizes */

#endif /* TMS320_WAV_IO_H */
