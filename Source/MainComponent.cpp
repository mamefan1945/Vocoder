#include "MainComponent.h"

MainComponent::MainComponent()
    : deviceSelector(deviceManager, 0, 2, 0, 2, true, false, true, false)
{
    setSize(900, 620);

    titleLabel.setText("VOCODER  |  AUDIO I/O TEST", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    outputLevelLabel.setText("Output level", juce::dontSendNotification);
    addAndMakeVisible(outputLevelLabel);

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
    area.removeFromTop(14);
    outputLevelLabel.setBounds(area.removeFromTop(24));
    outputLevel.setBounds(area.removeFromTop(38));
    area.removeFromTop(12);
    statusLabel.setBounds(area.removeFromTop(26));
    area.removeFromTop(8);
    deviceSelector.setBounds(area);
}

void MainComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &outputLevel)
        outputGain.store(static_cast<float>(outputLevel.getValue()));
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
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

    for (int output = 0; output < numOutputChannels; ++output)
    {
        auto* destination = outputChannelData[output];
        if (destination == nullptr)
            continue;

        const auto input = numInputChannels > 0 ? juce::jmin(output, numInputChannels - 1) : -1;

        if (input >= 0 && inputChannelData[input] != nullptr)
            juce::FloatVectorOperations::copy(destination, inputChannelData[input], numSamples);
        else
            juce::FloatVectorOperations::clear(destination, numSamples);

        if (gain != 1.0f)
            juce::FloatVectorOperations::multiply(destination, gain, numSamples);
    }
}
