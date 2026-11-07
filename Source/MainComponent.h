#pragma once

#include <JuceHeader.h>
#include "CarrierSynth.h"
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
    class MidiInputRouter final : public juce::MidiInputCallback
    {
    public:
        explicit MidiInputRouter(juce::MidiKeyboardState& state) : keyboardState(state) {}
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
        {
            keyboardState.processNextMidiEvent(message);
        }
    private:
        juce::MidiKeyboardState& keyboardState;
    };

    void sliderValueChanged(juce::Slider* slider) override;
    void selectMidiInput();
    void processAudio(const float* const* inputChannelData,
                      int numInputChannels,
                      float* const* outputChannelData,
                      int numOutputChannels,
                      int numSamples,
                      const float* internalCarrier);

    juce::AudioDeviceManager deviceManager;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::MidiKeyboardState keyboardState;
    MidiInputRouter midiInputRouter;
    juce::MidiKeyboardComponent keyboardComponent;
    juce::StringArray midiInputIdentifiers;

    juce::ComboBox processingMode;
    juce::ComboBox carrierSource;
    juce::ComboBox midiInput;
    juce::ComboBox waveform;
    juce::ComboBox octave;
    juce::Label modeLabel;
    juce::Label carrierLabel;
    juce::Label midiLabel;
    juce::Label waveformLabel;
    juce::Label octaveLabel;
    juce::Slider outputLevel;
    juce::Label outputLevelLabel;
    juce::Label statusLabel;
    juce::Label titleLabel;

    CarrierSynth carrierSynth;
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> synthBuffer;
    VocoderEngine vocoder;
    juce::String activeMidiIdentifier;

    std::atomic<float> outputGain { 1.0f };
    std::atomic<int> selectedMode { 0 };
    std::atomic<int> selectedCarrier { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
