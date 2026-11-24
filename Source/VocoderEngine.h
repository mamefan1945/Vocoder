#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

class VocoderEngine
{
public:
    static constexpr int maxBandCount = 56;
    static constexpr int configurationCount = 85;

    void prepare(double sampleRate);
    void reset();

    float processSample(float modulator, float externalCarrier, bool useExternalCarrier,
                        float internalCarrier);

    void setAttackMs(float milliseconds) noexcept;
    void setReleaseMs(float milliseconds) noexcept;
    void setOutputGain(float gain) noexcept;
    void setDryWet(float mix) noexcept;
    void setFrequencyRange(float lowHz, float highHz) noexcept;
    void setFormantShift(float semitones) noexcept;
    void setGateThreshold(float level) noexcept;
    void setArticulationLift(float amount) noexcept;
    void setUnvoicedNoise(float amount) noexcept;
    void setSibilanceAmount(float amount) noexcept;
    void setPlosiveBurst(float amount) noexcept;
    void setPreEmphasisAmount(float amount) noexcept;
    void setBandCount(int bands) noexcept;
    void setFilterQ(float q) noexcept;

private:
    using Filter = juce::dsp::IIR::Filter<float>;

    struct FilterConfiguration
    {
        int bandCount = 40;
        std::array<juce::dsp::IIR::Coefficients<float>::Ptr, maxBandCount> coefficients {};
        std::array<float, maxBandCount> centres {};
    };
    std::array<FilterConfiguration, configurationCount> configurations {};
    std::array<Filter, maxBandCount> modulatorFilters;
    std::array<Filter, maxBandCount> carrierFilters;
    std::array<Filter, maxBandCount> sibilanceNoiseFilters;
    int activeConfiguration = -1;
    int activeBandCount = 40;
    std::atomic<int> requestedBandCount { 40 };
    std::atomic<int> requestedQIndex { 6 };
    std::uint32_t noiseState = 0x6d2b79f5u;
    float previousModulator = 0.0f;
    float previousPreEmphasisInput = 0.0f;
    float zeroCrossingRate = 0.0f;
    float voicingSmoothingCoefficient = 0.0f;
    float unvoicedAmount = 0.0f;
    float unvoicedAttackCoefficient = 0.0f;
    float unvoicedReleaseCoefficient = 0.0f;
    float transientAttackCoefficient = 0.0f;
    float transientReleaseCoefficient = 0.0f;
    float plosiveAttackCoefficient = 0.0f;
    float plosiveReleaseCoefficient = 0.0f;
    float plosiveSlowAttackCoefficient = 0.0f;
    float plosiveSlowReleaseCoefficient = 0.0f;
    float plosiveEnvelope = 0.0f;
    float carrierActivity = 0.0f;
    float carrierActivityAttackCoefficient = 0.0f;
    float carrierActivityReleaseCoefficient = 0.0f;
    std::array<float, maxBandCount> envelopes {};
    std::array<float, maxBandCount> envelopePowers {};
    std::array<float, maxBandCount> transientEnvelopes {};
    std::array<float, maxBandCount> plosiveSlowEnvelopes {};
    std::array<float, maxBandCount> bandCentres {};
    std::array<std::atomic<float>, maxBandCount> bandAttackScales {};
    std::array<std::atomic<float>, maxBandCount> bandAttackCoefficients {};

    double currentSampleRate = 44100.0;
    std::atomic<float> attackCoefficient { 0.0f };
    std::atomic<float> releaseCoefficient { 0.0f };
    std::atomic<float> outputGain { 1.0f };
    std::atomic<float> dryWet { 1.0f };
    std::atomic<float> lowFrequency { 120.0f };
    std::atomic<float> highFrequency { 8000.0f };
    std::atomic<float> formantShift { 0.0f };
    std::atomic<float> gateThreshold { 0.003f };
    std::atomic<float> articulationLift { 1.0f };
    std::atomic<float> unvoicedNoise { 1.0f };
    std::atomic<float> sibilanceAmount { 1.0f };
    std::atomic<float> plosiveBurstAmount { 1.0f };
    std::atomic<float> preEmphasisAmount { 0.15f };
    std::atomic<float> attackMs { 1.0f };
    std::atomic<float> releaseMs { 10.0f };

    void updateEnvelopeTimes() noexcept;
    void applyPendingConfiguration() noexcept;
};
