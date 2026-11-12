#pragma once
#include <JuceHeader.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <array>
#include <vector>

class HarmonizerEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize = 2048) noexcept;
    void reset() noexcept;
    void processBlock(const float* input, float* output, int numSamples,
                      const juce::MidiBuffer& midi, float dryWet, float outputGain) noexcept;

private:
    static constexpr int voiceCount = 8;
    struct PitchVoice
    {
        signalsmith::stretch::SignalsmithStretch<float> shifter;
        std::vector<float> shifted;
        int note = -1;
        float velocity = 0.0f;
        float envelope = 0.0f;
        bool releasing = false;

        void prepare(double sampleRate, int maximumBlockSize);
        void start(int midiNote, float noteVelocity) noexcept;
        void stop() noexcept;
    };

    std::array<PitchVoice, voiceCount> voices {};
    std::array<float, 1024> pitchHistory {};
    std::vector<float> silence;
    int historyWrite = 0;
    int pitchUpdateCounter = 0;
    int historySamples = 0;
    int maximumBlockSize = 2048;
    float detectedPitchHz = 0.0f;
    double currentSampleRate = 44100.0;

    float estimatePitch() const noexcept;
    void handleMidi(const juce::MidiBuffer& midi) noexcept;
};
