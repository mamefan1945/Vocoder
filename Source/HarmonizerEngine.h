#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstdint>

class HarmonizerEngine
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void processBlock(const float* input, float* output, int numSamples,
                      const juce::MidiBuffer& midi, float dryWet, float outputGain) noexcept;

private:
    static constexpr int voiceCount = 8;
    static constexpr int ringSize = 8192;
    struct PitchVoice
    {
        std::array<float, ringSize> delay {};
        int writePosition = 0;
        int note = -1;
        float velocity = 0.0f;
        float phase = 0.0f;
        float envelope = 0.0f;
        void start(int midiNote, float noteVelocity) noexcept;
        void stop() noexcept;
        float process(float input, float sourceHz, double sampleRate) noexcept;
    };

    std::array<PitchVoice, voiceCount> voices {};
    std::array<float, 1024> pitchHistory {};
    int historyWrite = 0;
    int pitchUpdateCounter = 0;
    float detectedPitchHz = 0.0f;
    double currentSampleRate = 44100.0;
    float estimatePitch() const noexcept;
    void handleMidi(const juce::MidiBuffer& midi) noexcept;
};
