# Vocoder

A real-time Windows voice-effects application built with C++17, JUCE 8.0.10, and FluidSynth. It currently provides three processing modes:

- **Pass-through** — monitor the microphone/input directly.
- **Vocoder** — use the voice as a modulator to shape a musical carrier.
- **Harmonizer** — use the voice as the pitch source and MIDI notes as target harmony pitches.

The application is a standalone Windows x64 program. The DSP engines are separated from most of the UI/audio-device code, with a view to possible future reuse in a plug-in. A VST3 plug-in is **not currently provided**.

> **Development status:** this is an actively developed instrument, not a finished commercial processor. The harmonizer uses a custom, early-stage time-domain PSOLA implementation. It can produce useful pitch-following harmony, but artifacts, roughness, crackling, tracking errors, and polyphony-related quality changes are still being evaluated. A successful build confirms compilation and packaging; it does not certify audio quality.

## Contents

- [Features](#features)
- [Download and run](#download-and-run)
- [Audio setup and signal routing](#audio-setup-and-signal-routing)
- [Using the application](#using-the-application)
- [Vocoder concepts and signal path](#vocoder-concepts-and-signal-path)
- [Harmonizer concepts and signal path](#harmonizer-concepts-and-signal-path)
- [Testing and troubleshooting](#testing-and-troubleshooting)
- [Build from source](#build-from-source)
- [Project layout](#project-layout)
- [Dependencies and licences](#dependencies-and-licences)
- [Known limitations and development notes](#known-limitations-and-development-notes)

## Features

- Windows standalone desktop application.
- Selectable audio input/output devices and device buffer settings through JUCE's audio-device selector.
- Pass-through, vocoder, and MIDI-controlled harmonizer modes.
- Microphone input on input channel 1; optional external carrier on input channel 2.
- 32-band vocoder analysis/synthesis filter banks.
- Adjustable vocoder attack/release, frequency range, formant shift, and noise-gate threshold.
- Internal polyphonic oscillator synthesizer: sine, saw, square, triangle, noise, rich PWM, and choir-like waveforms.
- FluidSynth SoundFont playback, with an instrument list populated from the loaded SoundFont.
- Bundled GeneralUser GS SoundFont, plus support for loading another SF2 file.
- On-screen keyboard covering MIDI notes 36–108 (C2–C8).
- External MIDI input selection.
- Octave shift from -2 to +2 octaves.
- Output level, dry/wet control, input/output peak meters, microphone monitor, and carrier preview.
- Windows x64 build through GitHub Actions.

## Download and run

Prebuilt Windows packages are published as the **Vocoder-Windows-x64** artifact on successful GitHub Actions runs:

1. Open the repository's [Actions page](https://github.com/mamefan1945/Vocoder/actions).
2. Select a successful **Windows build** run for the commit you want.
3. Download the Vocoder-Windows-x64 artifact.
4. Extract the complete artifact to a folder.
5. Run Vocoder.exe from that folder.

Keep the package contents together. Do not separate the executable from its SoundFonts directory or the FluidSynth runtime DLLs. The application looks for the default SoundFont relative to the executable.

The artifact is a build output, not an installer. It does not install an audio driver or configure Windows audio devices. The workflow currently retains artifacts for 14 days, so older run artifacts may expire.

## Audio setup and signal routing

### Basic microphone setup

1. Connect your microphone or audio interface.
2. Start the application.
3. In the device settings at the bottom of the window, choose the desired audio device, input, output, sample rate, and buffer size.
4. Confirm that the input meter responds when you speak.
5. Select **Pass-through** and confirm that you can hear the input.
6. Reduce speaker volume or use headphones while setting levels to avoid feedback.

The application requests up to two input channels and two output channels. The first input channel is treated as the voice/microphone. If an external carrier is selected, the second input channel is used as that carrier.

For an audio interface with multiple inputs, connect the microphone to the channel that the device exposes as input channel 1. Route an external synth or other carrier source to input channel 2. The exact physical jack-to-channel mapping depends on the interface and its driver.

### Internal carrier versus external carrier

The **Carrier** selector has two choices:

- **Internal MIDI synth** — the application generates the carrier from the on-screen keyboard or the selected external MIDI input. This is the usual starting point.
- **External input 2** — the carrier comes from the second audio input channel. The selected audio device must expose that channel.

External-carrier mode is relevant to the vocoder. The harmonizer does not use a carrier signal; it pitch-shifts the microphone input to the MIDI note pitches.

### USB mixers and virtual audio devices

A USB mixer may expose its main mix as a stereo input rather than exposing each channel independently. If the microphone and external carrier are not available as separate inputs, check the mixer's USB routing and driver configuration. Virtual audio devices such as VB-CABLE or VoiceMeeter can appear in the device list when installed, but their routing must be configured separately.

### Avoiding feedback

When using speakers, the microphone can pick up the processed output and feed it back into the application. Headphones are recommended for initial testing. Start with conservative input preamp and output levels. The application's output control is not a substitute for correct gain staging.

## Using the application

### Main controls

| Control | Function |
|---|---|
| **Processing** | Select Pass-through, Vocoder, or Harmonizer. |
| **Carrier** | Select the internal MIDI synth or external audio input 2. Used by the vocoder. |
| **MIDI input** | Select a connected MIDI input, or No MIDI input to use only the on-screen keyboard. |
| **Carrier type** | Select an oscillator waveform or SoundFont (GM). |
| **Instrument** | Select a preset from the loaded SoundFont. Visible when SoundFont (GM) is selected. |
| **Load SF2...** | Load another SoundFont file. |
| **Octave** | Shift internal carrier notes and harmonizer target notes by -2 to +2 octaves. |
| **Output level** | Master output gain, from 0 to 2.0. |
| **Attack / Release** | Vocoder envelope response times, in milliseconds. |
| **Low freq / High freq** | Lower and upper limits of the vocoder's analysis/synthesis band range. |
| **Dry / wet** | Blend the original input with the processed signal. In harmonizer mode, this blends delayed dry voice with the harmony output. |
| **Formant** | Shift the vocoder's analysis-envelope positions by semitones. This is an approximate spectral/formant adjustment, not a full formant-preserving pitch shifter. |
| **Noise gate** | Set the per-band RMS threshold used by the vocoder to suppress low-level input noise. |
| **Input / Output meters** | Display peak levels in dBFS. |
| **Mic monitor** | In vocoder mode, add the direct microphone signal to the processed output. Disable it when judging the vocoder alone. |
| **Carrier preview** | Audition the selected carrier directly, independently of the vocoder. Not used in harmonizer mode. |
| **On-screen keyboard** | Play notes with the mouse. MIDI notes 36–108 are available. |
| **Audio device settings** | Select the audio device and configure its available inputs, outputs, sample rate, and buffer size. |

The input and output meters show peak levels, not loudness or true-peak measurements. The output meter reflects the combined signal sent to the output.

### Playing the on-screen keyboard

- Left-click and hold a key to play it; release the mouse button to send note-off.
- Right-click a key to toggle it into a **locked** state. A locked key stays held after the mouse is released.
- Right-click the same key again to unlock it and send note-off.
- You can hold multiple keys to create a chord.
- The keyboard range is C2 through C8.

The lock feature is useful for holding a chord while speaking or singing into the microphone. If a key seems stuck, right-click that key again or send All Notes Off from the MIDI controller.

### Using an external MIDI controller

1. Connect the controller to the computer with its USB-MIDI connection (or a standard MIDI interface) and power it on.
2. Click **Refresh** beside **MIDI input** if the controller was connected after the application started.
3. Choose the controller from **MIDI input**.
4. Select **Harmonizer** and play notes. Hold multiple keys for a chord; release keys to stop their harmony voices.

For an Alesis Recital, connect its USB port directly to the computer with a USB data cable. Windows should enumerate it as a MIDI input device; it does not send audio to the application over USB. If it does not appear, check Windows Device Manager, verify that the cable supports data (not charging only), reconnect the keyboard, and click **Refresh**.

Incoming MIDI note-on and note-off messages are routed into the same keyboard/MIDI state used by the on-screen keyboard. Note velocity controls the level of each harmonizer voice and the internal/SoundFont carrier note. MIDI pitch bend is supported with a ±2-semitone range; in Harmonizer mode it smoothly bends all active harmony voices, and in carrier-synth mode it bends active oscillator voices. The harmonizer supports up to eight simultaneous note voices. The internal vocoder carrier synthesizer has 16 voices.

### SoundFont instruments

1. Select **Vocoder** as the processing mode.
2. Select **Internal MIDI synth** as the carrier.
3. Select **SoundFont (GM)** under Carrier type.
4. Choose an instrument from the Instrument list.
5. Play the on-screen keyboard or an external MIDI controller.

The bundled GeneralUser GS bank is loaded from the SoundFonts/GeneralUser-GS.sf2 file next to the executable. To use another bank, select **Load SF2...** and choose a SoundFont file. The instrument list is rebuilt from the presets found in that file. SoundFont banks vary in preset naming, bank layout, sample quality, and CPU/memory requirements.

The SoundFont engine uses FluidSynth. Reverb and chorus are disabled in the current FluidSynth configuration. SoundFont playback is a carrier source for vocoding; it is not the harmonizer's pitch-shifting engine.

### A quick first-use setup

For a simple vocoder test:

1. Select **Vocoder**.
2. Select **Internal MIDI synth**.
3. Select **Saw** as the carrier type.
4. Leave Octave at **0**.
5. Enable **Carrier preview** briefly and play a key to confirm the carrier is audible. Disable preview before evaluating the vocoder.
6. Hold a single key, speak into the microphone, and watch the input/output meters.
7. Try a few different carrier waveforms and SoundFont presets.

For a simple harmonizer test:

1. Select **Harmonizer**.
2. Select **No MIDI input** if you intend to use the on-screen keyboard.
3. Hold one keyboard note.
4. Sing a steady vowel into the microphone.
5. Add a second, third, and fourth key while keeping the sung note and microphone level as constant as possible.
6. Try the Octave selector and the Dry / wet control.

## Vocoder concepts and signal path

A vocoder does not normally shift the pitch of the voice. Instead, it measures the changing spectral shape of the voice and applies that shape to a separate sound source called the **carrier**.

- **Modulator:** the microphone signal. It supplies the speech articulation and changing spectral envelope.
- **Carrier:** the synth, SoundFont, or external audio signal. It supplies the sound that is shaped by the voice.
- **Analysis:** a bank of filters separates the voice into frequency regions and measures the energy in each region.
- **Synthesis:** a corresponding bank of filters separates the carrier. The measured voice energy controls how strongly each carrier band contributes to the output.

### Current implementation

The vocoder uses 32 logarithmically spaced analysis and synthesis bands spanning approximately 120 Hz to 8 kHz. The centre frequencies are distributed logarithmically rather than linearly, giving more resolution in the lower frequency range where speech formants and many important voice characteristics occur.

For each audio sample:

1. The microphone signal is processed by the analysis filter bank.
2. Each analysis band's squared output is smoothed with an attack or release coefficient to estimate band power.
3. The square root of the smoothed power produces an RMS-like envelope for that band.
4. The selected carrier is processed by the synthesis filter bank.
5. The envelope controls the corresponding carrier band.
6. The modulated bands are summed to form the wet vocoder output.

A square-root modulation curve and a per-band gate are used to make quieter speech bands useful while suppressing low-level background noise. A gentle high-frequency articulation lift helps retain consonant detail.

Unvoiced consonants such as S, SH, F, and TH are difficult for a purely tonal carrier to reproduce. The current implementation estimates unvoiced activity using a smoothed zero-crossing measure and adds separately filtered noise excitation, weighted toward the upper frequency bands. That noise path is gated by carrier activity so it does not act as a standalone noise generator when no carrier is present.

The **Formant** control offsets the envelope lookup positions across the filter bank. This changes the spectral shape applied to the carrier. It is an approximate formant/spectral shift; it does not perform a high-quality independent formant-preserving transformation.

### What affects vocoder intelligibility?

- Microphone clarity and a clean, strong input level.
- Correct input routing and avoiding clipping.
- Carrier harmonic content. A saw or rich harmonic waveform is often a useful starting point.
- Attack and release times. Very slow settings can blur consonants; very fast settings can sound rough or unstable.
- The low/high frequency limits. Removing too much high-frequency content can make consonants indistinct.
- Noise-gate threshold. Too high a threshold can remove quiet speech detail; too low a threshold can let room noise through.
- Monitoring. Turn **Mic monitor** off when evaluating the processed vocoder signal by itself.

## Harmonizer concepts and signal path

The harmonizer works differently from the vocoder. It does not use a carrier synth. It estimates the fundamental pitch of the microphone signal, then uses held MIDI notes as the target pitches for one or more output voices.

For example, if the sung input is A3 and the MIDI keyboard holds C4 and E4, the engine attempts to produce two separate output voices at C4 and E4. The MIDI notes specify the output pitches; the singer supplies the source audio and articulation.

This is a **MIDI-notes harmonizer** rather than an automatic diatonic harmonizer. It does not infer a key or choose harmony intervals for you. The notes you hold determine the pitches.

### Current implementation: time-domain PSOLA

The harmonizer currently uses a custom implementation of **Time-Domain Pitch-Synchronous Overlap-Add (TD-PSOLA)**.

PSOLA changes pitch by cutting a voiced waveform into short overlapping segments, or **grains**, centred around detected pitch marks. It places those grains at new time positions:

- The spacing of source pitch marks follows the singer's detected fundamental period.
- The spacing of synthesis marks follows the target MIDI note's period.
- The grain waveform comes from the original voice, preserving much of its natural spectral character.
- Overlapping grains are combined with a window and normalized by their accumulated window weights.

The engine estimates pitch with a YIN-style cumulative-mean normalized difference function over a decimated analysis window. It selects the first sufficiently confident period trough rather than simply choosing the strongest autocorrelation peak, reducing octave/subharmonic errors on harmonic voices. Pitch estimation is updated periodically and smoothed over time. Pitch-mark timing is then refined from the measured spacing between successive same-polarity waveform peaks, with implausible half/double-cycle jumps rejected. This phase-locked timing helps keep grain placement aligned with the actual input waveform.

Each held MIDI note owns an independent harmony voice with its own synthesis-mark schedule, overlap buffer, note velocity, and release envelope. The current engine supports eight simultaneous harmony voices. Octave shift is applied to the target MIDI notes.

### Formant preservation and transient protection

The harmonizer now separates the input into an LPC residual and a time-varying vocal-tract spectral envelope. A windowed, autocorrelation-based LPC analysis estimates a stable all-pole envelope; the residual is pitch-shifted with TD-PSOLA, then each harmony voice is reconstructed through the delayed envelope coefficients. Coefficients are bandwidth-expanded and smoothed to reduce unstable resonances and zippering. The coefficient history is aligned to the same 40 ms delayed timeline as the PSOLA grains.

This is an always-on formant-preservation stage, not a formant-shift control. It is intended to retain the singer's vowel character when target notes differ substantially from the source pitch. LPC is an approximation: difficult input (strong breathiness, distortion, room resonances, very low level, or overlapping sources) can still produce coloration, and listening tests are required before treating it as transparent.

A transient detector also monitors short-term energy rise and waveform slope. During detected attacks it temporarily stops scheduling periodic grains and crossfades toward the latency-aligned direct signal. This is intended to keep plosive consonants and sharp onsets from being stretched or smeared by the periodic grain train. The detector is deliberately short and adaptive; it is not a general-purpose transient shaper and may need tuning against real vocal recordings.

The engine uses approximately 40 ms of lookahead to give the pitch-mark detector access to samples around the expected mark. This contributes to audible latency. The actual end-to-end latency also depends on the audio device, driver, and selected buffer size.

Unvoiced sounds do not have a stable fundamental period to transpose. The current implementation blends a delayed copy of the microphone input into the output when the pitch detector considers the signal unvoiced. That shared consonant path avoids duplicating the same S/SH/F noise once per harmony voice.

### Important PSOLA limitations

The current harmonizer is experimental. PSOLA quality depends heavily on reliable pitch tracking and well-placed pitch marks. Breathiness, noisy input, vibrato, fast pitch movement, low signal level, strong harmonics, and abrupt consonants can make pitch detection or grain placement less reliable.

Potential audible artifacts include:

- Rough or buzzy sustained notes.
- Crackles or discontinuities when pitch marks jump or become unstable.
- A change in tone as additional harmony notes are added.
- Unnatural transitions between voiced vowels and unvoiced consonants.
- Pitch-tracking errors on some voices or notes.
- Increased CPU use as more voices are active.

The code includes ongoing improvements to pitch-mark polarity, source-grain selection, and chord consonant handling, but these do not make the engine equivalent to a mature commercial harmonizer. The implementation should be judged by listening tests, not by the algorithm name or build result.

## Testing and troubleshooting

A repeatable test is much more useful than changing several controls at once. Keep the microphone, gain, sample rate, and audio buffer unchanged while comparing builds.

### Basic audio and routing tests

| Test | Procedure | What it checks |
|---|---|---|
| Input | Speak with Pass-through selected and watch the input meter. | Microphone/device input is reaching the application. |
| Output | Use Pass-through and listen on the selected output. | Output device and basic routing. |
| Internal carrier | Select Vocoder, enable Carrier preview, hold one key. | Internal synth, MIDI path, and output. Disable preview afterward. |
| External carrier | Select External input 2 and feed a known signal to the second input. | Second-channel routing. |
| MIDI | Select the controller and play several notes. | MIDI device selection and note-on/note-off handling. |
| SoundFont | Select SoundFont (GM), choose a preset, and play notes. | FluidSynth and SoundFont loading/playback. |

### Vocoder listening test

Use the same spoken material for each comparison:

1. Speak the numbers **one** through **ten** at a steady pace.
2. Then sustain or clearly articulate **A, E, I, O, U**.
3. Repeat with the same microphone distance and level.
4. Compare carrier waveforms and a few SoundFont instruments.
5. Listen for whether consonants remain distinct and whether the vowel identities can be recognized.

For a more objective check, record the output and ask someone who does not know the spoken words to transcribe it. A listener who already knows the script can unintentionally fill in missing syllables.

### Harmonizer listening test

Use a steady sung vowel so that changes are easier to hear:

1. Select Harmonizer and hold one MIDI key.
2. Sustain a comfortable vowel at a steady pitch and volume.
3. Record or listen to the one-note result.
4. Without changing the sung note, add a second key.
5. Add a third and fourth key one at a time.
6. Compare the output after each added note.
7. Repeat with a different target interval and with the octave control at 0, +1, and -1.
8. Repeat using a short spoken phrase that includes both vowels and consonants.

Pay attention to:
- Crackling or clicks.
- Whether the sung pitch is tracked steadily.
- Whether each held key produces a distinct target pitch.
- Whether the original vocal tone changes as notes are added.
- Whether consonants remain intelligible.
- Whether note release is smooth.

When reporting a problem, include the sample rate, audio buffer size, number of held notes, sung pitch range, and whether the problem occurs on a sustained vowel, a spoken phrase, or both. A short unprocessed microphone recording and a matching processed recording are especially useful.

### Recording a test

A DAW or audio editor such as Audacity can record the application's output. Select the correct Windows recording source (for example, the audio interface loopback or a configured virtual audio cable). Do not assume that the microphone input is the same as the application's output recording source.

For a fair A/B comparison:
- Record the same performance through each build.
- Keep the input gain and output level fixed.
- Avoid normalization or other effects until after the comparison.
- Keep a copy of the dry microphone recording as a reference.

### Common problems

**No input meter activity**
- Confirm the selected audio device and enabled input channels.
- Check Windows microphone privacy permissions and the device's hardware gain.
- Verify that the microphone is connected to the physical input mapped to channel 1.

**Input meter moves, but no sound in Pass-through**
- Check the selected output device, output channel routing, Windows output volume, and interface monitor controls.
- Test with headphones to rule out speaker routing or feedback.

**Vocoder output is silent**
- Confirm Vocoder mode is selected.
- Confirm the carrier source is correct.
- For the internal synth, hold a key and verify the carrier with Carrier preview.
- For External input 2, confirm the audio device exposes a second input and that a signal reaches it.
- Check that the Noise gate is not set too high and that the frequency range is valid.

**Harmonizer output is silent or does not follow the keys**
- Confirm Harmonizer mode is selected and a MIDI note is held.
- Try the on-screen keyboard to isolate external MIDI configuration.
- Check the microphone input meter and sing a clear sustained vowel.
- The harmonizer requires a detectable voiced pitch; unvoiced speech does not provide a stable pitch to transpose.

**Crackling, dropouts, or pulsing**
- First determine whether the problem is present in Pass-through. If it is, investigate the audio device, driver, USB connection, Windows audio enhancements, buffer size, and other applications using the device before attributing it to the DSP.
- Try a larger audio buffer to check for device/CPU underruns. This may increase latency.
- Compare one harmony note against several notes. If the issue grows with polyphony only in Harmonizer mode, capture a repeatable test recording and report the number of notes.
- Avoid changing multiple DSP controls at once; it makes the cause harder to isolate.

**SoundFont instruments are missing**
- Confirm that the SoundFonts folder is beside Vocoder.exe.
- Confirm the selected SF2 file loads successfully.
- Some banks use unusual preset/bank layouts or contain no selectable presets.

## Build from source

### Requirements

- Windows 10 or Windows 11, x64.
- Visual Studio 2022 with the **Desktop development with C++** workload and an x64 MSVC toolchain.
- CMake 3.22 or newer.
- Git.
- vcpkg, such as the installation included with Visual Studio.
- Internet access during the first configure/build to fetch JUCE and the bundled SoundFont if they are not already present.

### Clone and configure

Open a **Developer PowerShell for Visual Studio 2022** (or another shell where the MSVC x64 build environment is configured), then run:

~~~powershell
git clone https://github.com/mamefan1945/Vocoder.git
cd Vocoder

# Install the FluidSynth x64 dependency.
& "$env:VCPKG_INSTALLATION_ROOT/vcpkg.exe" install fluidsynth:x64-windows --classic

# Configure the x64 build.
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows

# Build Release.
cmake --build build --config Release --parallel
~~~

If VCPKG_INSTALLATION_ROOT is not defined in your environment, replace it in the commands with the actual path to your vcpkg installation. The vcpkg triplet must match the x64 build.

### What CMake fetches

The CMake configuration uses FetchContent to retrieve JUCE from its official GitHub repository and pins it to **8.0.10**. FluidSynth is found through the vcpkg CMake package configuration.

CMake also downloads the bundled **GeneralUser GS v2.0.3** SoundFont and its licence into the build tree if they are not already present. SHA-256 hashes are checked during download. The post-build step copies the SoundFont, its licence, and the FluidSynth licence beside the built application.

The executable and runtime dependencies are generated under the build output directory. The exact subdirectory can vary with the generator and configuration; search under build for Vocoder.exe.

### Build with Visual Studio or another generator

The documented command uses the Visual Studio generator via -A x64. If using a different generator, configure a separate build directory and ensure the compiler, architecture, and vcpkg triplet all agree. Do not reuse a CMake build directory after changing generators or toolchains; remove that build directory and configure again.

### CI build

The GitHub Actions workflow is .github/workflows/windows-build.yml. It runs on windows-2022, configures the MSVC x64 environment, installs FluidSynth through vcpkg, configures CMake, builds Release, and uploads the Windows x64 package.

The workflow is a compile/package check. It does not currently run automated audio-quality tests, render reference audio, or validate the harmonizer by listening.

## Project layout

| Path | Purpose |
|---|---|
| CMakeLists.txt | CMake project, JUCE FetchContent setup, FluidSynth dependency, SoundFont download/hash verification, target configuration, and post-build packaging. |
| Source/Main.cpp | JUCE application entry point, main window, and startup splash. |
| Source/MainComponent.h/.cpp | Main UI, audio-device callback, MIDI routing, carrier selection, meters, and mode dispatch. |
| Source/VocoderEngine.h/.cpp | Vocoder analysis/synthesis filter banks and speech-envelope processing. |
| Source/HarmonizerEngine.h/.cpp | Pitch estimation, pitch-mark detection, TD-PSOLA grain scheduling, and harmony voice mixing. |
| Source/CarrierSynth.h/.cpp | Internal oscillator-based MIDI synthesizer. |
| Source/SoundFontEngine.h/.cpp | FluidSynth wrapper, SoundFont loading, preset enumeration, MIDI handling, and audio rendering. |
| LICENSES/ | Third-party licence texts included in the repository. |
| .github/workflows/windows-build.yml | Windows x64 GitHub Actions build and artifact packaging. |

### Audio-thread overview

JUCE's audio device callback is the real-time entry point. It collects MIDI events from the shared keyboard state, renders the selected internal carrier (oscillator synth or FluidSynth), and dispatches samples to the selected processing engine.

- Pass-through sends input channel 1 to the output with the master gain.
- Vocoder mode sends microphone input, selected carrier, and carrier source selection into the vocoder engine.
- Harmonizer mode sends microphone input and MIDI events into the harmonizer engine. The harmonizer returns its own dry/wet result and applies the output gain.

The UI communicates audio-thread parameters using atomics where appropriate. Audio buffers are prepared when the audio device starts so the callback does not need to resize the main processing buffers.

## Dependencies and licences

- **JUCE 8.0.10** — fetched from the official JUCE GitHub repository. JUCE is available under the AGPLv3 or a commercial licence. Review the [JUCE licensing terms](https://juce.com/legal/juce-licence/) before distributing the application or modified binaries.
- **FluidSynth** — used for SoundFont playback and licensed under LGPL-2.1-or-later. The repository includes LICENSES/FluidSynth-LGPL-2.1.txt, and the build package copies the licence beside the executable.
- **GeneralUser GS v2.0.3** — bundled as a separate SoundFont data file, not embedded in the executable. Its licence is copied into the SoundFonts directory as GeneralUser-GS-LICENSE.txt.
- **Signalsmith licence texts** — the repository currently contains MIT licence texts for Signalsmith Linear and Signalsmith Stretch in LICENSES. The current harmonizer source is a custom TD-PSOLA implementation; do not assume that those libraries are the active pitch-shifting implementation merely because their licence files are present.

Check the licence terms of any user-supplied SoundFont or other asset before redistributing it. This README is technical project documentation, not legal advice.

## Known limitations and development notes

- Windows x64 standalone is the current supported application target.
- A VST3 target is a possible future direction, not a current build product.
- The vocoder is a developing 32-band implementation; it is not intended to be a transparent or reference-grade speech processor.
- Formant shifting is performed by shifting envelope-band lookup positions and is therefore approximate.
- The harmonizer is a custom, experimental eight-voice TD-PSOLA engine with LPC-based formant preservation and transient-aware grain suppression. Its pitch tracking, LPC envelope quality, transient decisions, grain scheduling, polyphonic stability, and latency still need listening-based refinement.
- No automated regression suite currently measures pitch accuracy, harmonic distortion, intelligibility, crackle/dropout rate, or subjective sound quality.
- The best way to contribute useful test information is to include the exact build/commit, audio device and sample rate, buffer size, mode, relevant control settings, MIDI notes held, and a short reproducible audio example.

For bug reports, describe what you expected, what actually happened, and the shortest sequence of steps that reproduces it. For audio defects, include both the dry input and processed output when possible.
