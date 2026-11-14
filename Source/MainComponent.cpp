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

MainComponent::MainComponent()
    : deviceSelector(deviceManager, 1, 2, 0, 2, false, false, true, false),
      midiInputRouter(keyboardState, externalMidiCollector),
      keyboardComponent(keyboardState)
{
    setSize(1080, 740);

    titleLabel.setText("VOCODER  |  REAL-TIME AUDIO", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

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
    refreshMidiInputs();

    waveformLabel.setText("Carrier type", juce::dontSendNotification);
    addAndMakeVisible(waveformLabel);
    waveform.addItem("Sine", 1);
    waveform.addItem("Saw", 2);
    waveform.addItem("Square", 3);
    waveform.addItem("Triangle", 4);
    waveform.addItem("Noise", 5);
    waveform.addItem("Rich (PWM)", 6);
    waveform.addItem("Angelic Choir", 7);
    waveform.addItem("SoundFont (GM)", 8);
    waveform.setSelectedId(2);
    waveform.onChange = [this]
    {
        const auto selected = waveform.getSelectedId();
        useSoundFontCarrier.store(selected == 8, std::memory_order_relaxed);
        if (selected != 8)
            carrierSynth.setWaveform(selected - 1);
        updateSoundFontControls();
    };
    addAndMakeVisible(waveform);

    soundFontInstrumentLabel.setText("Instrument", juce::dontSendNotification);
    addAndMakeVisible(soundFontInstrumentLabel);
    soundFontInstrument.onChange = [this]
    {
        soundFontEngine.selectPreset(soundFontInstrument.getSelectedId() - 1);
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
    configureControl(formantPreservationSlider, formantPreservationLabel, "Formant preserve", 0.0, 100.0, 1.0, 100.0, " %");
    configureControl(transientProtectionSlider, transientProtectionLabel, "Transient protect", 0.0, 100.0, 1.0, 100.0, " %");
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

    startTimerHz(20);

    keyboardComponent.setAvailableRange(36, 108);
    keyboardComponent.setKeyWidth(24.0f);
    addAndMakeVisible(keyboardComponent);

    statusLabel.setText("Initializing audio devices...", juce::dontSendNotification);
    addAndMakeVisible(statusLabel);
    deviceViewport.setViewedComponent(&deviceSelector, false);
    deviceViewport.setScrollBarsShown(true, false);
    deviceViewport.setScrollOnDragEnabled(true);
    addAndMakeVisible(deviceViewport);

    const auto defaultSoundFont = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getSiblingFile("SoundFonts")
        .getChildFile("GeneralUser-GS.sf2");
    juce::String soundFontError;
    if (soundFontEngine.load(defaultSoundFont, soundFontError))
        populateSoundFontInstruments();
    else
        statusLabel.setText("Default SoundFont unavailable: " + soundFontError,
                            juce::dontSendNotification);

    const auto error = deviceManager.initialise(2, 2, nullptr, true);
    if (error.isNotEmpty())
        statusLabel.setText("Audio initialization: " + error, juce::dontSendNotification);

    deviceManager.addAudioCallback(this);
    updateModeControls();
}

MainComponent::~MainComponent()
{
    deviceManager.removeAudioCallback(this);
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
    stopTimer();
    deviceManager.closeAudioDevice();
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
    show(formantPreservationLabel, harmonizerMode);
    show(formantPreservationSlider, harmonizerMode);
    show(transientProtectionLabel, harmonizerMode);
    show(transientProtectionSlider, harmonizerMode);
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
    area.removeFromTop(10);
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
        carrierSource.setBounds(row.removeFromLeft(170));
        row.removeFromLeft(14);
        waveformLabel.setBounds(row.removeFromLeft(82));
        waveform.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(12);
        octaveLabel.setBounds(row.removeFromLeft(55));
        octave.setBounds(row.removeFromLeft(90));
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
        vocoder.setUnvoicedNoise(static_cast<float>(unvoicedNoiseSlider.getValue() / 100.0));
    else if (slider == &formantPreservationSlider)
        harmonizer.setFormantPreservation(static_cast<float>(formantPreservationSlider.getValue() / 100.0));
    else if (slider == &transientProtectionSlider)
        harmonizer.setTransientProtection(static_cast<float>(transientProtectionSlider.getValue() / 100.0));
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
                                     sampleRate = device != nullptr ? device->getCurrentSampleRate() : 0.0,
                                     bufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0,
                                     inputLatency = device != nullptr ? device->getInputLatencyInSamples() : 0,
                                     outputLatency = device != nullptr ? device->getOutputLatencyInSamples() : 0]
    {
        if (safeThis != nullptr)
        {
            const auto message = deviceName.isNotEmpty()
                ? "Audio active: " + deviceName + " | " + juce::String(sampleRate, 0) + " Hz | "
                    + juce::String(bufferSize) + " samples | device-reported I/O "
                    + juce::String(1000.0 * static_cast<double>(inputLatency + outputLatency)
                                   / juce::jmax(1.0, sampleRate), 1) + " ms"
                    + " | Harmonizer lookahead ~40 ms"
                : "No audio device active";
            safeThis->statusLabel.setText(message, juce::dontSendNotification);
        }
    });
}

void MainComponent::audioDeviceStopped()
{
    vocoder.reset();
    harmonizer.reset();
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

    processAudio(inputChannelData, numInputChannels, outputChannelData, numOutputChannels,
                 numSamples, synthBuffer.getReadPointer(0), midiBuffer);
}

void MainComponent::processAudio(const float* const* inputChannelData,
                                 int numInputChannels,
                                 float* const* outputChannelData,
                                 int numOutputChannels,
                                 int numSamples,
                                 const float* internalCarrier,
                                 const juce::MidiBuffer& midi)
{
    const auto gain = outputGain.load();
    const auto mode = selectedMode.load();
    const auto vocoderEnabled = mode == 1;
    const auto harmonizerEnabled = mode == 2;
    if (harmonizerEnabled && harmonizerBuffer.getNumSamples() >= numSamples)
        harmonizer.processBlock(numInputChannels > 0 ? inputChannelData[0] : nullptr,
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
        const auto voice = numInputChannels > 0 && inputChannelData[0] != nullptr
            ? inputChannelData[0][sample] : 0.0f;
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
