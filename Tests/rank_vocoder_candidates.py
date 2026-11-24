#!/usr/bin/env python3
"""Diagnostic ranking for offline vocoder candidate renders."""
import csv
import sys
from collections import defaultdict
from pathlib import Path
import numpy as np
from pystoi import stoi
from evaluate_stoi import REFERENCE_DIR, read_mono, estimate_delay_samples, align_signals, band_envelope_correlations

outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/optimization-results")
rows = []
for speaker, stem in (("Ash", "vocoder_test_ash"), ("Nova", "vocoder_test_nova")):
    rate, reference = read_mono(REFERENCE_DIR / f"{stem}.wav")
    for path in sorted(outdir.glob(f"{speaker}_*.wav")):
        out_rate, processed = read_mono(path)
        if out_rate != rate:
            raise ValueError(f"Sample-rate mismatch: {path}")
        delay, alignment = estimate_delay_samples(reference, processed, rate)
        a, b = align_signals(reference, processed, delay)
        n = min(a.size, b.size)
        a, b = a[:n], b[:n]
        if n < rate:
            raise ValueError(f"Too little aligned audio: {path}")
        bands = band_envelope_correlations(a, b, rate)
        if not bands:
            raise ValueError(f"No band metrics: {path}")
        weighted = sum(v["correlation"] * v["reference_share"] for v in bands.values())
        mean = float(np.mean([v["correlation"] for v in bands.values()]))
        score = float(stoi(a, b, rate, extended=False))
        rms_db = 20 * np.log10(max(float(np.sqrt(np.mean(b*b))), 1e-12) /
                               max(float(np.sqrt(np.mean(a*a))), 1e-12))
        rows.append(dict(speaker=speaker, profile=path.stem[len(speaker)+1:],
                         stoi_aligned=score, band_weighted_correlation=weighted,
                         band_mean_correlation=mean, output_to_input_rms_db=rms_db,
                         estimated_delay_ms=delay*1000/rate,
                         alignment_envelope_correlation=alignment, file=path.name))
if not rows:
    raise RuntimeError(f"No candidate WAV files in {outdir}")
with (outdir / "candidate-metrics.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
groups = defaultdict(list)
for row in rows:
    groups[row["profile"]].append(row)
rank = []
for profile, values in groups.items():
    if len(values) != 2:
        continue
    rank.append(dict(profile=profile,
        diagnostic_composite=float(np.mean([.5*v["stoi_aligned"] + .5*v["band_weighted_correlation"] for v in values])),
        stoi_mean=float(np.mean([v["stoi_aligned"] for v in values])),
        band_weighted_mean=float(np.mean([v["band_weighted_correlation"] for v in values])),
        Ash_score=next(.5*v["stoi_aligned"]+.5*v["band_weighted_correlation"] for v in values if v["speaker"]=="Ash"),
        Nova_score=next(.5*v["stoi_aligned"]+.5*v["band_weighted_correlation"] for v in values if v["speaker"]=="Nova")))
rank.sort(key=lambda item: item["diagnostic_composite"], reverse=True)
with (outdir / "profile-ranking.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(rank[0]))
    writer.writeheader()
    writer.writerows(rank)
lines = ["# Vocoder candidate screening results", "",
 "Ranking is a heuristic: 50% classical STOI plus 50% reference-energy-weighted band-envelope correlation. Neither is a reliable perceptual quality score for deliberately vocoded speech. Listening is decisive.", "",
 "| Rank | Profile | Composite | STOI | Weighted band correlation |",
 "|---:|---|---:|---:|---:|"]
for index, item in enumerate(rank[:10], 1):
    lines.append(f"| {index} | {item['profile']} | {item['diagnostic_composite']:.4f} | {item['stoi_mean']:.4f} | {item['band_weighted_mean']:.4f} |")
lines += ["", "Compare Ash and Nova WAVs for each profile at matched playback levels. Reference baseline is r07_fm2: 56 bands, Q7, 10 ms attack, 7 ms release, -2 semitone formant shift, and 10% sibilance/unvoiced."]
for item in rank[:8]:
    lines.append(f"- {item['profile']}: Ash_{item['profile']}.wav and Nova_{item['profile']}.wav")
(outdir / "README-RESULTS.md").write_text("\n".join(lines), encoding="utf-8")
print("\n".join(lines))
