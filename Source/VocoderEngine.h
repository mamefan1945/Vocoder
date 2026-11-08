#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

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

private:
    using Filter = juce::dsp::IIR::Filter<float>;

    std::array<Filter, bandCount> modulatorFilters;
    std::array<Filter, bandCount> carrierFilters;
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
    std::atomic<float> attackMs { 8.0f };
    std::atomic<float> releaseMs { 75.0f };

    void updateEnvelopeTimes() noexcept;
};
