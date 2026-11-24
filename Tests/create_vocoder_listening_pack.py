#!/usr/bin/env python3
"""Create a compact MP3 listening pack from the offline vocoder candidates."""

from __future__ import annotations

import csv
import subprocess
import sys
import zipfile
from pathlib import Path

import imageio_ffmpeg


def main() -> int:
    results = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("build/optimization-results")
    ranking = results / "profile-ranking.csv"
    if not ranking.is_file():
        raise FileNotFoundError(f"Missing candidate ranking: {ranking}")

    with ranking.open(newline="", encoding="utf-8") as stream:
        ranked_profiles = [row["profile"] for row in csv.DictReader(stream)]

    # Keep the four highest diagnostic profiles plus the optimizer's fixed
    # reference profile. Include all three carrier-waveform comparisons.
    selected_profiles = list(dict.fromkeys(["r07_fm2", *ranked_profiles[:4]]))
    selected_wavs = []
    for speaker in ("Ash", "Nova"):
        for profile in selected_profiles:
            candidate = results / f"{speaker}_{profile}.wav"
            if candidate.is_file():
                selected_wavs.append(candidate)
        selected_wavs.extend(sorted(results.glob(f"{speaker}_carrier_*.wav")))

    if not selected_wavs:
        raise RuntimeError(f"No candidate WAV files found in {results}")

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    mp3_dir = results / "listening-mp3"
    mp3_dir.mkdir(parents=True, exist_ok=True)
    converted = []
    for wav in selected_wavs:
        mp3 = mp3_dir / f"{wav.stem}.mp3"
        command = [
            ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
            "-i", str(wav), "-map_metadata", "-1",
            "-codec:a", "libmp3lame", "-b:a", "192k",
            "-id3v2_version", "3", str(mp3),
        ]
        subprocess.run(command, check=True)
        converted.append(mp3)

    guide = mp3_dir / "LISTENING-GUIDE.txt"
    guide.write_text(
        "PRISMAVOX VOCODER LISTENING PACK\n"
        "================================\n\n"
        "Files are 192 kbps MP3 listening copies. The original WAV renders "
        "remain available in the optimization-results artifact.\n\n"
        "Ash and Nova identify the two source speech recordings; they do not "
        "refer to the app's Ash carrier preset. The parameter-profile renders "
        "use the optimizer's deterministic harmonic test carrier, not the "
        "exact live Ash/Square setup. The carrier comparison renders use the "
        "built-in Rich, Analog Stack, and Saw oscillators.\n\n"
        "r07_fm2 is the optimizer reference profile: 56 bands, Q 7, 10 ms "
        "attack, 7 ms release, -2 semitone formant shift, and 10% sibilance "
        "and unvoiced-noise amounts. Other profiles are diagnostic candidates "
        "ranked by objective metrics, not confirmed sound-quality winners.\n\n"
        "Compare files with the same speaker name at the same playback level. "
        "Listen for consonant clarity, vowel smoothness, hiss, buzzing, and "
        "abrupt transitions. The objective ranking is only a screening aid; "
        "your listening judgment is decisive.\n",
        encoding="utf-8",
    )

    archive = results / "PrismaVox-Vocoder-Listening-Pack.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for mp3 in converted:
            bundle.write(mp3, mp3.name)
        bundle.write(guide, guide.name)
        for report in (results / "README-RESULTS.md", ranking,
                       results / "candidate-metrics.csv"):
            if report.is_file():
                bundle.write(report, report.name)

    print(f"Created {len(converted)} MP3 files at 192 kbps.")
    print(f"Listening pack: {archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
