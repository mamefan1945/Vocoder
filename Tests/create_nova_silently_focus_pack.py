#!/usr/bin/env python3
"""Create a level-matched, focused Nova consonant A/B listening pack."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import imageio_ffmpeg
import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "TestAudio" / "vocoder_test_nova.wav"
START_SECONDS = 1.25
END_SECONDS = 2.55
PROFILES = [
    "r07_fm2",
    "wide_r01", "wide_r20", "wide_r50",
    "attack_a01", "attack_a30",
    "articulation_0", "articulation_2",
    "unvoiced_0", "unvoiced_2",
    "plosive_off", "plosive_on",
    "vowel_q4", "vowel_q10", "vowel_f0", "vowel_q10_f0",
    "carrier_saw_plosive_off", "carrier_saw_plosive_on",
    "carrier_square_plosive_off", "carrier_square_plosive_on",
]

def read_float(path: Path) -> tuple[int, np.ndarray]:
    rate, data = wavfile.read(path)
    if data.ndim == 2:
        data = data[:, 0]
    if np.issubdtype(data.dtype, np.integer):
        info = np.iinfo(data.dtype)
        data = data.astype(np.float64) / max(abs(info.min), abs(info.max))
    else:
        data = data.astype(np.float64)
    return int(rate), data

def main() -> int:
    results = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "optimization-results"
    if not SOURCE.is_file():
        raise FileNotFoundError(SOURCE)
    rate, original = read_float(SOURCE)
    start = int(round(START_SECONDS * rate))
    end = int(round(END_SECONDS * rate))
    reference = original[start:end]
    if reference.size == 0:
        raise RuntimeError("Focused source segment is empty.")
    target_rms = float(np.sqrt(np.mean(reference * reference)))
    out = results / "nova-silently-focus"
    out.mkdir(parents=True, exist_ok=True)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    items = [("original_dry", reference)]
    for profile in PROFILES:
        path = results / f"Nova_{profile}.wav"
        if not path.is_file():
            raise FileNotFoundError(path)
        candidate_rate, samples = read_float(path)
        if candidate_rate != rate:
            raise ValueError(f"Sample-rate mismatch for {path.name}")
        segment = samples[start:end].copy()
        rms = float(np.sqrt(np.mean(segment * segment)))
        if rms > 1.0e-12 and target_rms > 1.0e-12:
            segment *= target_rms / rms
        items.append((profile, segment))
    for name, samples in items:
        pcm = np.clip(samples, -1.0, 1.0)
        wav_path = out / f"{name}.wav"
        wavfile.write(wav_path, rate, np.round(pcm * 32767.0).astype(np.int16))
        mp3_path = out / f"{name}.mp3"
        subprocess.run([
            ffmpeg, "-y", "-hide_banner", "-loglevel", "error",
            "-i", str(wav_path), "-map_metadata", "-1",
            "-codec:a", "libmp3lame", "-b:a", "192k", str(mp3_path)
        ], check=True)
    (out / "LISTENING-ORDER.txt").write_text(
        "NOVA — SILENTLY CONSONANT TEST (1.25–2.55 seconds)\n"
        "==================================================\n\n"
        "All clips cover the same 1.30-second section around the word 'silently'. "
        "The dry original is first. Candidate clips are level-matched to the dry "
        "segment by whole-segment RMS; do not change playback volume between files.\n\n"
        "Reference: r07_fm2 (10 ms attack, 7 ms release, -2 semitone formant, "
        "56 bands, Q7, 10% sibilance/unvoiced, normal articulation lift).\n\n"
        "wide_r01/r20/r50 change only release to 1/20/50 ms. "
        "attack_a01/a30 change only attack to 1/30 ms. "
        "articulation_0/2 change only articulation lift to 0/200%. "
        "unvoiced_0/2 change only unvoiced-noise amount to 0/200%. "
        "plosive_off/on compare the new short stop-release burst path disabled/enabled. "
        "vowel_q4/q10 compare filter Q4/Q10 at the -2 semitone formant setting; "
        "vowel_f0 compares zero formant shift at Q7; vowel_q10_f0 combines Q10 with zero shift. "
        "These four isolate spectral resolution and static formant offset while attack/release stay fixed. "
        "These are diagnostic comparisons, not recommended settings.\n\n"
        "For the vowel question, compare r07_fm2, vowel_q4, vowel_q10, vowel_f0, and vowel_q10_f0 first. "
        "Listen for whether the /aɪ/ glide is more distinct rather than flattened toward /ɛ/. "
        "Then compare the plosive_off/on pair for the /t/ release. "
        "Listen for the middle sequence /n-t-l/ as well. "
        "Report whether any candidate makes 'silently' clearer, worse, or simply "
        "different. The optimizer carrier is a deterministic harmonic test tone, "
        "not the exact live Ash/Square carrier. Listening judgment is decisive.\n",
        encoding="utf-8"
    )
    print(f"Created {len(items)} focused WAV/MP3 pairs in {out}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
