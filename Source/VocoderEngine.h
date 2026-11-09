#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

class VocoderEngine
{
public:
    static constexpr int bandCount = 32;

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

private:
    using Filter = juce::dsp::IIR::Filter<float>;

    std::array<Filter, bandCount> modulatorFilters;
    std::array<Filter, bandCount> carrierFilters;
    std::array<Filter, bandCount> sibilanceNoiseFilters;
    std::uint32_t noiseState = 0x6d2b79f5u;
    float previousModulator = 0.0f;
    float zeroCrossingRate = 0.0f;
    float voicingSmoothingCoefficient = 0.0f;
    std::array<float, bandCount> envelopes {};
    std::array<float, bandCount> envelopePowers {};
    std::array<float, bandCount> bandCentres {};

    double currentSampleRate = 44100.0;
    std::atomic<float> attackCoefficient { 0.0f };
    std::atomic<float> releaseCoefficient { 0.0f };
    std::atomic<float> outputGain { 1.0f };
    std::atomic<float> dryWet { 1.0f };
    std::atomic<float> lowFrequency { 120.0f };
    std::atomic<float> highFrequency { 8000.0f };
    std::atomic<float> formantShift { 0.0f };
    std::atomic<float> gateThreshold { 0.003f };
    std::atomic<float> attackMs { 1.0f };
    std::atomic<float> releaseMs { 10.0f };

    void updateEnvelopeTimes() noexcept;
};
