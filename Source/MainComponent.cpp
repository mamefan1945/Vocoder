#include "MainComponent.h"

MainComponent::MainComponent()
    : deviceSelector(deviceManager, 1, 2, 0, 2, false, false, true, false),
      midiInputRouter(keyboardState),
      keyboardComponent(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setSize(960, 780);

    titleLabel.setText("VOCODER  |  REAL-TIME AUDIO", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    modeLabel.setText("Processing", juce::dontSendNotification);
    addAndMakeVisible(modeLabel);
    processingMode.addItem("Pass-through", 1);
    processingMode.addItem("Vocoder", 2);
    processingMode.setSelectedId(1);
    processingMode.onChange = [this]
    {
        selectedMode.store(processingMode.getSelectedId() == 2 ? 1 : 0);
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
    midiInput.addItem("No MIDI input", 1);
    const auto midiDevices = juce::MidiInput::getAvailableDevices();
    midiInputIdentifiers.clear();
    for (int i = 0; i < midiDevices.size(); ++i)
    {
        midiInputIdentifiers.add(midiDevices.getReference(i).identifier);
        midiInput.addItem(midiDevices.getReference(i).name, i + 2);
    }
    midiInput.setSelectedId(1);
    midiInput.onChange = [this] { selectMidiInput(); };
    addAndMakeVisible(midiInput);

    waveformLabel.setText("Waveform", juce::dontSendNotification);
    addAndMakeVisible(waveformLabel);
    waveform.addItem("Sine", 1);
    waveform.addItem("Saw", 2);
    waveform.addItem("Square", 3);
    waveform.addItem("Triangle", 4);
    waveform.addItem("Noise", 5);
    waveform.setSelectedId(1);
    waveform.onChange = [this] { carrierSynth.setWaveform(waveform.getSelectedId() - 1); };
    addAndMakeVisible(waveform);

    octaveLabel.setText("Octave", juce::dontSendNotification);
    addAndMakeVisible(octaveLabel);
    octave.addItem("-2", 1);
    octave.addItem("-1", 2);
    octave.addItem("0", 3);
    octave.addItem("+1", 4);
    octave.addItem("+2", 5);
    octave.setSelectedId(3);
    octave.onChange = [this] { carrierSynth.setOctave(octave.getSelectedId() - 3); };
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

    keyboardComponent.setAvailableRange(48, 84);
    keyboardComponent.setKeyWidth(24.0f);
    addAndMakeVisible(keyboardComponent);

    statusLabel.setText("Initializing audio devices...", juce::dontSendNotification);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(deviceSelector);

    const auto error = deviceManager.initialise(2, 2, nullptr, true);
    if (error.isNotEmpty())
        statusLabel.setText("Audio initialization: " + error, juce::dontSendNotification);

    deviceManager.addAudioCallback(this);
}

MainComponent::~MainComponent()
{
    deviceManager.removeAudioCallback(this);
    if (activeMidiIdentifier.isNotEmpty())
        deviceManager.removeMidiInputDeviceCallback(activeMidiIdentifier, &midiInputRouter);
    outputLevel.removeListener(this);
    deviceManager.closeAudioDevice();
}

void MainComponent::selectMidiInput()
{
    if (activeMidiIdentifier.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback(activeMidiIdentifier, &midiInputRouter);
        activeMidiIdentifier.clear();
    }

    const auto selectedIndex = midiInput.getSelectedId() - 2;
    if (!juce::isPositiveAndBelow(selectedIndex, midiInputIdentifiers.size()))
        return;

    activeMidiIdentifier = midiInputIdentifiers[selectedIndex];
    deviceManager.setMidiInputDeviceEnabled(activeMidiIdentifier, true);
    deviceManager.addMidiInputDeviceCallback(activeMidiIdentifier, &midiInputRouter);
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
    processingMode.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(14);
    carrierLabel.setBounds(row.removeFromLeft(58));
    carrierSource.setBounds(row.removeFromLeft(170));
    row.removeFromLeft(14);
    midiLabel.setBounds(row.removeFromLeft(72));
    midiInput.setBounds(row.removeFromLeft(190));

    area.removeFromTop(12);
    row = area.removeFromTop(32);
    waveformLabel.setBounds(row.removeFromLeft(70));
    waveform.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(18);
    octaveLabel.setBounds(row.removeFromLeft(55));
    octave.setBounds(row.removeFromLeft(90));

    area.removeFromTop(8);
    outputLevelLabel.setBounds(area.removeFromTop(22));
    outputLevel.setBounds(area.removeFromTop(34));
    area.removeFromTop(10);
    keyboardComponent.setBounds(area.removeFromTop(110));
    area.removeFromTop(8);
    statusLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    deviceSelector.setBounds(area);
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &outputLevel)
    {
        const auto gain = static_cast<float>(outputLevel.getValue());
        outputGain.store(gain);
        vocoder.setOutputGain(gain);
    }
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        vocoder.prepare(device->getCurrentSampleRate());
        carrierSynth.setCurrentPlaybackSampleRate(device->getCurrentSampleRate());
        synthBuffer.setSize(1, juce::jmax(512, device->getCurrentBufferSizeSamples()), false, false, true);
    }

    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<MainComponent>(this),
                                     deviceName = device != nullptr ? device->getName() : juce::String{}]
    {
        if (safeThis != nullptr)
        {
            const auto message = deviceName.isNotEmpty()
                ? "Audio active: " + deviceName
                : "No audio device active";
            safeThis->statusLabel.setText(message, juce::dontSendNotification);
        }
    });
}

void MainComponent::audioDeviceStopped()
{
    vocoder.reset();
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
    keyboardState.processNextMidiBuffer(midiBuffer, 0, numSamples, true);
    carrierSynth.renderNextBlock(synthBuffer, midiBuffer, 0, numSamples);

    processAudio(inputChannelData, numInputChannels, outputChannelData, numOutputChannels,
                 numSamples, synthBuffer.getReadPointer(0));
}

void MainComponent::processAudio(const float* const* inputChannelData,
                                 int numInputChannels,
                                 float* const* outputChannelData,
                                 int numOutputChannels,
                                 int numSamples,
                                 const float* internalCarrier)
{
    const auto gain = outputGain.load();
    const auto vocoderEnabled = selectedMode.load() == 1;
    const auto externalCarrier = selectedCarrier.load() == 1;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto voice = numInputChannels > 0 && inputChannelData[0] != nullptr
            ? inputChannelData[0][sample] : 0.0f;
        const auto external = numInputChannels > 1 && inputChannelData[1] != nullptr
            ? inputChannelData[1][sample] : 0.0f;
        const auto synth = internalCarrier != nullptr ? internalCarrier[sample] : 0.0f;

        const auto processed = vocoderEnabled
            ? vocoder.processSample(voice, external, externalCarrier, synth)
            : voice * gain;

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] != nullptr)
            {
                const auto passThrough = !vocoderEnabled && channel < numInputChannels
                    && inputChannelData[channel] != nullptr;

                outputChannelData[channel][sample] = passThrough
                    ? inputChannelData[channel][sample] * gain
                    : processed;
            }
        }
    }
}
