#include "MainComponent.h"

int MainComponent::LockableMidiKeyboard::noteAt(juce::Point<float> position) const
{
    // Black keys overlap the white-key rectangles, so test them first.
    for (int pass = 0; pass < 2; ++pass)
        for (int note = 108; note >= 36; --note)
            if (isBlackKey(note) == (pass == 0)
                && getRectangleForKey(note).contains(position))
                return note;
    return -1;
}

bool MainComponent::LockableMidiKeyboard::isBlackKey(int note) noexcept
{
    const auto semitone = note % 12;
    return semitone == 1 || semitone == 3 || semitone == 6
        || semitone == 8 || semitone == 10;
}

void MainComponent::LockableMidiKeyboard::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto note = noteAt(event.position);
    if (event.mods.isRightButtonDown())
    {
        if (note >= 0)
        {
            auto& locked = lockedNotes[static_cast<size_t>(note)];
            if (locked)
                keyboardState.noteOff(1, note, 0.0f);
            else
                keyboardState.noteOn(1, note, 0.8f);
            locked = !locked;
        }
        suppressMouseSequence = true;
        return;
    }

    suppressMouseSequence = note >= 0 && lockedNotes[static_cast<size_t>(note)];
    if (!suppressMouseSequence)
        juce::MidiKeyboardComponent::mouseDown(event);
}

void MainComponent::LockableMidiKeyboard::mouseDrag(const juce::MouseEvent& event)
{
    if (!suppressMouseSequence)
        juce::MidiKeyboardComponent::mouseDrag(event);
}

void MainComponent::LockableMidiKeyboard::mouseUp(const juce::MouseEvent& event)
{
    if (!suppressMouseSequence)
        juce::MidiKeyboardComponent::mouseUp(event);
    suppressMouseSequence = false;
}

MainComponent::MainComponent(std::function<void()> onStartupComplete)
    : deviceSelector(deviceManager, 1, 2, 0, 2, false, false, true, false),
      midiInputRouter(keyboardState, externalMidiCollector),
      keyboardComponent(keyboardState),
      startupCompleteCallback(onStartupComplete)
{
    setSize(1080, 740);
    testAudioFormats.registerBasicFormats();
    testAudioReadAheadThread.startThread();

    titleLabel.setText("VOCODER  |  REAL-TIME AUDIO", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    presetLabel.setText("Presets", juce::dontSendNotification);
    addAndMakeVisible(presetLabel);
    savePresetButton.setTooltip("Save all processing, carrier and sound controls to a reusable .vcpreset file.");
    savePresetButton.onClick = [this] { savePreset(); };
    addAndMakeVisible(savePresetButton);
    loadPresetButton.setTooltip("Load a previously saved .vcpreset file.");
    loadPresetButton.onClick = [this] { loadPreset(); };
    addAndMakeVisible(loadPresetButton);

    modeLabel.setText("Processing", juce::dontSendNotification);
    addAndMakeVisible(modeLabel);
    processingMode.addItem("Pass-through", 1);
    processingMode.addItem("Vocoder", 2);
    processingMode.addItem("Harmonizer", 3);
    processingMode.setSelectedId(1);
    processingMode.onChange = [this]
    {
        selectedMode.store(processingMode.getSelectedId() - 1, std::memory_order_relaxed);
        updateModeControls();
    };
    addAndMakeVisible(processingMode);

    carrierLabel.setText("Carrier", juce::dontSendNotification);
    addAndMakeVisible(carrierLabel);
    carrierSource.addItem("Internal MIDI synth", 1);
    carrierSource.addItem("External input 2", 2);
    carrierSource.setSelectedId(1);
    carrierSource.onChange = [this]
    {
        selectedCarrier.store(carrierSource.getSelectedId() == 2 ? 1 : 0);
    };
    addAndMakeVisible(carrierSource);

    midiLabel.setText("MIDI input", juce::dontSendNotification);
    addAndMakeVisible(midiLabel);
    midiInput.onChange = [this] { selectMidiInput(); };
    addAndMakeVisible(midiInput);
    refreshMidiButton.setTooltip("Rescan Windows MIDI inputs. Connect the keyboard first, then refresh if it is not listed.");
    refreshMidiButton.onClick = [this] { refreshMidiInputs(); };
    addAndMakeVisible(refreshMidiButton);
    waveformLabel.setText("Carrier type", juce::dontSendNotification);
    addAndMakeVisible(waveformLabel);
    carrierPresetLabel.setText("Profile", juce::dontSendNotification);
    addAndMakeVisible(carrierPresetLabel);
    waveform.addItem("Sine", 1);
    waveform.addItem("Saw", 2);
    waveform.addItem("Square", 3);
    waveform.addItem("Triangle", 4);
    waveform.addItem("Noise", 5);
    waveform.addItem("Rich (PWM)", 6);
    waveform.addItem("Angelic Choir", 7);
    waveform.addItem("SoundFont (GM)", 8);
    waveform.setSelectedId(6);
    carrierSynth.setWaveform(CarrierSynth::rich);
    waveform.onChange = [this]
    {
        const auto selected = waveform.getSelectedId();
        useSoundFontCarrier.store(selected == 8, std::memory_order_relaxed);
        if (selected != 8)
            carrierSynth.setWaveform(selected - 1);
        if (!applyingCarrierPreset)
            carrierPreset.setSelectedId(11, juce::dontSendNotification);
        updateSoundFontControls();
    };
    addAndMakeVisible(waveform);

    carrierPreset.addItem("Classic Analog Saw", 1);
    carrierPreset.addItem("Rich PWM", 2);
    carrierPreset.addItem("Bright Pulse", 3);
    carrierPreset.addItem("Soft Triangle", 4);
    carrierPreset.addItem("Choir Pad", 5);
    carrierPreset.addItem("Noise / Hiss", 6);
    carrierPreset.addItem("Noise Hybrid", 7);
    carrierPreset.addItem("GM Organ", 8);
    carrierPreset.addItem("GM Strings", 9);
    carrierPreset.addItem("GM Choir", 10);
    carrierPreset.addItem("Custom", 11);
    carrierPreset.setSelectedId(2, juce::dontSendNotification);
    carrierPreset.setTooltip("Curated carrier designs. Rich PWM is the recommended starting point; GM profiles select a matching instrument from the loaded SoundFont.");
    carrierPreset.onChange = [this]
    {
        const auto profile = carrierPreset.getSelectedId();
        if (profile == 11 || profile <= 0)
            return;

        applyingCarrierPreset = true;
        int waveformId = 6;
        float noiseLevel = 100.0f;
        float sibilanceLevel = 100.0f;
        juce::String instrumentSearch;
        bool instrumentFound = true;
        switch (profile)
        {
            case 1: waveformId = 2; break; // Classic saw
            case 2: waveformId = 6; break; // Rich PWM
            case 3: waveformId = 3; noiseLevel = 110.0f; sibilanceLevel = 110.0f; break;
            case 4: waveformId = 4; break;
            case 5: waveformId = 7; noiseLevel = 115.0f; sibilanceLevel = 115.0f; break;
            case 6: waveformId = 5; noiseLevel = 200.0f; sibilanceLevel = 150.0f; break;
            case 7: waveformId = 6; noiseLevel = 140.0f; sibilanceLevel = 150.0f; break;
            case 8: waveformId = 8; instrumentSearch = "organ"; break;
            case 9: waveformId = 8; instrumentSearch = "string"; break;
            case 10: waveformId = 8; instrumentSearch = "choir"; break;
            default: break;
        }

        waveform.setSelectedId(waveformId, juce::sendNotificationSync);
        if (instrumentSearch.isNotEmpty())
        {
            const auto& available = soundFontEngine.getPresets();
            instrumentFound = false;
            int bestMatch = -1;
            int bestScore = 0;
            for (size_t i = 0; i < available.size(); ++i)
            {
                const auto name = available[i].name;
                int score = name.containsIgnoreCase(instrumentSearch) ? 10 : 0;
                if (profile == 8 && name.containsIgnoreCase("drawbar")) score += 5;
                if (profile == 9 && name.containsIgnoreCase("ensemble")) score += 5;
                if (profile == 10 && name.containsIgnoreCase("aahs")) score += 5;
                if (score > bestScore)
                {
                    bestScore = score;
                    bestMatch = static_cast<int>(i);
                }
            }
            if (bestMatch >= 0)
            {
                soundFontEngine.selectPreset(bestMatch);
                soundFontInstrument.setSelectedId(bestMatch + 1, juce::dontSendNotification);
                instrumentFound = true;
            }
            else
            {
                statusLabel.setText("No matching GM " + instrumentSearch
                    + " patch found in the loaded SoundFont.", juce::dontSendNotification);
            }
        }

        unvoicedNoiseSlider.setValue(noiseLevel, juce::sendNotificationSync);
        sibilanceSlider.setValue(sibilanceLevel, juce::sendNotificationSync);
        applyingCarrierPreset = false;
        if (instrumentFound)
            statusLabel.setText("Carrier profile: " + carrierPreset.getText(), juce::dontSendNotification);
    };
    addAndMakeVisible(carrierPreset);

    soundFontInstrumentLabel.setText("Instrument", juce::dontSendNotification);
    addAndMakeVisible(soundFontInstrumentLabel);
    soundFontInstrument.onChange = [this]
    {
        soundFontEngine.selectPreset(soundFontInstrument.getSelectedId() - 1);
        if (!applyingCarrierPreset)
            carrierPreset.setSelectedId(11, juce::dontSendNotification);
    };
    addAndMakeVisible(soundFontInstrument);
    loadSoundFontButton.onClick = [this] { chooseSoundFont(); };
    addAndMakeVisible(loadSoundFontButton);
    updateSoundFontControls();

    octaveLabel.setText("Octave", juce::dontSendNotification);
    addAndMakeVisible(octaveLabel);
    octave.addItem("-2", 1);
    octave.addItem("-1", 2);
    octave.addItem("0", 3);
    octave.addItem("+1", 4);
    octave.addItem("+2", 5);
    octave.setSelectedId(3);
    octave.onChange = [this]
    {
        const auto shift = octave.getSelectedId() - 3;
        carrierSynth.setOctave(shift);
        harmonizerOctaveShift.store(shift, std::memory_order_relaxed);
    };
    addAndMakeVisible(octave);

    outputLevelLabel.setText("Output level", juce::dontSendNotification);
    addAndMakeVisible(outputLevelLabel);
    outputLevel.setRange(0.0, 2.0, 0.01);
    outputLevel.setValue(1.0);
    outputLevel.setTextValueSuffix(" x");
    outputLevel.setSliderStyle(juce::Slider::LinearHorizontal);
    outputLevel.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 28);
    outputLevel.addListener(this);
    addAndMakeVisible(outputLevel);

    const auto configureControl = [this](juce::Slider& slider, juce::Label& label,
                                         const juce::String& caption, double minimum,
                                         double maximum, double step, double initial,
                                         const juce::String& suffix)
    {
        label.setText(caption, juce::dontSendNotification);
        addAndMakeVisible(label);
        slider.setRange(minimum, maximum, step);
        slider.setValue(initial, juce::dontSendNotification);
        slider.setTextValueSuffix(suffix);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 24);
        slider.addListener(this);
        addAndMakeVisible(slider);
    };

    configureControl(attackTime, attackLabel, "Attack", 0.1, 30.0, 0.1, 1.0, " ms");
    configureControl(releaseTime, releaseLabel, "Release", 1.0, 250.0, 0.5, 10.0, " ms");
    configureControl(lowFrequency, lowFrequencyLabel, "Low freq", 120.0, 4000.0, 10.0, 120.0, " Hz");
    configureControl(highFrequency, highFrequencyLabel, "High freq", 500.0, 8000.0, 10.0, 8000.0, " Hz");
    configureControl(dryWetMix, dryWetLabel, "Dry / wet", 0.0, 100.0, 1.0, 100.0, " %");
    configureControl(formantShiftSlider, formantShiftLabel, "Formant", -12.0, 12.0, 0.5, 0.0, " st");
    configureControl(gateThresholdSlider, gateThresholdLabel, "Noise gate", 0.0, 0.02, 0.0005, 0.003, " RMS");
    configureControl(articulationLiftSlider, articulationLiftLabel, "Articulation", 0.0, 200.0, 1.0, 100.0, " %");
    configureControl(unvoicedNoiseSlider, unvoicedNoiseLabel, "Unvoiced noise", 0.0, 200.0, 1.0, 100.0, " %");
    configureControl(sibilanceSlider, sibilanceLabel, "Sibilance", 0.0, 200.0, 1.0, 100.0, " %");
    filterBandCountLabel.setText("Bands", juce::dontSendNotification);
    addAndMakeVisible(filterBandCountLabel);
    filterBandCount.addItem("24", 1);
    filterBandCount.addItem("32", 2);
    filterBandCount.addItem("40", 3);
    filterBandCount.addItem("48", 4);
    filterBandCount.addItem("56", 5);
    filterBandCount.setSelectedId(3, juce::dontSendNotification);
    filterBandCount.setTooltip("Number of logarithmically spaced analysis and synthesis bands.");
    filterBandCount.onChange = [this]
    {
        const int counts[] { 24, 32, 40, 48, 56 };
        const auto index = juce::jlimit(0, 4, filterBandCount.getSelectedId() - 1);
        vocoder.setBandCount(counts[index]);
    };
    addAndMakeVisible(filterBandCount);
    configureControl(filterQSlider, filterQLabel, "Filter Q", 2.0, 10.0, 0.5, 5.0, "");
    filterQSlider.setTooltip("Higher Q narrows each band; lower Q increases overlap between adjacent bands.");
    configureControl(formantPreservationSlider, formantPreservationLabel, "Formant preserve", 0.0, 100.0, 1.0, 100.0, " %");
    configureControl(transientProtectionSlider, transientProtectionLabel, "Transient protect", 0.0, 100.0, 1.0, 100.0, " %");
    configureControl(pitchSensitivitySlider, pitchSensitivityLabel, "Pitch sensitivity", 0.0, 100.0, 1.0, 50.0, " %");
    pitchSensitivitySlider.setTooltip("Higher values track breathier voices but may mistake noisy consonants for pitch.");
    configureControl(pitchHoldSlider, pitchHoldLabel, "Pitch hold", 0.0, 300.0, 5.0, 120.0, " ms");
    pitchHoldSlider.setTooltip("How long the last reliable vocal pitch is retained through unvoiced gaps.");
    configureControl(pitchRecoverySlider, pitchRecoveryLabel, "Pitch recovery", 50.0, 1000.0, 10.0, 250.0, " ms");
    pitchRecoverySlider.setTooltip("How quickly the held pitch fades after the hold time expires.");
    inputMeterLabel.setText("Input: -120.0 dBFS", juce::dontSendNotification);
    outputMeterLabel.setText("Output: -120.0 dBFS", juce::dontSendNotification);
    addAndMakeVisible(inputMeterLabel);
    addAndMakeVisible(outputMeterLabel);

    microphoneMonitor.setButtonText("Mic monitor");
    microphoneMonitor.setTooltip("Audition the microphone directly alongside the processed signal.");
    microphoneMonitor.onClick = [this]
    {
        monitorMicrophone.store(microphoneMonitor.getToggleState(), std::memory_order_relaxed);
    };
    addAndMakeVisible(microphoneMonitor);

    carrierPreview.setButtonText("Carrier preview");
    carrierPreview.setTooltip("Audition the selected carrier directly, independently of the vocoder.");
    carrierPreview.onClick = [this]
    {
        previewCarrier.store(carrierPreview.getToggleState(), std::memory_order_relaxed);
    };
    addAndMakeVisible(carrierPreview);

    loadTestAudioButton.setTooltip("Load prerecorded speech for repeatable testing.");
    loadTestAudioButton.onClick = [this] { chooseTestAudio(); };
    addAndMakeVisible(loadTestAudioButton);
    playTestAudioButton.setEnabled(false);
    playTestAudioButton.onClick = [this]
    {
        if (testAudioReaderSource != nullptr)
        {
            if (testAudioTransport.getCurrentPosition() >= testAudioTransport.getLengthInSeconds())
                testAudioTransport.setPosition(0.0);
            testAudioTransport.start();
            statusLabel.setText("Playing prerecorded test input", juce::dontSendNotification);
        }
    };
    addAndMakeVisible(playTestAudioButton);
    stopTestAudioButton.setEnabled(false);
    stopTestAudioButton.onClick = [this]
    {
        testAudioTransport.stop();
        if (testAudioReaderSource != nullptr) testAudioTransport.setPosition(0.0);
        statusLabel.setText("Test audio stopped", juce::dontSendNotification);
    };
    addAndMakeVisible(stopTestAudioButton);
    useTestAudioInput.setEnabled(false);
    useTestAudioInput.setTooltip("Replace microphone input with the file; external carrier input remains available.");
    useTestAudioInput.onClick = [this]
    {
        useTestAudio.store(useTestAudioInput.getToggleState(), std::memory_order_relaxed);
        statusLabel.setText(useTestAudioInput.getToggleState()
            ? "Prerecorded file selected as voice input" : "Live audio input selected",
            juce::dontSendNotification);
    };
    addAndMakeVisible(useTestAudioInput);

    testVoiceSelector.addItem("Nova (female)", 1);
    testVoiceSelector.addItem("Ash (male)", 2);
    testVoiceSelector.setSelectedId(1, juce::dontSendNotification);
    testVoiceSelector.setTooltip("Select the bundled speech-test voice. The WAV files are found in Downloads or the app's TestAudio folder.");
    testVoiceSelector.onChange = [this]
    {
        if (speechTestRunning)
        {
            const auto voiceName = testVoiceSelector.getSelectedId() == 2 ? "ash" : "nova";
            if (loadTestAudioFile(findSpeechTestFile(voiceName), true))
                statusLabel.setText("Speech test: " + testVoiceSelector.getText() + " (looping)",
                                    juce::dontSendNotification);
            else
                statusLabel.setText("Could not find vocoder_test_" + juce::String(voiceName)
                    + ".wav in Downloads or the app TestAudio folder.", juce::dontSendNotification);
        }
    };
    addAndMakeVisible(testVoiceSelector);

    runSpeechTestButton.setClickingTogglesState(true);
    runSpeechTestButton.setTooltip("Automatically configures the vocoder, selects Rich PWM, holds a C3 carrier note, and loops the selected speech file.");
    runSpeechTestButton.onClick = [this]
    {
        if (runSpeechTestButton.getToggleState())
            startSpeechTest();
        else
            stopSpeechTest();
    };
    addAndMakeVisible(runSpeechTestButton);

    startTimerHz(20);

    keyboardComponent.setAvailableRange(36, 108);
    keyboardComponent.setKeyWidth(24.0f);
    addAndMakeVisible(keyboardComponent);

    statusLabel.setText("Starting in background...", juce::dontSendNotification);
    addAndMakeVisible(statusLabel);
    deviceViewport.setViewedComponent(&deviceSelector, false);
    deviceViewport.setScrollBarsShown(true, false);
    deviceViewport.setScrollOnDragEnabled(true);
    addAndMakeVisible(deviceViewport);

    updateModeControls();

    // Defer all potentially slow startup work until after this component has
    // returned to the message loop, so the splash and waveform can animate.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    startupThread = std::thread([this, safeThis]
    {
        initializeAudioAndInstruments();
        if (shuttingDown.load(std::memory_order_acquire))
            return;

        juce::MessageManager::callAsync([safeThis]
        {
            if (safeThis == nullptr)
                return;

            safeThis->populateSoundFontInstruments();
            safeThis->refreshMidiInputs();
            if (safeThis->statusLabel.getText().startsWith("Starting in background"))
                safeThis->statusLabel.setText("Ready. Select a processing mode and audio devices.",
                                              juce::dontSendNotification);
            if (safeThis->startupCompleteCallback)
                safeThis->startupCompleteCallback();
        });
    });
}

MainComponent::~MainComponent()
{
    shuttingDown.store(true, std::memory_order_release);
    if (startupThread.joinable())
        startupThread.join();

    deviceManager.removeAudioCallback(this);
    testAudioTransport.stop();
    testAudioTransport.setSource(nullptr);
    testAudioReaderSource.reset();
    testAudioReadAheadThread.stopThread(2000);
    if (activeMidiIdentifier.isNotEmpty())
        deviceManager.removeMidiInputDeviceCallback(activeMidiIdentifier, &midiInputRouter);
    outputLevel.removeListener(this);
    attackTime.removeListener(this);
    releaseTime.removeListener(this);
    lowFrequency.removeListener(this);
    highFrequency.removeListener(this);
    dryWetMix.removeListener(this);
    formantShiftSlider.removeListener(this);
    gateThresholdSlider.removeListener(this);
    articulationLiftSlider.removeListener(this);
    unvoicedNoiseSlider.removeListener(this);
    sibilanceSlider.removeListener(this);
    filterQSlider.removeListener(this);
    formantPreservationSlider.removeListener(this);
    transientProtectionSlider.removeListener(this);
    pitchSensitivitySlider.removeListener(this);
    pitchHoldSlider.removeListener(this);
    pitchRecoverySlider.removeListener(this);
    stopTimer();
    deviceManager.closeAudioDevice();
}


void MainComponent::initializeAudioAndInstruments()
{
    const auto defaultSoundFont = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getSiblingFile("SoundFonts")
        .getChildFile("GeneralUser-GS.sf2");

    juce::String soundFontError;
    const auto soundFontLoaded = soundFontEngine.load(defaultSoundFont, soundFontError);

    if (shuttingDown.load(std::memory_order_acquire))
        return;

    // Opening a Windows audio device can block while the driver initializes;
    // keep it off the UI thread. Register the callback only after the synth
    // engine and its SoundFont state have finished loading.
    const auto audioError = deviceManager.initialise(2, 2, nullptr, true);
    if (shuttingDown.load(std::memory_order_acquire))
        return;

    deviceManager.addAudioCallback(this);

    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, soundFontLoaded,
                                     soundFontError, audioError]
    {
        if (safeThis == nullptr)
            return;

        if (!soundFontLoaded)
            safeThis->statusLabel.setText("Default SoundFont unavailable: "
                + soundFontError, juce::dontSendNotification);
        else if (audioError.isNotEmpty())
            safeThis->statusLabel.setText("Audio initialization: "
                + audioError, juce::dontSendNotification);
    });
}

void MainComponent::updateSoundFontControls()
{
    const auto visible = waveform.getSelectedId() == 8;
    soundFontInstrumentLabel.setVisible(visible);
    soundFontInstrument.setVisible(visible);
    loadSoundFontButton.setVisible(visible);
    resized();
}

void MainComponent::updateModeControls()
{
    const auto mode = processingMode.getSelectedId();
    const auto vocoderMode = mode == 2;
    const auto harmonizerMode = mode == 3;
    const auto hasKeyboard = vocoderMode || harmonizerMode;
    const auto show = [](juce::Component& component, bool visible) { component.setVisible(visible); };
    show(carrierLabel, vocoderMode);
    show(carrierSource, vocoderMode);
    show(carrierPresetLabel, vocoderMode);
    show(carrierPreset, vocoderMode);
    show(waveformLabel, vocoderMode);
    show(waveform, vocoderMode);
    const auto showSoundFont = vocoderMode && waveform.getSelectedId() == 8;
    show(soundFontInstrumentLabel, showSoundFont);
    show(soundFontInstrument, showSoundFont);
    show(loadSoundFontButton, showSoundFont);
    show(midiLabel, hasKeyboard);
    show(midiInput, hasKeyboard);
    show(refreshMidiButton, hasKeyboard);
    show(octaveLabel, hasKeyboard);
    show(octave, hasKeyboard);
    show(keyboardComponent, hasKeyboard);
    show(attackLabel, vocoderMode);
    show(attackTime, vocoderMode);
    show(releaseLabel, vocoderMode);
    show(releaseTime, vocoderMode);
    show(lowFrequencyLabel, vocoderMode);
    show(lowFrequency, vocoderMode);
    show(highFrequencyLabel, vocoderMode);
    show(highFrequency, vocoderMode);
    show(formantShiftLabel, vocoderMode);
    show(formantShiftSlider, vocoderMode);
    show(gateThresholdLabel, vocoderMode);
    show(gateThresholdSlider, vocoderMode);
    show(articulationLiftLabel, vocoderMode);
    show(articulationLiftSlider, vocoderMode);
    show(unvoicedNoiseLabel, vocoderMode);
    show(unvoicedNoiseSlider, vocoderMode);
    show(sibilanceLabel, vocoderMode);
    show(sibilanceSlider, vocoderMode);
    show(filterBandCountLabel, vocoderMode);
    show(filterBandCount, vocoderMode);
    show(filterQLabel, vocoderMode);
    show(filterQSlider, vocoderMode);
    show(formantPreservationLabel, harmonizerMode);
    show(formantPreservationSlider, harmonizerMode);
    show(transientProtectionLabel, harmonizerMode);
    show(transientProtectionSlider, harmonizerMode);
    show(pitchSensitivityLabel, harmonizerMode);
    show(pitchSensitivitySlider, harmonizerMode);
    show(pitchHoldLabel, harmonizerMode);
    show(pitchHoldSlider, harmonizerMode);
    show(pitchRecoveryLabel, harmonizerMode);
    show(pitchRecoverySlider, harmonizerMode);
    show(microphoneMonitor, vocoderMode);
    show(carrierPreview, vocoderMode);
    const auto showMix = vocoderMode || harmonizerMode;
    show(dryWetLabel, showMix);
    show(dryWetMix, showMix);
    show(outputLevelLabel, true);
    show(outputLevel, true);
    show(inputMeterLabel, true);
    show(outputMeterLabel, true);
    resized();
}
void MainComponent::populateSoundFontInstruments()
{
    soundFontInstrument.clear(juce::dontSendNotification);
    const auto& presets = soundFontEngine.getPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& preset = presets[i];
        const auto label = preset.name + "  [Bank " + juce::String(preset.bank)
            + ", Program " + juce::String(preset.program + 1) + "]";
        soundFontInstrument.addItem(label, static_cast<int>(i) + 1);
    }

    if (!presets.empty())
        soundFontInstrument.setSelectedId(soundFontEngine.getSelectedPresetIndex() + 1,
                                          juce::dontSendNotification);
}

void MainComponent::chooseSoundFont()
{
    soundFontFileChooser = std::make_unique<juce::FileChooser>(
        "Load a SoundFont", juce::File{}, "*.sf2");
    soundFontFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto file = chooser.getResult();
            if (file == juce::File{})
                return;

            juce::String error;
            if (safeThis->soundFontEngine.load(file, error))
            {
                safeThis->populateSoundFontInstruments();
                safeThis->statusLabel.setText("Loaded SoundFont: "
                    + safeThis->soundFontEngine.getLoadedFileName(),
                    juce::dontSendNotification);
            }
            else
            {
                safeThis->statusLabel.setText("SoundFont load failed: " + error,
                                              juce::dontSendNotification);
            }
        });
}

juce::File MainComponent::findSpeechTestFile(const juce::String& voiceName) const
{
    const auto fileName = "vocoder_test_" + voiceName + ".wav";
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const juce::Array<juce::File> candidates
    {
        executable.getSiblingFile("TestAudio").getChildFile(fileName),
        executable.getSiblingFile(fileName),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Downloads").getChildFile(fileName),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("Vocoder Test Audio").getChildFile(fileName)
    };

    for (const auto& candidate : candidates)
        if (candidate.existsAsFile())
            return candidate;

    return {};
}

bool MainComponent::loadTestAudioFile(const juce::File& file, bool loop)
{
    if (!file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader(testAudioFormats.createReaderFor(file));
    if (reader == nullptr)
        return false;

    testAudioTransport.stop();
    testAudioTransport.setSource(nullptr);
    testAudioReaderSource.reset();

    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    newSource->setLooping(loop);
    const auto sourceRate = newSource->getAudioFormatReader()->sampleRate;
    testAudioTransport.setSource(newSource.get(), 32768, &testAudioReadAheadThread, sourceRate);
    testAudioReaderSource = std::move(newSource);
    testAudioTransport.setPosition(0.0);
    useTestAudio.store(true, std::memory_order_relaxed);
    useTestAudioInput.setToggleState(true, juce::dontSendNotification);
    useTestAudioInput.setEnabled(true);
    playTestAudioButton.setEnabled(true);
    stopTestAudioButton.setEnabled(true);
    if (loop)
        testAudioTransport.start();
    return true;
}

void MainComponent::startSpeechTest()
{
    const auto voiceName = testVoiceSelector.getSelectedId() == 2 ? "ash" : "nova";
    const auto file = findSpeechTestFile(voiceName);
    if (!file.existsAsFile())
    {
        runSpeechTestButton.setToggleState(false, juce::dontSendNotification);
        statusLabel.setText("Speech WAV not found. Download vocoder_test_nova.wav and vocoder_test_ash.wav "
            "to your Downloads folder, then try again.", juce::dontSendNotification);
        return;
    }

    processingMode.setSelectedId(2, juce::sendNotificationSync);
    carrierSource.setSelectedId(1, juce::sendNotificationSync);
    carrierPreset.setSelectedId(2, juce::sendNotificationSync);
    octave.setSelectedId(3, juce::sendNotificationSync);
    attackTime.setValue(1.0, juce::sendNotificationSync);
    releaseTime.setValue(10.0, juce::sendNotificationSync);
    dryWetMix.setValue(100.0, juce::sendNotificationSync);
    outputLevel.setValue(0.75, juce::sendNotificationSync);
    microphoneMonitor.setToggleState(false, juce::dontSendNotification);
    monitorMicrophone.store(false, std::memory_order_relaxed);
    carrierPreview.setToggleState(false, juce::dontSendNotification);
    previewCarrier.store(false, std::memory_order_relaxed);
    midiInput.setSelectedId(1, juce::sendNotificationSync); // Isolate the fixed test note from external MIDI.
    for (int channel = 1; channel <= 16; ++channel)
        keyboardState.allNotesOff(channel);
    keyboardState.noteOn(1, 48, 0.8f); // C3: stable, single-note carrier reference.
    if (!loadTestAudioFile(file, true))
    {
        keyboardState.noteOff(1, 48, 0.0f);
        runSpeechTestButton.setToggleState(false, juce::dontSendNotification);
        statusLabel.setText("Could not open speech test WAV: " + file.getFileName(),
                            juce::dontSendNotification);
        return;
    }
    speechTestRunning = true;
    statusLabel.setText("Speech test running: " + testVoiceSelector.getText()
        + " | Rich PWM | C3 | 100% wet | looping", juce::dontSendNotification);
}

void MainComponent::stopSpeechTest()
{
    speechTestRunning = false;
    testAudioTransport.stop();
    if (testAudioReaderSource != nullptr)
    {
        testAudioReaderSource->setLooping(false);
        testAudioTransport.setPosition(0.0);
    }
    keyboardState.noteOff(1, 48, 0.0f);
    useTestAudio.store(false, std::memory_order_relaxed);
    useTestAudioInput.setToggleState(false, juce::dontSendNotification);
    statusLabel.setText("Speech test stopped. Live microphone input restored.", juce::dontSendNotification);
}

void MainComponent::chooseTestAudio()
{
    testAudioFileChooser = std::make_unique<juce::FileChooser>(
        "Choose prerecorded speech for testing", juce::File{},
        "*.wav;*.aif;*.aiff");
    testAudioFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr) return;
            const auto file = chooser.getResult();
            if (file == juce::File{}) return;
            if (!safeThis->loadTestAudioFile(file, false))
            {
                safeThis->statusLabel.setText("Could not read audio file: " + file.getFileName(),
                                              juce::dontSendNotification);
                return;
            }
            safeThis->statusLabel.setText("Loaded: " + file.getFileName() + " | "
                + juce::String(safeThis->testAudioTransport.getLengthInSeconds(), 1) + " s",
                juce::dontSendNotification);
        });
}

void MainComponent::refreshMidiInputs()
{
    const auto previousIdentifier = activeMidiIdentifier;
    const auto devices = juce::MidiInput::getAvailableDevices();
    midiInput.clear(juce::dontSendNotification);
    midiInput.addItem("No MIDI input", 1);
    midiInputIdentifiers.clear();

    int selectedId = 1;
    for (int i = 0; i < devices.size(); ++i)
    {
        const auto& device = devices.getReference(i);
        midiInputIdentifiers.add(device.identifier);
        midiInput.addItem(device.name, i + 2);
        if (device.identifier == previousIdentifier)
            selectedId = i + 2;
    }

    if (previousIdentifier.isNotEmpty() && selectedId == 1)
    {
        deviceManager.removeMidiInputDeviceCallback(previousIdentifier, &midiInputRouter);
        deviceManager.setMidiInputDeviceEnabled(previousIdentifier, false);
        activeMidiIdentifier.clear();
        for (int channel = 1; channel <= 16; ++channel)
            keyboardState.allNotesOff(channel);
        statusLabel.setText("MIDI device disconnected. Select another input or refresh.",
                            juce::dontSendNotification);
    }

    midiInput.setSelectedId(selectedId, juce::dontSendNotification);
    if (selectedId == 1 && devices.isEmpty())
        midiInput.setTextWhenNothingSelected("No MIDI devices found");
}

void MainComponent::selectMidiInput()
{
    if (activeMidiIdentifier.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback(activeMidiIdentifier, &midiInputRouter);
        deviceManager.setMidiInputDeviceEnabled(activeMidiIdentifier, false);
        activeMidiIdentifier.clear();
    }

    for (int channel = 1; channel <= 16; ++channel)
        keyboardState.allNotesOff(channel);

    const auto selectedIndex = midiInput.getSelectedId() - 2;
    if (!juce::isPositiveAndBelow(selectedIndex, midiInputIdentifiers.size()))
    {
        statusLabel.setText("MIDI input: on-screen keyboard only",
                            juce::dontSendNotification);
        return;
    }

    activeMidiIdentifier = midiInputIdentifiers[selectedIndex];
    deviceManager.setMidiInputDeviceEnabled(activeMidiIdentifier, true);
    deviceManager.addMidiInputDeviceCallback(activeMidiIdentifier, &midiInputRouter);

    const auto selectedName = midiInput.getText();
    statusLabel.setText("MIDI input selected: " + selectedName
                            + " | Play keys to send notes",
                        juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff171a20));
    graphics.setColour(juce::Colour(0xff2b303a));
    graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(12.0f), 10.0f, 1.0f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(28);
    titleLabel.setBounds(area.removeFromTop(42));
    area.removeFromTop(10);
    auto row = area.removeFromTop(32);
    modeLabel.setBounds(row.removeFromLeft(82));
    processingMode.setBounds(row.removeFromLeft(180));
    const auto mode = processingMode.getSelectedId();
    const auto vocoderMode = mode == 2;
    const auto harmonizerMode = mode == 3;
    const auto hasKeyboard = vocoderMode || harmonizerMode;
    area.removeFromTop(8);
    row = area.removeFromTop(30);
    presetLabel.setBounds(row.removeFromLeft(58));
    row.removeFromLeft(8);
    savePresetButton.setBounds(row.removeFromLeft(124));
    row.removeFromLeft(8);
    loadPresetButton.setBounds(row.removeFromLeft(124));
    area.removeFromTop(6);
    row = area.removeFromTop(30);
    loadTestAudioButton.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(5);
    playTestAudioButton.setBounds(row.removeFromLeft(52));
    row.removeFromLeft(3);
    stopTestAudioButton.setBounds(row.removeFromLeft(52));
    row.removeFromLeft(8);
    useTestAudioInput.setBounds(row.removeFromLeft(166));
    row.removeFromLeft(8);
    testVoiceSelector.setBounds(row.removeFromLeft(125));
    row.removeFromLeft(8);
    runSpeechTestButton.setBounds(row.removeFromLeft(130));
    area.removeFromTop(6);
    if (hasKeyboard)
    {
        row = area.removeFromTop(32);
        midiLabel.setBounds(row.removeFromLeft(72));
        midiInput.setBounds(row.removeFromLeft(240));
        row.removeFromLeft(8);
        refreshMidiButton.setBounds(row.removeFromLeft(82));
    }
    if (vocoderMode)
    {
        area.removeFromTop(8);
        row = area.removeFromTop(32);
        carrierLabel.setBounds(row.removeFromLeft(58));
        carrierSource.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(8);
        carrierPresetLabel.setBounds(row.removeFromLeft(48));
        carrierPreset.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(8);
        waveformLabel.setBounds(row.removeFromLeft(82));
        waveform.setBounds(row.removeFromLeft(125));
        row.removeFromLeft(8);
        octaveLabel.setBounds(row.removeFromLeft(55));
        octave.setBounds(row.removeFromLeft(75));
        if (waveform.getSelectedId() == 8)
        {
            row.removeFromLeft(8);
            soundFontInstrumentLabel.setBounds(row.removeFromLeft(72));
            soundFontInstrument.setBounds(row.removeFromLeft(260));
            row.removeFromLeft(6);
            loadSoundFontButton.setBounds(row.removeFromLeft(105));
        }
    }
    else if (harmonizerMode)
    {
        area.removeFromTop(8);
        row = area.removeFromTop(32);
        octaveLabel.setBounds(row.removeFromLeft(55));
        octave.setBounds(row.removeFromLeft(90));
    }
    area.removeFromTop(8);
    outputLevelLabel.setBounds(area.removeFromTop(22));
    outputLevel.setBounds(area.removeFromTop(34));
    if (vocoderMode || harmonizerMode)
    {
        area.removeFromTop(8);
        row = area.removeFromTop(30);
        if (vocoderMode)
        {
            attackLabel.setBounds(row.removeFromLeft(52));
            attackTime.setBounds(row.removeFromLeft(150));
            row.removeFromLeft(12);
            releaseLabel.setBounds(row.removeFromLeft(58));
            releaseTime.setBounds(row.removeFromLeft(170));
            row.removeFromLeft(12);
        }
        dryWetLabel.setBounds(row.removeFromLeft(64));
        dryWetMix.setBounds(row);
    }
    if (vocoderMode)
    {
        area.removeFromTop(6);
        row = area.removeFromTop(30);
        lowFrequencyLabel.setBounds(row.removeFromLeft(64));
        lowFrequency.setBounds(row.removeFromLeft(180));
        row.removeFromLeft(12);
        highFrequencyLabel.setBounds(row.removeFromLeft(70));
        highFrequency.setBounds(row.removeFromLeft(190));
        area.removeFromTop(6);
        row = area.removeFromTop(30);
        formantShiftLabel.setBounds(row.removeFromLeft(64));
        formantShiftSlider.setBounds(row.removeFromLeft(210));
        gateThresholdLabel.setBounds(row.removeFromLeft(86));
        gateThresholdSlider.setBounds(row.removeFromLeft(220));
        area.removeFromTop(6);
        row = area.removeFromTop(30);
        articulationLiftLabel.setBounds(row.removeFromLeft(86));
        articulationLiftSlider.setBounds(row.removeFromLeft(210));
        row.removeFromLeft(12);
        unvoicedNoiseLabel.setBounds(row.removeFromLeft(100));
        unvoicedNoiseSlider.setBounds(row.removeFromLeft(210));
        area.removeFromTop(6);
        row = area.removeFromTop(30);
        sibilanceLabel.setBounds(row.removeFromLeft(86));
        sibilanceSlider.setBounds(row.removeFromLeft(210));
        row.removeFromLeft(12);
        filterBandCountLabel.setBounds(row.removeFromLeft(52));
        filterBandCount.setBounds(row.removeFromLeft(76));
        row.removeFromLeft(10);
        filterQLabel.setBounds(row.removeFromLeft(62));
        filterQSlider.setBounds(row.removeFromLeft(210));
    }
    else if (harmonizerMode)
    {
        area.removeFromTop(6);
        row = area.removeFromTop(30);
        formantPreservationLabel.setBounds(row.removeFromLeft(126));
        formantPreservationSlider.setBounds(row.removeFromLeft(260));
        row.removeFromLeft(12);
        transientProtectionLabel.setBounds(row.removeFromLeft(126));
        transientProtectionSlider.setBounds(row.removeFromLeft(260));
        area.removeFromTop(6);
        row = area.removeFromTop(30);
        pitchSensitivityLabel.setBounds(row.removeFromLeft(96));
        pitchSensitivitySlider.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(10);
        pitchHoldLabel.setBounds(row.removeFromLeft(72));
        pitchHoldSlider.setBounds(row.removeFromLeft(140));
        row.removeFromLeft(10);
        pitchRecoveryLabel.setBounds(row.removeFromLeft(90));
        pitchRecoverySlider.setBounds(row.removeFromLeft(150));
    }
    row = area.removeFromTop(24);
    inputMeterLabel.setBounds(row.removeFromLeft(180));
    outputMeterLabel.setBounds(row);
    if (vocoderMode)
    {
        area.removeFromTop(6);
        row = area.removeFromTop(28);
        microphoneMonitor.setBounds(row.removeFromLeft(150));
        carrierPreview.setBounds(row.removeFromLeft(170));
    }
    if (hasKeyboard)
    {
        area.removeFromTop(8);
        keyboardComponent.setBounds(area.removeFromTop(88));
    }
    area.removeFromTop(8);
    statusLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    deviceViewport.setBounds(area);
    deviceSelector.setBounds(0, 0, juce::jmax(320, deviceViewport.getWidth() - 18),
                             juce::jmax(300, deviceViewport.getHeight()));
}

void MainComponent::savePreset()
{
    const auto presetDirectory = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory).getChildFile("Vocoder Presets");
    presetDirectory.createDirectory();
    presetFileChooser = std::make_unique<juce::FileChooser>(
        "Save Vocoder preset", presetDirectory.getChildFile("My Preset.vcpreset"),
        "*.vcpreset");
    presetFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file == juce::File{})
                return;

            juce::DynamicObject::Ptr data = new juce::DynamicObject();
            data->setProperty("formatVersion", 1);
            data->setProperty("mode", processingMode.getSelectedId());
            data->setProperty("carrierSource", carrierSource.getSelectedId());
            data->setProperty("waveform", waveform.getSelectedId());
            data->setProperty("carrierPreset", carrierPreset.getSelectedId());
            data->setProperty("octave", octave.getSelectedId());
            data->setProperty("soundFontInstrument", soundFontInstrument.getSelectedId());
            data->setProperty("outputLevel", outputLevel.getValue());
            data->setProperty("attack", attackTime.getValue());
            data->setProperty("release", releaseTime.getValue());
            data->setProperty("lowFrequency", lowFrequency.getValue());
            data->setProperty("highFrequency", highFrequency.getValue());
            data->setProperty("dryWet", dryWetMix.getValue());
            data->setProperty("formantShift", formantShiftSlider.getValue());
            data->setProperty("gateThreshold", gateThresholdSlider.getValue());
            data->setProperty("articulation", articulationLiftSlider.getValue());
            data->setProperty("unvoicedNoise", unvoicedNoiseSlider.getValue());
            data->setProperty("sibilance", sibilanceSlider.getValue());
            data->setProperty("filterBands", filterBandCount.getSelectedId());
            data->setProperty("filterQ", filterQSlider.getValue());
            data->setProperty("formantPreservation", formantPreservationSlider.getValue());
            data->setProperty("transientProtection", transientProtectionSlider.getValue());
            data->setProperty("pitchSensitivity", pitchSensitivitySlider.getValue());
            data->setProperty("pitchHold", pitchHoldSlider.getValue());
            data->setProperty("pitchRecovery", pitchRecoverySlider.getValue());
            data->setProperty("microphoneMonitor", microphoneMonitor.getToggleState());
            data->setProperty("carrierPreview", carrierPreview.getToggleState());

            const auto json = juce::JSON::toString(juce::var(data.get()), true);
            if (!file.replaceWithText(json))
                statusLabel.setText("Could not save preset: " + file.getFullPathName(),
                                    juce::dontSendNotification);
            else
                statusLabel.setText("Preset saved: " + file.getFileName(),
                                    juce::dontSendNotification);
        });
}

void MainComponent::loadPreset()
{
    const auto presetDirectory = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory).getChildFile("Vocoder Presets");
    presetDirectory.createDirectory();
    presetFileChooser = std::make_unique<juce::FileChooser>(
        "Load Vocoder preset", presetDirectory,
        "*.vcpreset");
    presetFileChooser->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file == juce::File{})
                return;

            juce::var parsed;
            const auto result = juce::JSON::parse(file.loadFileAsString(), parsed);
            if (result.failed() || !parsed.isObject()
                || static_cast<int>(parsed.getProperty("formatVersion", 0)) != 1)
            {
                statusLabel.setText("Invalid or unsupported preset file.",
                                    juce::dontSendNotification);
                return;
            }

            applyPreset(parsed);
            statusLabel.setText("Preset loaded: " + file.getFileName(),
                                juce::dontSendNotification);
        });
}

void MainComponent::applyPreset(const juce::var& preset)
{
    const auto setSlider = [&preset](juce::Slider& slider, const juce::Identifier& key)
    {
        slider.setValue(static_cast<double>(preset.getProperty(key, slider.getValue())),
                        juce::sendNotificationSync);
    };

    processingMode.setSelectedId(juce::jlimit(1, 3,
        static_cast<int>(preset.getProperty("mode", processingMode.getSelectedId()))),
        juce::sendNotificationSync);
    carrierSource.setSelectedId(juce::jlimit(1, 2,
        static_cast<int>(preset.getProperty("carrierSource", carrierSource.getSelectedId()))),
        juce::sendNotificationSync);
    waveform.setSelectedId(juce::jlimit(1, 8,
        static_cast<int>(preset.getProperty("waveform", waveform.getSelectedId()))),
        juce::sendNotificationSync);
    octave.setSelectedId(juce::jlimit(1, 5,
        static_cast<int>(preset.getProperty("octave", octave.getSelectedId()))),
        juce::sendNotificationSync);
    const auto instrumentId = static_cast<int>(preset.getProperty(
        "soundFontInstrument", soundFontInstrument.getSelectedId()));
    if (instrumentId > 0 && instrumentId <= soundFontInstrument.getNumItems())
        soundFontInstrument.setSelectedId(instrumentId, juce::sendNotificationSync);

    setSlider(outputLevel, "outputLevel");
    setSlider(attackTime, "attack");
    setSlider(releaseTime, "release");
    setSlider(lowFrequency, "lowFrequency");
    setSlider(highFrequency, "highFrequency");
    setSlider(dryWetMix, "dryWet");
    setSlider(formantShiftSlider, "formantShift");
    setSlider(gateThresholdSlider, "gateThreshold");
    setSlider(articulationLiftSlider, "articulation");
    setSlider(unvoicedNoiseSlider, "unvoicedNoise");
    setSlider(sibilanceSlider, "sibilance");
    filterBandCount.setSelectedId(juce::jlimit(1, 5, static_cast<int>(preset.getProperty("filterBands", filterBandCount.getSelectedId()))), juce::sendNotificationSync);
    setSlider(filterQSlider, "filterQ");
    setSlider(formantPreservationSlider, "formantPreservation");
    setSlider(transientProtectionSlider, "transientProtection");
    setSlider(pitchSensitivitySlider, "pitchSensitivity");
    setSlider(pitchHoldSlider, "pitchHold");
    setSlider(pitchRecoverySlider, "pitchRecovery");
    carrierPreset.setSelectedId(juce::jlimit(1, 11,
        static_cast<int>(preset.getProperty("carrierPreset", 11))), juce::dontSendNotification);

    microphoneMonitor.setToggleState(
        static_cast<bool>(preset.getProperty("microphoneMonitor", false)),
        juce::sendNotificationSync);
    carrierPreview.setToggleState(
        static_cast<bool>(preset.getProperty("carrierPreview", false)),
        juce::sendNotificationSync);
    updateModeControls();
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &outputLevel)
    {
        const auto gain = static_cast<float>(outputLevel.getValue());
        outputGain.store(gain);
        vocoder.setOutputGain(gain);
    }
    else if (slider == &attackTime)
        vocoder.setAttackMs(static_cast<float>(attackTime.getValue()));
    else if (slider == &releaseTime)
        vocoder.setReleaseMs(static_cast<float>(releaseTime.getValue()));
    else if (slider == &lowFrequency)
    {
        const auto low = static_cast<float>(lowFrequency.getValue());
        const auto high = static_cast<float>(highFrequency.getValue());
        if (low >= high)
            highFrequency.setValue(juce::jmin(8000.0, low + 10.0), juce::dontSendNotification);
        vocoder.setFrequencyRange(low, static_cast<float>(highFrequency.getValue()));
    }
    else if (slider == &highFrequency)
    {
        const auto high = static_cast<float>(highFrequency.getValue());
        const auto low = static_cast<float>(lowFrequency.getValue());
        if (high <= low)
            lowFrequency.setValue(juce::jmax(120.0, high - 10.0), juce::dontSendNotification);
        vocoder.setFrequencyRange(static_cast<float>(lowFrequency.getValue()), high);
    }
    else if (slider == &dryWetMix)
    {
        const auto mix = static_cast<float>(dryWetMix.getValue() / 100.0);
        vocoder.setDryWet(mix);
        harmonizerMix.store(mix, std::memory_order_relaxed);
    }
    else if (slider == &formantShiftSlider)
        vocoder.setFormantShift(static_cast<float>(formantShiftSlider.getValue()));
    else if (slider == &gateThresholdSlider)
        vocoder.setGateThreshold(static_cast<float>(gateThresholdSlider.getValue()));
    else if (slider == &articulationLiftSlider)
        vocoder.setArticulationLift(static_cast<float>(articulationLiftSlider.getValue() / 100.0));
    else if (slider == &unvoicedNoiseSlider)
    {
        vocoder.setUnvoicedNoise(static_cast<float>(unvoicedNoiseSlider.getValue() / 100.0));
        if (!applyingCarrierPreset) carrierPreset.setSelectedId(11, juce::dontSendNotification);
    }
    else if (slider == &sibilanceSlider)
    {
        vocoder.setSibilanceAmount(static_cast<float>(sibilanceSlider.getValue() / 100.0));
        if (!applyingCarrierPreset) carrierPreset.setSelectedId(11, juce::dontSendNotification);
    }
    else if (slider == &filterQSlider)
        vocoder.setFilterQ(static_cast<float>(filterQSlider.getValue()));
    else if (slider == &formantPreservationSlider)
        harmonizer.setFormantPreservation(static_cast<float>(formantPreservationSlider.getValue() / 100.0));
    else if (slider == &transientProtectionSlider)
        harmonizer.setTransientProtection(static_cast<float>(transientProtectionSlider.getValue() / 100.0));
    else if (slider == &pitchSensitivitySlider)
        harmonizer.setPitchSensitivity(static_cast<float>(pitchSensitivitySlider.getValue() / 100.0));
    else if (slider == &pitchHoldSlider)
        harmonizer.setPitchHoldMs(static_cast<float>(pitchHoldSlider.getValue()));
    else if (slider == &pitchRecoverySlider)
        harmonizer.setPitchRecoveryMs(static_cast<float>(pitchRecoverySlider.getValue()));
}

void MainComponent::timerCallback()
{
    const auto input = inputPeak.exchange(0.0f, std::memory_order_relaxed);
    const auto output = outputPeak.exchange(0.0f, std::memory_order_relaxed);
    const auto toDb = [](float peak)
    {
        return peak > 0.000001f ? juce::Decibels::gainToDecibels(peak, -120.0f) : -120.0f;
    };
    inputMeterLabel.setText("Input: " + juce::String(toDb(input), 1) + " dBFS", juce::dontSendNotification);
    outputMeterLabel.setText("Output: " + juce::String(toDb(output), 1) + " dBFS", juce::dontSendNotification);
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        externalMidiCollector.reset(device->getCurrentSampleRate());
        testAudioTransport.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
        testAudioBuffer.setSize(1, juce::jmax(512, device->getCurrentBufferSizeSamples()), false, false, true);
        vocoder.prepare(device->getCurrentSampleRate());
        carrierSynth.setCurrentPlaybackSampleRate(device->getCurrentSampleRate());
        synthBuffer.setSize(1, juce::jmax(512, device->getCurrentBufferSizeSamples()), false, false, true);
        soundFontEngine.prepare(device->getCurrentSampleRate(),
                                juce::jmax(512, device->getCurrentBufferSizeSamples()));
        harmonizer.prepare(device->getCurrentSampleRate(),
                           juce::jmax(512, device->getCurrentBufferSizeSamples()));
        harmonizerBuffer.setSize(1, juce::jmax(512, device->getCurrentBufferSizeSamples()), false, false, true);
    }

    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MainComponent>(this),
                                     deviceName = device != nullptr ? device->getName() : juce::String{},
                                     driverType = device != nullptr ? device->getTypeName() : juce::String{},
                                     sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0,
                                     bufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0,
                                     inputLatency = device != nullptr ? device->getInputLatencyInSamples() : 0,
                                     outputLatency = device != nullptr ? device->getOutputLatencyInSamples() : 0]
    {
        if (safeThis != nullptr)
        {
            const auto message = deviceName.isNotEmpty()
                ? "Audio active: " + driverType + " / " + deviceName + " | " + juce::String(sampleRate, 0) + " Hz | "
                    + juce::String(bufferSize) + " samples | device-reported I/O "
                    + juce::String(1000.0 * static_cast<double>(inputLatency + outputLatency)
                                   / juce::jmax(1.0, sampleRate), 1) + " ms"
                    + " | Harmonizer lookahead ~70 ms"
                : "No audio device active";
            safeThis->statusLabel.setText(message, juce::dontSendNotification);
        }
    });
}

void MainComponent::audioDeviceStopped()
{
    vocoder.reset();
    harmonizer.reset();
    testAudioTransport.releaseResources();
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MainComponent>(this)]
    {
        if (safeThis != nullptr)
            safeThis->statusLabel.setText("Audio device stopped", juce::dontSendNotification);
    });
}

void MainComponent::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext&)
{
    // The buffer is sized in audioDeviceAboutToStart, never reallocated on the
    // real-time callback thread. A device should not exceed its reported block size.
    if (numSamples > synthBuffer.getNumSamples())
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        return;
    }

    synthBuffer.clear();
    midiBuffer.clear();
    externalMidiCollector.removeNextBlockOfMessages(midiBuffer, numSamples);
    keyboardState.processNextMidiBuffer(midiBuffer, 0, numSamples, true);
    if (useSoundFontCarrier.load(std::memory_order_relaxed))
        soundFontEngine.renderNextBlock(synthBuffer, midiBuffer);
    else
        carrierSynth.renderNextBlock(synthBuffer, midiBuffer, 0, numSamples);

    const float* prerecordedVoice = nullptr;
    if (useTestAudio.load(std::memory_order_relaxed) && testAudioBuffer.getNumSamples() >= numSamples)
    {
        testAudioBuffer.clear(0, 0, numSamples);
        juce::AudioSourceChannelInfo testInfo(&testAudioBuffer, 0, numSamples);
        testAudioTransport.getNextAudioBlock(testInfo);
        prerecordedVoice = testAudioBuffer.getReadPointer(0);
    }
    processAudio(inputChannelData, numInputChannels, outputChannelData, numOutputChannels,
                 numSamples, synthBuffer.getReadPointer(0), midiBuffer, prerecordedVoice);
}

void MainComponent::processAudio(const float* const* inputChannelData,
                                 int numInputChannels,
                                 float* const* outputChannelData,
                                 int numOutputChannels,
                                 int numSamples,
                                 const float* internalCarrier,
                                 const juce::MidiBuffer& midi,
                                 const float* prerecordedVoice)
{
    const auto gain = outputGain.load();
    const auto mode = selectedMode.load();
    const auto vocoderEnabled = mode == 1;
    const auto harmonizerEnabled = mode == 2;
    if (harmonizerEnabled && harmonizerBuffer.getNumSamples() >= numSamples)
        harmonizer.processBlock(prerecordedVoice != nullptr ? prerecordedVoice
                                : (numInputChannels > 0 ? inputChannelData[0] : nullptr),
                                harmonizerBuffer.getWritePointer(0), numSamples, midi,
                                harmonizerMix.load(std::memory_order_relaxed), gain,
                                harmonizerOctaveShift.load(std::memory_order_relaxed));
    const auto externalCarrier = selectedCarrier.load() == 1;
    const auto monitorMic = monitorMicrophone.load(std::memory_order_relaxed);
    const auto preview = previewCarrier.load(std::memory_order_relaxed);
    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto voice = prerecordedVoice != nullptr ? prerecordedVoice[sample]
            : (numInputChannels > 0 && inputChannelData[0] != nullptr
                ? inputChannelData[0][sample] : 0.0f);
        const auto external = numInputChannels > 1 && inputChannelData[1] != nullptr
            ? inputChannelData[1][sample] : 0.0f;
        const auto synth = internalCarrier != nullptr ? internalCarrier[sample] : 0.0f;
        blockInputPeak = juce::jmax(blockInputPeak, std::abs(voice));

        // Each processing mode must use its own engine output. In particular,
        // do not fall back to the dry microphone in Harmonizer mode: the
        // harmonizer buffer already contains the dry/wet mix and output gain.
        const auto vocoderOutput = vocoderEnabled
            ? vocoder.processSample(voice, external, externalCarrier, synth)
            : 0.0f;
        const auto harmonizerOutput = harmonizerEnabled
            ? harmonizerBuffer.getReadPointer(0)[sample]
            : 0.0f;
        const auto passThroughOutput = mode == 0 ? voice * gain : 0.0f;
        const auto microphoneOutput = monitorMic && vocoderEnabled ? voice * gain : 0.0f;
        const auto carrierSignal = externalCarrier ? external : synth;
        const auto carrierOutput = preview && !harmonizerEnabled ? carrierSignal * gain : 0.0f;
        const auto output = passThroughOutput + vocoderOutput + harmonizerOutput
                          + microphoneOutput + carrierOutput;

        blockOutputPeak = juce::jmax(blockOutputPeak, std::abs(output));
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] != nullptr)
                outputChannelData[channel][sample] = output;
        }
    }

    const auto updatePeak = [](std::atomic<float>& peak, float blockPeak)
    {
        auto previous = peak.load(std::memory_order_relaxed);
        while (blockPeak > previous
               && !peak.compare_exchange_weak(previous, blockPeak,
                                              std::memory_order_relaxed,
                                              std::memory_order_relaxed))
        {
        }
    };
    updatePeak(inputPeak, blockInputPeak);
    updatePeak(outputPeak, blockOutputPeak);
}
