#include "MainComponent.h"

MainComponent::MainComponent()
    : deviceSelector(deviceManager, 1, 2, 0, 2, true, false, true, false)
{
    setSize(900, 700);

    titleLabel.setText("VOCODER  |  REAL-TIME AUDIO", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
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

    carrierLabel.setText("Carrier source", juce::dontSendNotification);
    addAndMakeVisible(carrierLabel);
    carrierSource.addItem("Built-in chord", 1);
    carrierSource.addItem("External input 2", 2);
    carrierSource.setSelectedId(1);
    carrierSource.onChange = [this]
    {
        selectedCarrier.store(carrierSource.getSelectedId() == 2 ? 1 : 0);
    };
    addAndMakeVisible(carrierSource);

    outputLevelLabel.setText("Output level", juce::dontSendNotification);
    addAndMakeVisible(outputLevel);
    outputLevel.setRange(0.0, 2.0, 0.01);
    outputLevel.setValue(1.0);
    outputLevel.setTextValueSuffix(" x");
    outputLevel.setSliderStyle(juce::Slider::LinearHorizontal);
    outputLevel.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 28);
    outputLevel.addListener(this);
    addAndMakeVisible(outputLevel);

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
    outputLevel.removeListener(this);
    deviceManager.closeAudioDevice();
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
    modeLabel.setBounds(row.removeFromLeft(100));
    processingMode.setBounds(row.removeFromLeft(190));
    row.removeFromLeft(20);
    carrierLabel.setBounds(row.removeFromLeft(100));
    carrierSource.setBounds(row.removeFromLeft(190));

    area.removeFromTop(10);
    outputLevelLabel.setBounds(area.removeFromTop(24));
    outputLevel.setBounds(area.removeFromTop(36));
    area.removeFromTop(8);
    statusLabel.setBounds(area.removeFromTop(26));
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
        vocoder.prepare(device->getCurrentSampleRate());

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
    processAudio(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

void MainComponent::processAudio(const float* const* inputChannelData,
                                 int numInputChannels,
                                 float* const* outputChannelData,
                                 int numOutputChannels,
                                 int numSamples)
{
    const auto gain = outputGain.load();
    const auto vocoderEnabled = selectedMode.load() == 1;
    const auto externalCarrier = selectedCarrier.load() == 1;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto voice = numInputChannels > 0 && inputChannelData[0] != nullptr
            ? inputChannelData[0][sample] : 0.0f;
        const auto carrier = numInputChannels > 1 && inputChannelData[1] != nullptr
            ? inputChannelData[1][sample] : 0.0f;

        const auto processed = vocoderEnabled
            ? vocoder.processSample(voice, carrier, externalCarrier)
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
