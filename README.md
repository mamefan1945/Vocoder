# Vocoder

A real-time Windows vocoder application built with C++ and JUCE. The DSP engine is separate from the application UI so it can later be shared with a VST3 plugin target.

## Current functionality

- Windows standalone desktop application
- Selectable audio input and output devices
- Physical audio interfaces and Windows virtual audio devices
- Pass-through mode for checking audio routing
- 32-band voice-analysis and carrier filter banks
- Attack/release envelope tracking
- Polyphonic internal synthesizer carrier with on-screen and external MIDI input
- Selectable sine, saw, square, triangle and noise waveforms
- Carrier octave shift from -2 to +2
- Optional external carrier from input channel 2
- Adjustable output level
- Windows x64 GitHub Actions build

## Using the vocoder

1. Open the app and select the microphone/input and speakers/output in **Audio settings**.
2. Select **Vocoder** under Processing.
3. Select **Internal MIDI synth** to play the on-screen keyboard or a connected MIDI controller, or select **External input 2** and connect an external synth/audio source to the second input channel.
4. Choose the carrier waveform and octave. The internal synth supports 16 simultaneous voices.
4. Speak or sing into input channel 1.

For an external carrier, the selected Windows audio device must expose at least two input channels. Virtual devices such as VB-CABLE or VoiceMeeter appear in the device lists when their drivers are installed.

The internal carrier is a 16-voice polyphonic synthesizer. MIDI input devices can be selected in the MIDI input list; the on-screen keyboard is also available. Waveform and octave controls are provided.

## Build locally

Requirements:
- Windows 10/11 x64
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.22 or newer
- Git

From a Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The first configure downloads the pinned JUCE dependency from GitHub. The standalone executable is produced under `build/Vocoder_artefacts/Release/`.

## DSP design

The initial engine uses 32 logarithmically spaced band-pass filters from approximately 120 Hz to 8 kHz. The microphone signal drives per-band envelope followers; those envelopes control the corresponding filtered carrier bands. The output is the sum of the modulated carrier bands.

This is an initial functional implementation. Band spacing, filter response, envelope times, and output gain will be refined as we test it with real microphones and carrier sources.

## Dependency and licensing

JUCE is fetched at build time from its official GitHub repository and is pinned to JUCE 8.0.10. JUCE is available under the AGPLv3 or a commercial licence; review the [JUCE licensing terms](https://juce.com/legal/juce-licence/) before distributing binaries or source.

## Roadmap

1. [x] Audio device setup and pass-through
2. [x] Initial 32-band vocoder DSP
3. [x] Polyphonic MIDI carrier synth and external carrier selection
4. [ ] Attack/release, frequency range, formant shaping, dry/wet and effects controls
5. [ ] Attack/release, frequency range, formant shaping, dry/wet and effects controls
6. [ ] Spectrum display, metering and latency tuning
7. [ ] VST3 plugin target sharing the DSP engine
