#pragma once

#include <JuceHeader.h>
#include <array>

class VocoderEngine
{
public:
    static constexpr int bandCount = 32;

    void prepare(double sampleRate);
    void reset();

    // Processes one mono sample. The modulator is the voice/microphone;
    // externalCarrier is an optional second input channel.
    float processSample(float modulator, float externalCarrier, bool useExternalCarrier);

    void setAttackMs(float milliseconds) noexcept;
    void setReleaseMs(float milliseconds) noexcept;
    void setOutputGain(float gain) noexcept;

private:
    using Filter = juce::dsp::IIR::Filter<float>;

    std::array<Filter, bandCount> modulatorFilters;
    std::array<Filter, bandCount> carrierFilters;
    std::array<float, bandCount> envelopes {};
    std::array<float, 3> oscillatorPhases {};

    double currentSampleRate = 44100.0;
    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float outputGain = 1.0f;
    float attackMs = 8.0f;
    float releaseMs = 75.0f;

    float nextInternalCarrier() noexcept;
    void updateEnvelopeTimes() noexcept;
};
