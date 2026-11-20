#pragma once

#include <JuceHeader.h>
#include "CarrierSynth.h"
#include "VocoderEngine.h"
#include "SoundFontEngine.h"
#include "HarmonizerEngine.h"
#include <thread>
#include <vector>

class MainComponent final : public juce::Component,
                            public juce::AudioIODeviceCallback,
                            private juce::Slider::Listener,
                            private juce::Timer
{
public:
    explicit MainComponent(std::function<void()> onStartupComplete = {});
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
            // Let the on-screen MIDI keyboard regain keyboard focus when clicked.
            setWantsKeyboardFocus(true);
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
        MidiInputRouter(juce::MidiKeyboardState& state, juce::MidiMessageCollector& collector)
            : keyboardState(state), midiCollector(collector) {}
        void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override
        {
            keyboardState.processNextMidiEvent(message);
            // MidiKeyboardState tracks note keys, not continuous controller data.
            // Queue pitch wheel separately so it reaches the audio thread and DSP.
            if (message.isPitchWheel())
                midiCollector.addMessageToQueue(message);
        }
    private:
        juce::MidiKeyboardState& keyboardState;
        juce::MidiMessageCollector& midiCollector;
    };

    struct ControlInfo
    {
        juce::Component* target = nullptr;
        juce::TextButton* button = nullptr;
    };
    juce::OwnedArray<juce::TextButton> controlInfoButtons;
    std::vector<ControlInfo> controlInfo;
    void addControlInfo(juce::Component& target, const juce::String& title,
                        const juce::String& details);

    void sliderValueChanged(juce::Slider* slider) override;
    void timerCallback() override;
    void selectMidiInput();
    void refreshMidiInputs();
    void initializeAudioAndInstruments();
    void updateSoundFontControls();
    void updateModeControls();
    void populateSoundFontInstruments();
    void chooseSoundFont();
    void chooseTestAudio();
    bool loadTestAudioFile(const juce::File& file, bool loop);
    juce::File findSpeechTestFile(const juce::String& voiceName) const;
    juce::File findHarmonizerTestFile() const;
    void startSpeechTest();
    void stopSpeechTest();
    void savePreset();
    void loadPreset();
    void applyPreset(const juce::var& preset);
    void processAudio(const float* const* inputChannelData,
                      int numInputChannels,
                      float* const* outputChannelData,
                      int numOutputChannels,
                      int numSamples,
                      const float* internalCarrier,
                      const juce::MidiBuffer& midi,
                      const float* prerecordedVoice);

    juce::AudioDeviceManager deviceManager;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Viewport deviceViewport;
    juce::MidiKeyboardState keyboardState;
    juce::MidiMessageCollector externalMidiCollector;
    MidiInputRouter midiInputRouter;
    LockableMidiKeyboard keyboardComponent;
    juce::StringArray midiInputIdentifiers;

    juce::ComboBox processingMode;
    juce::ComboBox carrierSource;
    juce::ComboBox midiInput;
    juce::TextButton refreshMidiButton { "Refresh" };
    juce::Label presetLabel;
    juce::TextButton savePresetButton { "Save preset..." };
    juce::TextButton loadPresetButton { "Load preset..." };
    std::unique_ptr<juce::FileChooser> presetFileChooser;
    juce::ComboBox waveform;
    juce::ComboBox carrierPreset;
    juce::ComboBox octave;
    juce::ComboBox harmonizerAlgorithm;
    juce::Label harmonizerAlgorithmLabel;
    juce::ComboBox soundFontInstrument;
    juce::Label soundFontInstrumentLabel;
    juce::TextButton loadSoundFontButton { "Load SF2..." };
    std::unique_ptr<juce::FileChooser> soundFontFileChooser;
    juce::TextButton loadTestAudioButton { "Load test WAV..." };
    juce::TextButton playTestAudioButton { "Play" };
    juce::TextButton stopTestAudioButton { "Stop" };
    juce::ToggleButton useTestAudioInput { "Use file as voice input" };
    juce::ComboBox testVoiceSelector;
    juce::TextButton runSpeechTestButton { "Run speech test" };
    std::unique_ptr<juce::FileChooser> testAudioFileChooser;
    juce::AudioFormatManager testAudioFormats;
    juce::TimeSliceThread testAudioReadAheadThread { "Vocoder test audio read-ahead" };
    juce::AudioTransportSource testAudioTransport;
    std::unique_ptr<juce::AudioFormatReaderSource> testAudioReaderSource;
    juce::AudioBuffer<float> testAudioBuffer;
    juce::MidiMessageSequence vocalMidiSequence;
    int vocalMidiEventIndex = 0;
    double lastVocalMidiPosition = -1.0;
    bool useVocalMidiTest = false;
    juce::Label modeLabel;
    juce::Label carrierLabel;
    juce::Label midiLabel;
    juce::Label waveformLabel;
    juce::Label carrierPresetLabel;
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
    juce::Slider articulationLiftSlider;
    juce::Slider unvoicedNoiseSlider;
    juce::Slider sibilanceSlider;
    juce::ComboBox filterBandCount;
    juce::Label filterBandCountLabel;
    juce::Slider filterQSlider;
    juce::Label filterQLabel;
    juce::Slider formantPreservationSlider;
    juce::Slider transientProtectionSlider;
    juce::Slider pitchSensitivitySlider;
    juce::Slider pitchHoldSlider;
    juce::Slider pitchRecoverySlider;
    juce::Label formantShiftLabel;
    juce::Label gateThresholdLabel;
    juce::Label articulationLiftLabel;
    juce::Label unvoicedNoiseLabel;
    juce::Label sibilanceLabel;
    juce::Label formantPreservationLabel;
    juce::Label transientProtectionLabel;
    juce::Label pitchSensitivityLabel;
    juce::Label pitchHoldLabel;
    juce::Label pitchRecoveryLabel;
    juce::Label inputMeterLabel;
    juce::Label outputMeterLabel;
    juce::Label harmonizerPitchLabel;
    juce::Label harmonizerConfidenceLed;
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
    std::thread startupThread;
    std::atomic<bool> shuttingDown { false };
    std::function<void()> startupCompleteCallback;

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
    std::atomic<bool> useTestAudio { false };
    bool speechTestRunning = false;
    bool applyingCarrierPreset = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
