#pragma once

#include <JuceHeader.h>
#include "VocoderEngine.h"

class MainComponent final : public juce::Component,
                            public juce::AudioIODeviceCallback,
                            private juce::Slider::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    void sliderValueChanged(juce::Slider* slider) override;
    void processAudio(const float* const* inputChannelData,
                      int numInputChannels,
                      float* const* outputChannelData,
                      int numOutputChannels,
                      int numSamples);

    juce::AudioDeviceManager deviceManager;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::ComboBox processingMode;
    juce::ComboBox carrierSource;
    juce::Label modeLabel;
    juce::Label carrierLabel;
    juce::Slider outputLevel;
    juce::Label outputLevelLabel;
    juce::Label statusLabel;
    juce::Label titleLabel;

    VocoderEngine vocoder;
    std::atomic<float> outputGain { 1.0f };
    std::atomic<int> selectedMode { 0 };
    std::atomic<int> selectedCarrier { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
