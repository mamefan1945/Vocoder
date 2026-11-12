#pragma once
#include <JuceHeader.h>
#include <array>
#include <cstdint>
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
    static constexpr size_t inputRingSize = 16384;
    static constexpr size_t outputRingSize = 32768;
    static constexpr size_t inputRingMask = inputRingSize - 1;
    static constexpr size_t outputRingMask = outputRingSize - 1;
    static constexpr int pitchWindowSize = 1024;

    struct PitchVoice
    {
        std::vector<float> overlap;
        std::vector<float> weights;
        int note = -1;
        float velocity = 0.0f;
        float envelope = 0.0f;
        double nextSynthesisMark = 0.0;
        bool synthesisMarkInitialized = false;
        bool releasing = false;

        void prepare();
        void start(int midiNote, float noteVelocity) noexcept;
        void stop() noexcept;
    };

    std::array<PitchVoice, voiceCount> voices {};
    std::array<float, inputRingSize> inputHistory {};
    int maximumBlockSize = 2048;
    double currentSampleRate = 44100.0;
    uint64_t totalSamples = 0;
    int pitchUpdateCounter = 0;
    int64_t previousPitchMark = -1;
    double previousPitchPeriod = 0.0;
    double nextExpectedPitchMark = 0.0;
    float detectedPitchHz = 0.0f;
    float voicedMix = 0.0f;
    bool havePitchMark = false;
    int latencySamples = 0;

    float estimatePitch() const noexcept;
    void detectAndProcessPitchMark() noexcept;
    void processPitchMark(int64_t mark, double sourcePeriod) noexcept;
    void scheduleGrain(PitchVoice& voice, int64_t sourceCentre,
                       double sourcePeriod, int64_t synthesisCentre,
                       double targetPeriod) noexcept;
    void handleMidi(const juce::MidiBuffer& midi) noexcept;
};
