# Vocoder

A real-time Windows vocoder application built with C++ and JUCE. The project is structured so the DSP engine can later be shared with a VST3 plugin target.

## Current milestone

- Windows standalone desktop application
- Selectable audio input and output devices (including physical interfaces and virtual audio devices exposed by Windows)
- Real-time input-to-output pass-through
- Adjustable output level
- GitHub Actions build for Windows x64

This is the audio-I/O foundation. The vocoder filter bank, internal MIDI-playable carrier synthesizer, external carrier input/mixing, and VST3 target are planned next.

## Build locally

Requirements:
- Windows 10/11 x64
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.22 or newer
- Git

Configure and build from a Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The first configure downloads the pinned JUCE dependency from GitHub. The standalone executable is produced under the build directory.

## Audio routing

Choose the microphone/input and speakers/output in the application's **Audio settings** panel. Virtual audio devices installed in Windows (for example, VB-CABLE or VoiceMeeter devices) appear in the same lists when their drivers are installed.

The current pass-through stage is intended for checking device selection and latency before the vocoder DSP is added.

## Dependency and licensing

JUCE is fetched at build time from its official GitHub repository and is currently pinned to the JUCE 8.0.10 tag. JUCE is available under the AGPLv3 or a commercial licence; review the [JUCE licensing terms](https://juce.com/legal/juce-licence/) before distributing binaries or source.

## Planned milestones

1. Audio device setup and stable low-latency pass-through
2. Vocoder DSP engine (32-band filter bank and envelope followers)
3. Internal polyphonic carrier synth with MIDI input
4. External carrier input and selectable/mixable carrier sources
5. Formant shaping, attack/release, frequency range, dry/wet and effects
6. VST3 plugin target sharing the same DSP engine
