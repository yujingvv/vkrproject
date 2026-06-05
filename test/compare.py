#!/usr/bin/env python3
"""
compare.py -- Compare two WAVs for the frequency-transposition signature.

Usage:
    python3 test/compare.py INPUT.wav OUTPUT.wav [OPTIONAL_PNG]

Both files must be 16 kHz, mono, 16-bit PCM. Computes STFT magnitude
(1024-pt FFT, hop=256, Hamming window) for each, then totals energy in
two bands:
    SOURCE band : 4000-8000 Hz (FFT bins 256..511)
    TARGET band : 2000-4000 Hz (FFT bins 128..255)

PASS criteria (the transposition signature):
    Source-band energy(OUTPUT) <  energy(INPUT) - 3 dB   (attenuated)
    Target-band energy(OUTPUT) >  energy(INPUT) + 3 dB   (boosted)

Exit code is 0 on PASS and 1 on FAIL.
"""

from __future__ import annotations

import sys
import wave

import numpy as np


# ----- STFT parameters (must match MATLAB reference: 1024-FFT, hop=256, Hamming).
FFT_SIZE = 1024
HOP      = 256
WIN      = np.hamming(FFT_SIZE).astype(np.float64)

# Bin slices (rfft has FFT_SIZE/2 + 1 = 513 bins for FFT_SIZE=1024 at 16 kHz):
#   bin k -> frequency k * fs / FFT_SIZE = k * 15.625 Hz
#   2 kHz -> bin 128 ; 4 kHz -> bin 256 ; 8 kHz -> bin 512.
TARGET_BAND = slice(128, 256)   # 2000..4000 Hz
SOURCE_BAND = slice(256, 512)   # 4000..8000 Hz


def load_wav(path: str) -> np.ndarray:
    """Load a 16-bit mono 16 kHz WAV as float64 in [-1, 1]."""
    with wave.open(path, "rb") as w:
        n_ch    = w.getnchannels()
        sw      = w.getsampwidth()
        fs      = w.getframerate()
        n_fr    = w.getnframes()
        raw     = w.readframes(n_fr)
    if n_ch != 1:
        raise ValueError(f"{path}: expected mono, got {n_ch} channels")
    if sw != 2:
        raise ValueError(f"{path}: expected 16-bit, got {sw*8}-bit")
    if fs != 16000:
        raise ValueError(f"{path}: expected 16000 Hz, got {fs} Hz")
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return x


def stft_magnitude(x: np.ndarray) -> np.ndarray:
    """Return |STFT(x)| with shape (n_frames, FFT_SIZE/2 + 1)."""
    if len(x) < FFT_SIZE:
        # Pad short signals to at least one frame.
        x = np.concatenate([x, np.zeros(FFT_SIZE - len(x))])
    n_frames = 1 + (len(x) - FFT_SIZE) // HOP
    out = np.empty((n_frames, FFT_SIZE // 2 + 1), dtype=np.float64)
    for i in range(n_frames):
        start = i * HOP
        frame = x[start:start + FFT_SIZE] * WIN
        out[i] = np.abs(np.fft.rfft(frame, n=FFT_SIZE))
    return out


def band_energy_db(mag: np.ndarray, band: slice) -> float:
    """Total band energy across all frames, in dB (10*log10).

    Returns -inf if the band is exactly zero; we add a small floor to
    avoid that pathology in practice.
    """
    e = float(np.sum(mag[:, band] ** 2))
    e = max(e, 1e-20)
    return 10.0 * np.log10(e)


def cosine_similarity_per_frame(a: np.ndarray, b: np.ndarray) -> float:
    """Average cosine similarity of |STFT| frames; trims to shorter length."""
    n = min(a.shape[0], b.shape[0])
    if n == 0:
        return float("nan")
    a = a[:n]
    b = b[:n]
    na = np.linalg.norm(a, axis=1)
    nb = np.linalg.norm(b, axis=1)
    denom = na * nb
    mask = denom > 0
    if not np.any(mask):
        return float("nan")
    dots = np.sum(a * b, axis=1)
    sims = np.zeros(n, dtype=np.float64)
    sims[mask] = dots[mask] / denom[mask]
    return float(np.mean(sims[mask]))


def maybe_save_spectrogram_png(path: str,
                               mag_in: np.ndarray,
                               mag_out: np.ndarray) -> None:
    """Save a side-by-side spectrogram PNG. Silently skip if matplotlib
    is unavailable."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return

    fs = 16000.0

    def to_db(m):
        return 20.0 * np.log10(np.maximum(m, 1e-10))

    db_in  = to_db(mag_in).T   # (freq, time)
    db_out = to_db(mag_out).T

    vmin = min(db_in.min(), db_out.min())
    vmax = max(db_in.max(), db_out.max())

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), sharey=True)
    extent_in  = [0, mag_in.shape[0]  * HOP / fs, 0, fs / 2.0]
    extent_out = [0, mag_out.shape[0] * HOP / fs, 0, fs / 2.0]

    axes[0].imshow(db_in,  origin="lower", aspect="auto",
                   extent=extent_in,  vmin=vmin, vmax=vmax)
    axes[0].set_title("INPUT")
    axes[0].set_xlabel("Time (s)")
    axes[0].set_ylabel("Frequency (Hz)")

    im = axes[1].imshow(db_out, origin="lower", aspect="auto",
                        extent=extent_out, vmin=vmin, vmax=vmax)
    axes[1].set_title("OUTPUT")
    axes[1].set_xlabel("Time (s)")

    fig.colorbar(im, ax=axes, label="dB")
    fig.savefig(path, dpi=120, bbox_inches="tight")
    plt.close(fig)


def main(argv: list[str]) -> int:
    if len(argv) < 3 or len(argv) > 4:
        print("Usage: python3 test/compare.py INPUT.wav OUTPUT.wav "
              "[OPTIONAL_PNG]", file=sys.stderr)
        return 2

    in_path  = argv[1]
    out_path = argv[2]
    png_path = argv[3] if len(argv) == 4 else None

    x_in  = load_wav(in_path)
    x_out = load_wav(out_path)

    mag_in  = stft_magnitude(x_in)
    mag_out = stft_magnitude(x_out)

    src_in_db  = band_energy_db(mag_in,  SOURCE_BAND)
    src_out_db = band_energy_db(mag_out, SOURCE_BAND)
    tgt_in_db  = band_energy_db(mag_in,  TARGET_BAND)
    tgt_out_db = band_energy_db(mag_out, TARGET_BAND)

    src_delta = src_out_db - src_in_db
    tgt_delta = tgt_out_db - tgt_in_db

    cos_sim = cosine_similarity_per_frame(mag_in, mag_out)

    print("              INPUT      OUTPUT     DELTA")
    print(f"Source band:  {src_in_db:7.2f} dB {src_out_db:7.2f} dB "
          f"{src_delta:+7.2f} dB")
    print(f"Target band:  {tgt_in_db:7.2f} dB {tgt_out_db:7.2f} dB "
          f"{tgt_delta:+7.2f} dB")
    print(f"Average frame cosine similarity: {cos_sim:.4f}")

    # Source band should drop noticeably but not by an absurd amount.
    # The MATLAB reference algorithm leaves a ~6 dB residual after
    # real(istft(...)) because the negative-freq mirror of the source
    # band is preserved.  A C output that flattens the source band to
    # silence indicates the pipeline is over-zeroing the spectrum
    # (the C1 bug class).
    src_ok = -20.0 < src_delta < -3.0
    tgt_ok = tgt_delta >  3.0

    if src_ok and tgt_ok:
        print("[compare] PASS")
        rc = 0
    else:
        failed = []
        if not src_ok:
            failed.append(
                f"source-band attenuation (delta={src_delta:+.2f} dB, "
                "need -20 < delta < -3 dB)")
        if not tgt_ok:
            failed.append(
                f"target-band boost (delta={tgt_delta:+.2f} dB, "
                "need > +3 dB)")
        print("[compare] FAIL: " + "; ".join(failed))
        rc = 1

    if png_path is not None:
        maybe_save_spectrogram_png(png_path, mag_in, mag_out)

    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
