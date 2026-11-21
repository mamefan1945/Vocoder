#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class HarmonizerEngine
{
public:
    enum class Algorithm : int { timeDomain = 0, lpc = 1, frequencyDomain = 2 };
    void setAlgorithm(Algorithm algorithm) noexcept { selectedAlgorithm.store(static_cast<int>(algorithm), std::memory_order_relaxed); }
    Algorithm getAlgorithm() const noexcept { return static_cast<Algorithm>(selectedAlgorithm.load(std::memory_order_relaxed)); }
    void prepare(double sampleRate, int maximumBlockSize = 2048) noexcept;
    void reset() noexcept;
    void setFormantPreservation(float amount) noexcept;
    void setTransientProtection(float amount) noexcept;
    void setPitchSensitivity(float amount) noexcept;
    void setPitchHoldMs(float milliseconds) noexcept;
    void setPitchRecoveryMs(float milliseconds) noexcept;
    float getDetectedPitchHz() const noexcept { return publishedPitchHz.load(std::memory_order_relaxed); }
    float getPitchConfidence() const noexcept { return publishedPitchConfidence.load(std::memory_order_relaxed); }
    float getGrainCoverage() const noexcept { return publishedGrainCoverage.load(std::memory_order_relaxed); }
    float getFdInversePeak() const noexcept { return publishedFdInversePeak.load(std::memory_order_relaxed); }
    float getFdAnalysisRms() const noexcept { return publishedFdAnalysisRms.load(std::memory_order_relaxed); }
    float getFdInverseRms() const noexcept { return publishedFdInverseRms.load(std::memory_order_relaxed); }
    float getFdExtractedRms() const noexcept { return publishedFdExtractedRms.load(std::memory_order_relaxed); }
    float getFdPsolaPeak() const noexcept { return publishedFdPsolaPeak.load(std::memory_order_relaxed); }
    float getFdReconstructedPeak() const noexcept { return publishedFdReconstructedPeak.load(std::memory_order_relaxed); }
    float getFdPsolaRms() const noexcept { return publishedFdPsolaRms.load(std::memory_order_relaxed); }
    float getFdReconstructedRms() const noexcept { return publishedFdReconstructedRms.load(std::memory_order_relaxed); }
    float getFdVoicedRms() const noexcept { return publishedFdVoicedRms.load(std::memory_order_relaxed); }
    float getFdHarmonyRms() const noexcept { return publishedFdHarmonyRms.load(std::memory_order_relaxed); }
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
        int midiChannel = 1;
        float velocity = 0.0f;
        float envelope = 0.0f;
        double nextSynthesisMark = 0.0;
        bool synthesisMarkInitialized = false;
        bool releasing = false;
        float grainBlend = 0.0f;
        std::array<float, lpcOrder> formantState {};

        void prepare();
        void start(int midiNote, int originalMidiNote, int channel, float noteVelocity) noexcept;
        void stop() noexcept;
    };

    static constexpr int fdFftOrder = 11;
    static constexpr int fdFftSize = 1 << fdFftOrder;
    juce::dsp::FFT fdPsolaFft { fdFftOrder };
    std::array<PitchVoice, voiceCount> voices {};
    std::array<float, inputRingSize> inputHistory {};
    std::array<float, inputRingSize> residualHistory {};
    std::array<std::array<float, inputRingSize>, lpcOrder> formantCoefficientHistory {};
    // Smooth reflection coefficients, not direct-form LPC polynomial
    // coefficients. Every interpolated set remains stable by construction.
    std::array<float, lpcOrder> formantReflectionCoefficients {};
    std::array<float, lpcOrder> targetFormantReflectionCoefficients {};
    std::array<float, lpcOrder> formantCoefficients {};
    int maximumBlockSize = 2048;
    double currentSampleRate = 44100.0;
    uint64_t totalSamples = 0;
    int pitchUpdateCounter = 0;
    int pitchMissCount = 0;
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
    std::array<float, 16> pitchBendSemitones {};
    std::array<float, 16> targetPitchBendSemitones {};
    int latencySamples = 0;
    int activeOctaveShift = 0;
    std::atomic<int> selectedAlgorithm { static_cast<int>(Algorithm::lpc) };
    std::atomic<float> formantPreservation { 1.0f };
    std::atomic<float> transientProtection { 1.0f };
    std::atomic<float> pitchSensitivity { 0.5f };
    std::atomic<float> pitchHoldMs { 120.0f };
    std::atomic<float> pitchRecoveryMs { 250.0f };
    std::atomic<float> publishedPitchHz { 0.0f };
    std::atomic<float> publishedPitchConfidence { 0.0f };
    std::atomic<float> publishedGrainCoverage { 0.0f };
    std::atomic<float> publishedFdInversePeak { 0.0f };
    std::atomic<float> publishedFdAnalysisRms { 0.0f };
    std::atomic<float> publishedFdInverseRms { 0.0f };
    std::atomic<float> publishedFdExtractedRms { 0.0f };
    std::atomic<float> publishedFdPsolaPeak { 0.0f };
    std::atomic<float> publishedFdReconstructedPeak { 0.0f };
    std::atomic<float> publishedFdPsolaRms { 0.0f };
    std::atomic<float> publishedFdReconstructedRms { 0.0f };
    std::atomic<float> publishedFdVoicedRms { 0.0f };
    std::atomic<float> publishedFdHarmonyRms { 0.0f };

    float estimatePitch(float sensitivity, float& confidence) const noexcept;
    void estimateFormantEnvelope() noexcept;
    void detectAndProcessPitchMark() noexcept;
    void processPitchMark(int64_t mark, double sourcePeriod, int64_t nextMark) noexcept;
    void scheduleGrain(PitchVoice& voice, int64_t sourceCentre,
                       double sourcePeriod, double targetPeriod,
                       int64_t synthesisCentre) noexcept;
    void handleMidiMessage(const juce::MidiMessage& message, int octaveShift) noexcept;
};
