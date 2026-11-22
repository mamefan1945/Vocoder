#!/usr/bin/env python3
"""Compute reference-based STOI scores for the PrismaVox vocoder test renders."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from pystoi import stoi

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_DIR = ROOT / "TestAudio"
RENDER_DIR = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "test-renders"


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

        length = min(reference.size, processed.size)
        reference = reference[:length]
        processed = processed[:length]
        if length < ref_rate:
            print(f"ERROR: less than one second of aligned speech for {stem}",
                  file=sys.stderr)
            failures.append(stem)
            continue

        identity_score = float(stoi(reference, reference, ref_rate, extended=False))
        vocoder_score = float(stoi(reference, processed, ref_rate, extended=False))
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

        print(f"{stem}: STOI={vocoder_score:.4f} (identity control={identity_score:.4f}); "
              "reported baseline only, no quality threshold applied")

    if failures:
        print("STOI evaluation failed for: " + ", ".join(failures), file=sys.stderr)
        return 1
    print("STOI evaluation completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
