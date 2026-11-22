#!/usr/bin/env python3
"""Compute delay-aligned, reference-based STOI scores for PrismaVox renders."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt
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
                           sample_rate: int) -> tuple[int, float]:
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
    return best_lag * frame_length, float(best_corr)


def align_signals(reference: np.ndarray, processed: np.ndarray,
                  delay_samples: int) -> tuple[np.ndarray, np.ndarray]:
    # Positive delay means the processed signal starts later.
    if delay_samples > 0:
        return reference[:-delay_samples], processed[delay_samples:]
    if delay_samples < 0:
        offset = -delay_samples
        return reference[offset:], processed[:-offset]
    return reference, processed


def band_envelope_correlations(reference: np.ndarray, processed: np.ndarray,
                                 sample_rate: int) -> dict[int, float]:
    """Compare speech-energy contours in corresponding frequency bands.

    This is a vocoder-specific diagnostic, not a perceptual intelligibility
    score. Identical analysis filters and 20 ms RMS envelopes make it less
    sensitive to carrier cycles while retaining speech articulation timing.
    """
    centres = (160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300)
    frame_length = max(1, round(sample_rate * 0.020))
    results: dict[int, dict[str, float]] = {}
    length = min(reference.size, processed.size)
    reference = reference[:length]
    processed = processed[:length]
    total_reference_power = 0.0

    for centre in centres:
        low = centre / np.sqrt(2.0)
        high = min(centre * np.sqrt(2.0), sample_rate * 0.49)
        if low >= high:
            continue
        sos = butter(4, (low, high), btype="bandpass", fs=sample_rate,
                     output="sos")
        ref_band = sosfilt(sos, reference)
        out_band = sosfilt(sos, processed)
        frame_count = length // frame_length
        if frame_count < 50:
            continue
        ref_frames = ref_band[:frame_count * frame_length].reshape(
            frame_count, frame_length)
        out_frames = out_band[:frame_count * frame_length].reshape(
            frame_count, frame_length)
        ref_power = float(np.mean(ref_band ** 2))
        out_power = float(np.mean(out_band ** 2))
        ref_env = np.log(np.sqrt(np.mean(ref_frames ** 2, axis=1)) + 1.0e-8)
        out_env = np.log(np.sqrt(np.mean(out_frames ** 2, axis=1)) + 1.0e-8)
        if ref_power < 1.0e-12 or np.max(ref_env) < np.log(1.0e-6):
            continue
        ref_env -= np.mean(ref_env)
        out_env -= np.mean(out_env)
        denominator = np.linalg.norm(ref_env) * np.linalg.norm(out_env)
        if denominator > 1.0e-12:
            results[centre] = {
                "correlation": float(np.dot(ref_env, out_env) / denominator),
                "reference_power": ref_power,
                "output_power": out_power,
            }
            total_reference_power += ref_power

    if total_reference_power > 0.0:
        for values in results.values():
            values["reference_share"] = (
                values["reference_power"] / total_reference_power)
            values["level_change_db"] = 10.0 * np.log10(
                max(values["output_power"], 1.0e-20)
                / max(values["reference_power"], 1.0e-20))
    return results


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
        unaligned_length = min(reference.size, processed.size)
        if unaligned_length < ref_rate:
            print(f"ERROR: less than one second of unaligned speech for {stem}",
                  file=sys.stderr)
            failures.append(stem)
            continue
        unaligned_score = float(stoi(
            reference[:unaligned_length], processed[:unaligned_length],
            ref_rate, extended=False))
        try:
            delay_samples, alignment_correlation = estimate_delay_samples(
                reference, processed, ref_rate)
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
        if (not np.isfinite(vocoder_score) or not -1.0 <= vocoder_score <= 1.0
                or not np.isfinite(unaligned_score)
                or not -1.0 <= unaligned_score <= 1.0):
            print(f"ERROR: invalid STOI score for {stem}: {vocoder_score}",
                  file=sys.stderr)
            failures.append(stem)
            continue

        delay_ms = delay_samples * 1000.0 / ref_rate
        band_scores = band_envelope_correlations(
            aligned_reference, aligned_processed, ref_rate)
        if band_scores:
            band_summary = ", ".join(
                f"{centre}Hz:corr={values['correlation']:.3f},"
                f"ref={values['reference_share'] * 100.0:.1f}%,"
                f"level={values['level_change_db']:+.1f}dB"
                for centre, values in band_scores.items())
            band_mean = float(np.mean(
                [values["correlation"] for values in band_scores.values()]))
            weighted_mean = sum(
                values["correlation"] * values["reference_share"]
                for values in band_scores.values())
            band_report = (
                f"band-envelope mean={band_mean:.3f}, "
                f"reference-energy-weighted={weighted_mean:.3f} "
                f"({band_summary})")
        else:
            band_report = "band-envelope correlation unavailable"
        print(f"{stem}: STOI unaligned={unaligned_score:.4f}, "
              f"aligned={vocoder_score:.4f} (estimated processed delay="
              f"{delay_ms:+.1f} ms; envelope correlation={alignment_correlation:.3f}; "
              f"identity control={identity_score:.4f}); {band_report}; "
              "diagnostic baselines only; no quality threshold applied")

    if failures:
        print("STOI evaluation failed for: " + ", ".join(failures), file=sys.stderr)
        return 1
    print("STOI evaluation completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
