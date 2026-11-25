#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

// Experimental LPC/source-filter vocoder. Analysis runs on a fixed-size
// rolling frame; synthesis remains sample-by-sample and allocation-free.
class LpcVocoderEngine
{
public:
    static constexpr int lpcOrder = 16;
    static constexpr int frameSize = 1024;
    static constexpr int analysisHop = 256;

    void prepare(double sampleRate);
    void reset();
    float processSample(float voice, float carrier) noexcept;
    void setDryWet(float mix) noexcept { dryWet.store(juce::jlimit(0.0f, 1.0f, mix)); }
    void setOutputGain(float gain) noexcept { outputGain.store(juce::jlimit(0.0f, 2.0f, gain)); }

private:
    void updateModel() noexcept;

    std::array<float, frameSize> analysisFrame {};
    std::array<float, frameSize> windowedFrame {};
    std::array<float, lpcOrder + 1> coefficients {};
    std::array<float, lpcOrder + 1> candidateCoefficients {};
    std::array<float, lpcOrder + 1> previousInput {};
    std::array<float, lpcOrder + 1> previousOutput {};
    std::uint32_t noiseState = 0x7f4a7c15u;
    int writePosition = 0;
    int samplesSinceUpdate = 0;
    int samplesSeen = 0;
    float previousVoice = 0.0f;
    float voiceRms = 0.0f;
    float carrierRms = 0.0f;
    float unvoicedAmount = 0.0f;
    float outputEnvelope = 0.0f;
    double sampleRate = 44100.0;
    std::atomic<float> dryWet { 1.0f };
    std::atomic<float> outputGain { 1.0f };
};
