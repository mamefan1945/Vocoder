# PrismaVox DSP regression tests

The project has two deterministic, device-independent DSP test executables:

- `HarmonizerDSPTests` exercises TD-PSOLA, LP-PSOLA, and FD-PSOLA pitch shifting, source-pitch leakage, pitch bend, polyphony, speech-file processing, algorithm switching, silence/no-MIDI gating, sustained-level stability, and an FD-PSOLA polyphony performance profile.
- `VocoderDSPTests` checks silence and missing-carrier behavior, dry/wet endpoint behavior, repeatability after reset, finite/bounded output across sample rates and block sizes, and live band-count/filter-Q configuration changes. It also processes the two checked-in reference speech recordings (`vocoder_test_ash.wav` and `vocoder_test_nova.wav`) through a deterministic harmonically rich carrier and reports input/wet RMS, wet peak, crest factor, output energy above 2.5 kHz, and the fraction of active 20 ms speech frames. These checks catch silence, clipping, non-finite output, and loss of all upper-band activity on real speech material. The speech-corpus render uses a representative reduced-noise profile: 10 ms attack and release, -2 semitones of formant shift, maximum 56 filter bands, filter Q 7, and 10% unvoiced-noise and sibilance amounts (the latter two are normalized to 0.10 in the engine). Other engine defaults remain unchanged unless explicitly set by the test. The same test renders are written to `build/test-renders` and evaluated by `Tests/evaluate_stoi.py` using the MIT-licensed `pystoi` implementation. CI estimates relative timing from 10 ms RMS envelopes (searching within ±200 ms), then reports both unaligned and delay-aligned classical STOI scores, the estimated delay, envelope-correlation value, and metric identity control. Reporting both scores makes it possible to see whether envelope-based alignment actually helps each recording. The evaluator also reports per-band speech-envelope correlations (160 Hz to 6.3 kHz), comparing 20 ms RMS energy contours in matching analysis bands. For each recording it reports the reference speaker's share of energy in each measured band, the processed/reference band-level change in dB, and an energy-weighted correlation (bands with more reference energy contribute more). This makes comparisons more speaker-relative when the two reference voices have different spectral distributions. These remain vocoder-specific diagnostics of speech-energy tracking, not perceptual intelligibility scores; carrier harmonics, synthesis-band overlap, and the finite set of measured bands can affect them. STOI and band-envelope scores are diagnostic only and are not yet pass/fail thresholds.

Both are registered with CTest and run by the Windows GitHub Actions build:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --verbose --output-on-failure
```

## Test scope and limitations

These tests call the DSP engines directly. They do not require a microphone, audio interface, MIDI device, or GUI, which makes them repeatable in CI.

A passing test means the measured behavior met its explicit numerical assertions. The speech-file RMS/activity metrics and band-envelope correlations are signal diagnostics, not proof of intelligibility: upper-band energy can come from carrier harmonics/noise, and envelope correlation does not establish that consonants are perceptually identifiable. STOI is a reference-based intelligibility estimate, but its interpretation is limited when the output is deliberately vocoded rather than a degraded copy of the original voice. They do **not** prove that a voice sounds natural, that consonants are intelligible, or that a harmony is musically convincing. Those require controlled, level-matched listening tests using the reference recordings.

Performance timings printed by the harmonizer tests are diagnostic measurements, not yet hard real-time pass/fail limits. CI virtual-machine timing is variable; establish stable baselines before adding deadline gates. Likewise, these offline tests do not measure device round-trip latency, driver dropouts, or the acoustic noise floor of a physical setup.

## Next measurement work

The next useful additions are listening-backed STOI baselines and regression margins, calibrated THD+N and noise-floor analysis, true-peak measurement, pitch/formant tracking over a wider vocal range, and golden-render comparisons. Hardware loopback tests should remain a separate opt-in test tier.


## Hosted vocoder parameter search

The Windows workflow renders a baseline and one-factor-at-a-time probes for filter
bands, filter Q, attack, release, formant shift, sibilance, unvoiced noise, and
articulation lift using both checked-in speaker recordings. It publishes WAVs,
per-speaker metrics, and a shortlist as a separate GitHub Actions artifact.

The ranking is a screening heuristic (50% classical STOI and 50% reference-energy-
weighted band-envelope correlation), not a perceptual quality claim. These metrics
can mis-rank deliberately vocoded speech. Listen to shortlisted, level-matched
Ash and Nova files before changing application defaults.
