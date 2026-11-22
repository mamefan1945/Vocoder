#!/usr/bin/env python3
"""Compute delay-aligned, reference-based STOI scores for PrismaVox renders."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from pystoi import stoi

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_DIR = ROOT / "TestAudio"
RENDER_DIR = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "test-renders"
ALIGNMENT_FRAME_MS = 10
MAX_ALIGNMENT_MS = 200


def read_mono(path: Path) -> tuple[int, np.ndarray]:
    sample_rate, samples = wavfile.read(path)
    if samples.ndim == 2:
        samples = samples[:, 0]
    if np.issubdtype(samples.dtype, np.integer):
        info = np.iinfo(samples.dtype)
        scale = float(max(abs(info.min), abs(info.max)))
        samples = samples.astype(np.float64) / scale
    else:
        samples = samples.astype(np.float64)
    return int(sample_rate), samples


def frame_envelope(samples: np.ndarray, frame_length: int) -> np.ndarray:
    frame_count = samples.size // frame_length
    if frame_count == 0:
        return np.empty(0, dtype=np.float64)
    framed = samples[:frame_count * frame_length].reshape(frame_count, frame_length)
    # Log RMS reduces the influence of carrier amplitude while retaining the
    # speech-shaped amplitude contour used to estimate relative timing.
    rms = np.sqrt(np.mean(np.square(framed), axis=1) + 1.0e-12)
    return np.log(rms + 1.0e-8)


def estimate_delay_samples(reference: np.ndarray, processed: np.ndarray,
                           sample_rate: int) -> int:
    """Estimate processed-signal delay from 10 ms RMS envelopes.

    Positive results mean the processed signal lags the reference. The search
    is deliberately bounded: this is intended to correct pipeline latency,
    not to find an unrelated matching phrase elsewhere in a recording.
    """
    frame_length = max(1, round(sample_rate * ALIGNMENT_FRAME_MS / 1000.0))
    ref_env = frame_envelope(reference, frame_length)
    out_env = frame_envelope(processed, frame_length)
    max_lag = round(MAX_ALIGNMENT_MS / ALIGNMENT_FRAME_MS)
    best_lag = 0
    best_corr = -np.inf

    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            count = min(ref_env.size, out_env.size - lag)
            if count <= 0:
                continue
            a, b = ref_env[:count], out_env[lag:lag + count]
        else:
            offset = -lag
            count = min(ref_env.size - offset, out_env.size)
            if count <= 0:
                continue
            a, b = ref_env[offset:offset + count], out_env[:count]

        if count < 50:
            continue
        a = a - np.mean(a)
        b = b - np.mean(b)
        denominator = np.linalg.norm(a) * np.linalg.norm(b)
        if denominator <= 1.0e-12:
            continue
        correlation = float(np.dot(a, b) / denominator)
        if correlation > best_corr:
            best_corr = correlation
            best_lag = lag

    if not np.isfinite(best_corr):
        raise ValueError("could not estimate a reliable speech-envelope alignment")
    return best_lag * frame_length


def align_signals(reference: np.ndarray, processed: np.ndarray,
                  delay_samples: int) -> tuple[np.ndarray, np.ndarray]:
    # Positive delay means the processed signal starts later.
    if delay_samples > 0:
        return reference[:-delay_samples], processed[delay_samples:]
    if delay_samples < 0:
        offset = -delay_samples
        return reference[offset:], processed[:-offset]
    return reference, processed


def main() -> int:
    failures = []
    for stem in ("vocoder_test_ash", "vocoder_test_nova"):
        reference_path = REFERENCE_DIR / f"{stem}.wav"
        processed_path = RENDER_DIR / f"{stem}_vocoded.wav"
        if not reference_path.is_file() or not processed_path.is_file():
            print(f"ERROR: missing reference or render for {stem}", file=sys.stderr)
            failures.append(stem)
            continue

        ref_rate, reference = read_mono(reference_path)
        out_rate, processed = read_mono(processed_path)
        if ref_rate != out_rate:
            print(f"ERROR: sample-rate mismatch for {stem}: {ref_rate} vs {out_rate}",
                  file=sys.stderr)
            failures.append(stem)
            continue

        identity_score = float(stoi(reference, reference, ref_rate, extended=False))
        try:
            delay_samples = estimate_delay_samples(reference, processed, ref_rate)
            aligned_reference, aligned_processed = align_signals(
                reference, processed, delay_samples)
        except ValueError as error:
            print(f"ERROR: alignment failed for {stem}: {error}", file=sys.stderr)
            failures.append(stem)
            continue

        length = min(aligned_reference.size, aligned_processed.size)
        aligned_reference = aligned_reference[:length]
        aligned_processed = aligned_processed[:length]
        if length < ref_rate:
            print(f"ERROR: less than one second of aligned speech for {stem}",
                  file=sys.stderr)
            failures.append(stem)
            continue

        vocoder_score = float(stoi(
            aligned_reference, aligned_processed, ref_rate, extended=False))
        if not np.isfinite(identity_score) or identity_score < 0.99:
            print(f"ERROR: STOI identity sanity check failed for {stem}: {identity_score}",
                  file=sys.stderr)
            failures.append(stem)
            continue
        if not np.isfinite(vocoder_score) or not -1.0 <= vocoder_score <= 1.0:
            print(f"ERROR: invalid STOI score for {stem}: {vocoder_score}",
                  file=sys.stderr)
            failures.append(stem)
            continue

        delay_ms = delay_samples * 1000.0 / ref_rate
        print(f"{stem}: STOI={vocoder_score:.4f} (estimated processed delay="
              f"{delay_ms:+.1f} ms; identity control={identity_score:.4f}); "
              "delay-aligned baseline only, no quality threshold applied")

    if failures:
        print("STOI evaluation failed for: " + ", ".join(failures), file=sys.stderr)
        return 1
    print("STOI evaluation completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
