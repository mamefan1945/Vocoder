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
                      const juce::MidiBuffer& midi, float dryWet, float outputGain,
                      int octaveShift = 0) noexcept;

private:
    static constexpr int voiceCount = 8;
    static constexpr size_t inputRingSize = 16384;
    static constexpr size_t outputRingSize = 32768;
    static constexpr size_t inputRingMask = inputRingSize - 1;
    static constexpr size_t outputRingMask = outputRingSize - 1;
    static constexpr int pitchWindowSize = 1024;
    static constexpr int lpcOrder = 20;
    static constexpr int lpcFrameSize = 2048;

    struct PitchVoice
    {
        std::vector<float> overlap;
        std::vector<float> weights;
        int note = -1;
        int sourceNote = -1;
        float velocity = 0.0f;
        float envelope = 0.0f;
        double nextSynthesisMark = 0.0;
        bool synthesisMarkInitialized = false;
        bool releasing = false;
        std::array<float, lpcOrder> formantState {};

        void prepare();
        void start(int midiNote, int originalMidiNote, float noteVelocity) noexcept;
        void stop() noexcept;
    };

    std::array<PitchVoice, voiceCount> voices {};
    std::array<float, inputRingSize> inputHistory {};
    std::array<float, inputRingSize> residualHistory {};
    std::array<std::array<float, inputRingSize>, lpcOrder> formantCoefficientHistory {};
    std::array<float, lpcOrder> formantCoefficients {};
    std::array<float, lpcOrder> targetFormantCoefficients {};
    int maximumBlockSize = 2048;
    double currentSampleRate = 44100.0;
    uint64_t totalSamples = 0;
    int pitchUpdateCounter = 0;
    int formantUpdateCounter = 0;
    int64_t previousPitchMark = -1;
    double previousPitchPeriod = 0.0;
    double nextExpectedPitchMark = 0.0;
    float detectedPitchHz = 0.0f;
    float voicedMix = 0.0f;
    bool havePitchMark = false;
    float pitchMarkPolarity = 1.0f;
    float fastEnergy = 0.0f;
    float slowEnergy = 0.0f;
    float slopeEnergy = 0.0f;
    float previousInputSample = 0.0f;
    float transientMix = 0.0f;
    int transientHoldSamples = 0;
    int transientCooldownSamples = 0;
    float pitchBendSemitones = 0.0f;
    float targetPitchBendSemitones = 0.0f;
    int latencySamples = 0;

    float estimatePitch() const noexcept;
    void estimateFormantEnvelope() noexcept;
    void detectAndProcessPitchMark() noexcept;
    void processPitchMark(int64_t mark, double sourcePeriod) noexcept;
    void scheduleGrain(PitchVoice& voice, int64_t sourceCentre,
                       double sourcePeriod, int64_t synthesisCentre) noexcept;
    void handleMidi(const juce::MidiBuffer& midi, int octaveShift) noexcept;
};
