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
    rows: list[dict[str, float | str]] = []
    for stem in ("vocoder_test_ash", "vocoder_test_nova"):
        reference_path = REFERENCE_DIR / f"{stem}.wav"
        if not reference_path.is_file():
            print(f"ERROR: missing reference {reference_path}", file=sys.stderr)
            failures.append(stem)
            continue
        ref_rate, reference = read_mono(reference_path)
        identity_score = float(stoi(reference, reference, ref_rate, extended=False))
        if not np.isfinite(identity_score) or identity_score < 0.99:
            print(f"ERROR: STOI identity sanity check failed for {stem}: {identity_score}",
                  file=sys.stderr)
            failures.append(stem)
            continue

        for engine_name in ("classic", "lpc"):
            processed_path = RENDER_DIR / f"{stem}_{engine_name}.wav"
            if not processed_path.is_file():
                print(f"ERROR: missing {engine_name} render for {stem}: {processed_path}",
                      file=sys.stderr)
                failures.append(f"{stem}/{engine_name}")
                continue
            out_rate, processed = read_mono(processed_path)
            if ref_rate != out_rate:
                print(f"ERROR: sample-rate mismatch for {stem}/{engine_name}: "
                      f"{ref_rate} vs {out_rate}", file=sys.stderr)
                failures.append(f"{stem}/{engine_name}")
                continue
            if not np.all(np.isfinite(processed)):
                print(f"ERROR: non-finite samples in {stem}/{engine_name}", file=sys.stderr)
                failures.append(f"{stem}/{engine_name}")
                continue

            delay_samples, alignment_correlation = estimate_delay_samples(
                reference, processed, ref_rate)
            aligned_reference, aligned_processed = align_signals(
                reference, processed, delay_samples)
            length = min(aligned_reference.size, aligned_processed.size)
            aligned_reference = aligned_reference[:length]
            aligned_processed = aligned_processed[:length]
            if length < ref_rate:
                print(f"ERROR: less than one second of aligned speech for "
                      f"{stem}/{engine_name}", file=sys.stderr)
                failures.append(f"{stem}/{engine_name}")
                continue

            aligned_stoi = float(stoi(
                aligned_reference, aligned_processed, ref_rate, extended=False))
            bands = band_envelope_correlations(
                aligned_reference, aligned_processed, ref_rate)
            if not bands:
                print(f"ERROR: no usable band metrics for {stem}/{engine_name}",
                      file=sys.stderr)
                failures.append(f"{stem}/{engine_name}")
                continue
            band_mean = float(np.mean(
                [values["correlation"] for values in bands.values()]))
            band_weighted = float(sum(
                values["correlation"] * values["reference_share"]
                for values in bands.values()))
            input_rms = float(np.sqrt(np.mean(aligned_reference ** 2)))
            output_rms = float(np.sqrt(np.mean(aligned_processed ** 2)))
            output_peak = float(np.max(np.abs(aligned_processed)))
            output_crest = output_peak / max(output_rms, 1.0e-12)
            output_to_input_db = 20.0 * np.log10(
                max(output_rms, 1.0e-12) / max(input_rms, 1.0e-12))
            clipping_percent = 100.0 * float(
                np.mean(np.abs(aligned_processed) >= 0.999))
            delay_ms = delay_samples * 1000.0 / ref_rate

            if (not np.isfinite(aligned_stoi) or not -1.0 <= aligned_stoi <= 1.0
                    or not np.isfinite(band_weighted)):
                print(f"ERROR: invalid metrics for {stem}/{engine_name}",
                      file=sys.stderr)
                failures.append(f"{stem}/{engine_name}")
                continue

            row = {
                "speaker": stem.removeprefix("vocoder_test_"),
                "engine": engine_name,
                "stoi_aligned": aligned_stoi,
                "band_envelope_mean": band_mean,
                "band_envelope_weighted": band_weighted,
                "output_to_input_rms_db": float(output_to_input_db),
                "output_peak": output_peak,
                "output_crest_factor": float(output_crest),
                "clipped_sample_percent": clipping_percent,
                "estimated_delay_ms": delay_ms,
                "alignment_envelope_correlation": alignment_correlation,
            }
            rows.append(row)
            band_summary = ", ".join(
                f"{centre}Hz={values['correlation']:.3f}"
                for centre, values in bands.items())
            print(
                f"{stem} / {engine_name.upper()}: STOI={aligned_stoi:.4f}, "
                f"band-envelope weighted={band_weighted:.4f}, "
                f"band mean={band_mean:.4f}, output/input RMS={output_to_input_db:+.2f} dB, "
                f"peak={output_peak:.4f}, crest={output_crest:.2f}, "
                f"clipped={clipping_percent:.5f}%, delay={delay_ms:+.1f} ms; "
                f"bands[{band_summary}]"
            )

    if failures:
        print("Engine comparison failed for: " + ", ".join(failures), file=sys.stderr)
        return 1

    by_key = {(row["speaker"], row["engine"]): row for row in rows}
    print("\nLPC minus Classic (positive STOI/band values favour LPC):")
    for speaker in ("ash", "nova"):
        classic = by_key[(speaker, "classic")]
        lpc = by_key[(speaker, "lpc")]
        print(
            f"{speaker}: STOI {lpc['stoi_aligned'] - classic['stoi_aligned']:+.4f}; "
            f"band-envelope {lpc['band_envelope_weighted'] - classic['band_envelope_weighted']:+.4f}; "
            f"output level {lpc['output_to_input_rms_db'] - classic['output_to_input_rms_db']:+.2f} dB; "
            f"peak {lpc['output_peak'] - classic['output_peak']:+.4f}"
        )

    if rows:
        import csv
        output_csv = RENDER_DIR / "engine-comparison.csv"
        with output_csv.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"\nMachine-readable comparison: {output_csv}")
    print("\nEngine comparison completed. STOI and band-envelope metrics are diagnostic proxies, "
          "not a complete perceptual-quality score; review paired WAVs before choosing a winner.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
