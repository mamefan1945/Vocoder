# PrismaVox DSP regression tests

The project has two deterministic, device-independent DSP test executables:

- `HarmonizerDSPTests` exercises TD-PSOLA, LP-PSOLA, and FD-PSOLA pitch shifting, source-pitch leakage, pitch bend, polyphony, speech-file processing, algorithm switching, silence/no-MIDI gating, sustained-level stability, and an FD-PSOLA polyphony performance profile.
- `VocoderDSPTests` checks silence and missing-carrier behavior, dry/wet endpoint behavior, repeatability after reset, finite/bounded output across sample rates and block sizes, and live band-count/filter-Q configuration changes. It also processes the two checked-in reference speech recordings (`vocoder_test_ash.wav` and `vocoder_test_nova.wav`) through a deterministic harmonically rich carrier and reports input/wet RMS, wet peak, crest factor, output energy above 2.5 kHz, and the fraction of active 20 ms speech frames. These checks catch silence, clipping, non-finite output, and loss of all upper-band activity on real speech material. The same test renders are written to `build/test-renders` and evaluated by `Tests/evaluate_stoi.py` using the MIT-licensed `pystoi` implementation. CI reports classical STOI scores against each original recording and verifies the metric identity control; scores are not yet pass/fail thresholds.

Both are registered with CTest and run by the Windows GitHub Actions build:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --verbose --output-on-failure
```

## Test scope and limitations

These tests call the DSP engines directly. They do not require a microphone, audio interface, MIDI device, or GUI, which makes them repeatable in CI.

A passing test means the measured behavior met its explicit numerical assertions. The speech-file metrics are signal-integrity and activity indicators, not an intelligibility score: upper-band energy can come from carrier harmonics/noise and does not prove consonants are preserved perceptually. They do **not** prove that a voice sounds natural, that consonants are intelligible, or that a harmony is musically convincing. Those require controlled, level-matched listening tests using the reference recordings.

Performance timings printed by the harmonizer tests are diagnostic measurements, not yet hard real-time pass/fail limits. CI virtual-machine timing is variable; establish stable baselines before adding deadline gates. Likewise, these offline tests do not measure device round-trip latency, driver dropouts, or the acoustic noise floor of a physical setup.

## Next measurement work

The next useful additions are listening-backed STOI baselines and regression margins, calibrated THD+N and noise-floor analysis, true-peak measurement, pitch/formant tracking over a wider vocal range, and golden-render comparisons. Hardware loopback tests should remain a separate opt-in test tier.
