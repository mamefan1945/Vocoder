#pragma once

#include <JuceHeader.h>
#include "CarrierSynth.h"
#include "VocoderEngine.h"
#include "SoundFontEngine.h"
#include "HarmonizerEngine.h"

class MainComponent final : public juce::Component,
                            public juce::AudioIODeviceCallback,
                            private juce::Slider::Listener,
                            private juce::Timer
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
    class LockableMidiKeyboard final : public juce::MidiKeyboardComponent
    {
    public:
        LockableMidiKeyboard(juce::MidiKeyboardState& state)
            : juce::MidiKeyboardComponent(state, juce::MidiKeyboardComponent::horizontalKeyboard),
              keyboardState(state)
        {
        }

        void mouseDown(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;

    private:
        int noteAt(juce::Point<float> position) const;
        static bool isBlackKey(int note) noexcept;
        juce::MidiKeyboardState& keyboardState;
        std::array<bool, 128> lockedNotes {};
        bool suppressMouseSequence = false;
    };

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
    void timerCallback() override;
    void selectMidiInput();
    void refreshMidiInputs();
    void updateSoundFontControls();
    void populateSoundFontInstruments();
    void chooseSoundFont();
    void processAudio(const float* const* inputChannelData,
                      int numInputChannels,
                      float* const* outputChannelData,
                      int numOutputChannels,
                      int numSamples,
                      const float* internalCarrier,
                      const juce::MidiBuffer& midi);

    juce::AudioDeviceManager deviceManager;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Viewport deviceViewport;
    juce::MidiKeyboardState keyboardState;
    MidiInputRouter midiInputRouter;
    LockableMidiKeyboard keyboardComponent;
    juce::StringArray midiInputIdentifiers;

    juce::ComboBox processingMode;
    juce::ComboBox carrierSource;
    juce::ComboBox midiInput;
    juce::TextButton refreshMidiButton { "Refresh" };
    juce::ComboBox waveform;
    juce::ComboBox octave;
    juce::ComboBox soundFontInstrument;
    juce::Label soundFontInstrumentLabel;
    juce::TextButton loadSoundFontButton { "Load SF2..." };
    std::unique_ptr<juce::FileChooser> soundFontFileChooser;
    juce::Label modeLabel;
    juce::Label carrierLabel;
    juce::Label midiLabel;
    juce::Label waveformLabel;
    juce::Label octaveLabel;
    juce::Slider outputLevel;
    juce::Label outputLevelLabel;
    juce::Slider attackTime;
    juce::Slider releaseTime;
    juce::Slider lowFrequency;
    juce::Slider highFrequency;
    juce::Slider dryWetMix;
    juce::Slider formantShiftSlider;
    juce::Slider gateThresholdSlider;
    juce::Label formantShiftLabel;
    juce::Label gateThresholdLabel;
    juce::Label inputMeterLabel;
    juce::Label outputMeterLabel;
    juce::ToggleButton microphoneMonitor;
    juce::ToggleButton carrierPreview;
    juce::Label attackLabel;
    juce::Label releaseLabel;
    juce::Label lowFrequencyLabel;
    juce::Label highFrequencyLabel;
    juce::Label dryWetLabel;
    juce::Label statusLabel;
    juce::Label titleLabel;

    CarrierSynth carrierSynth;
    SoundFontEngine soundFontEngine;
    juce::MidiBuffer midiBuffer;
    juce::AudioBuffer<float> synthBuffer;
    VocoderEngine vocoder;
    HarmonizerEngine harmonizer;
    juce::AudioBuffer<float> harmonizerBuffer;
    juce::String activeMidiIdentifier;

    std::atomic<float> outputGain { 1.0f };
    std::atomic<float> harmonizerMix { 1.0f };
    std::atomic<int> harmonizerOctaveShift { 0 };
    std::atomic<float> inputPeak { 0.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::atomic<int> selectedMode { 0 };
    std::atomic<int> selectedCarrier { 0 };
    std::atomic<bool> useSoundFontCarrier { false };
    std::atomic<bool> monitorMicrophone { false };
    std::atomic<bool> previewCarrier { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
