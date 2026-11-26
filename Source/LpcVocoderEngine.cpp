#include "LpcVocoderEngine.h"

#include <cmath>

void LpcVocoderEngine::prepare(double newSampleRate)
{
    sampleRate = juce::jmax(8000.0, newSampleRate);
    reset();
}

void LpcVocoderEngine::reset()
{
    analysisFrame.fill(0.0f);
    windowedFrame.fill(0.0f);
    coefficients.fill(0.0f);
    candidateCoefficients.fill(0.0f);
    previousInput.fill(0.0f);
    previousOutput.fill(0.0f);
    coefficients[0] = 1.0f;
    writePosition = 0;
    samplesSinceUpdate = 0;
    samplesSeen = 0;
    previousVoice = 0.0f;
    voiceRms = 0.0f;
    carrierRms = 0.0f;
    unvoicedAmount = 0.0f;
    outputEnvelope = 0.0f;
    noiseState = 0x7f4a7c15u;
}

void LpcVocoderEngine::updateModel() noexcept
{
    if (samplesSeen < frameSize)
        return;

    double mean = 0.0;
    for (int i = 0; i < frameSize; ++i)
    {
        const auto index = (writePosition + i) % frameSize;
        const auto window = 0.54f - 0.46f * std::cos(
            2.0f * juce::MathConstants<float>::pi * static_cast<float>(i)
            / static_cast<float>(frameSize - 1));
        windowedFrame[static_cast<size_t>(i)] = analysisFrame[static_cast<size_t>(index)] * window;
        mean += windowedFrame[static_cast<size_t>(i)];
    }
    const auto dc = static_cast<float>(mean / frameSize);
    for (auto& value : windowedFrame)
        value -= dc;

    std::array<double, lpcOrder + 1> autocorrelation {};
    for (int lag = 0; lag <= lpcOrder; ++lag)
    {
        double sum = 0.0;
        for (int i = lag; i < frameSize; ++i)
            sum += static_cast<double>(windowedFrame[static_cast<size_t>(i)])
                 * windowedFrame[static_cast<size_t>(i - lag)];
        autocorrelation[static_cast<size_t>(lag)] = sum;
    }

    const auto r0 = autocorrelation[0];
    if (r0 < 1.0e-9)
        return;

    candidateCoefficients.fill(0.0f);
    candidateCoefficients[0] = 1.0f;
    double predictionError = r0;
    for (int order = 1; order <= lpcOrder; ++order)
    {
        double residual = autocorrelation[static_cast<size_t>(order)];
        for (int j = 1; j < order; ++j)
            residual += static_cast<double>(candidateCoefficients[static_cast<size_t>(j)])
                      * autocorrelation[static_cast<size_t>(order - j)];

        if (!std::isfinite(residual) || !std::isfinite(predictionError)
            || predictionError <= r0 * 1.0e-10)
            break;
        const auto rawReflection = -residual / predictionError;
        if (!std::isfinite(rawReflection))
            break;
        const auto reflection = juce::jlimit(-0.92, 0.92, rawReflection);
        std::array<float, lpcOrder + 1> next = candidateCoefficients;
        next[static_cast<size_t>(order)] = static_cast<float>(reflection);
        for (int j = 1; j < order; ++j)
            next[static_cast<size_t>(j)] = candidateCoefficients[static_cast<size_t>(j)]
                + static_cast<float>(reflection) * candidateCoefficients[static_cast<size_t>(order - j)];
        candidateCoefficients = next;
        predictionError *= 1.0 - reflection * reflection;
        if (predictionError <= r0 * 1.0e-7)
            break;
    }

    for (auto& coefficient : coefficients)
        if (!std::isfinite(coefficient))
        {
            coefficients.fill(0.0f);
            coefficients[0] = 1.0f;
            previousOutput.fill(0.0f);
            break;
        }

    // Smooth model changes to avoid zippering as the analysis frame advances.
    for (int i = 1; i <= lpcOrder; ++i)
        coefficients[static_cast<size_t>(i)] += 0.28f
            * (candidateCoefficients[static_cast<size_t>(i)] - coefficients[static_cast<size_t>(i)]);
    coefficients[0] = 1.0f;

    double zeroCrossings = 0.0;
    for (int i = 1; i < frameSize; ++i)
        if ((windowedFrame[static_cast<size_t>(i)] >= 0.0f)
            != (windowedFrame[static_cast<size_t>(i - 1)] >= 0.0f))
            zeroCrossings += 1.0;
    const auto zcr = static_cast<float>(zeroCrossings / frameSize);
    const auto unvoicedTarget = juce::jlimit(0.0f, 1.0f, (zcr - 0.08f) / 0.20f);
    unvoicedAmount += 0.35f * (unvoicedTarget - unvoicedAmount);
    voiceRms = std::sqrt(static_cast<float>(r0 / frameSize));
    if (!std::isfinite(voiceRms))
        voiceRms = 0.0f;
}

float LpcVocoderEngine::processSample(float voice, float carrier) noexcept
{
    // Pre-emphasis helps LPC resolve the vocal-tract envelope without letting
    // low-frequency pitch energy dominate the autocorrelation.
    const auto emphasized = voice - 0.94f * previousVoice;
    previousVoice = voice;
    analysisFrame[static_cast<size_t>(writePosition)] = emphasized;
    writePosition = (writePosition + 1) % frameSize;
    samplesSeen = juce::jmin(frameSize, samplesSeen + 1);
    if (++samplesSinceUpdate >= analysisHop)
    {
        samplesSinceUpdate = 0;
        updateModel();
    }

    const auto carrierCoefficient = std::exp(-1.0f / (0.008f * static_cast<float>(sampleRate)));
    carrierRms = carrierCoefficient * carrierRms
        + (1.0f - carrierCoefficient) * carrier * carrier;

    const auto mix = dryWet.load(std::memory_order_relaxed);
    const auto gain = outputGain.load(std::memory_order_relaxed);

    // The LPC filter is a resonator: exciting it with its unvoiced-noise
    // component while the MIDI carrier is silent creates whistles/chirps.
    // Once the carrier envelope has decayed below -80 dBFS, close the wet
    // path and clear its delay line so no stored resonance can ring on.
    if (carrierRms < 1.0e-8f)
    {
        previousOutput.fill(0.0f);
        return voice * (1.0f - mix) * gain;
    }

    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    const auto noise = static_cast<float>(noiseState & 0x00ffffffu) / 8388607.5f - 1.0f;

    // Retain a pitched excitation for voiced speech and introduce a restrained
    // noise component when the analysis frame is likely unvoiced.
    const auto excitation = carrier * (1.0f - 0.78f * unvoicedAmount)
        + noise * (0.20f * unvoicedAmount);
    auto synthesized = excitation * juce::jlimit(0.025f, 0.30f, voiceRms * 0.85f);
    for (int i = lpcOrder; i > 0; --i)
    {
        synthesized -= coefficients[static_cast<size_t>(i)]
                     * previousOutput[static_cast<size_t>(i - 1)];
        previousOutput[static_cast<size_t>(i)] = previousOutput[static_cast<size_t>(i - 1)];
        previousInput[static_cast<size_t>(i)] = previousInput[static_cast<size_t>(i - 1)];
    }
    if (!std::isfinite(synthesized))
    {
        previousOutput.fill(0.0f);
        coefficients.fill(0.0f);
        coefficients[0] = 1.0f;
        synthesized = 0.0f;
    }
    previousOutput[0] = synthesized;
    previousInput[0] = emphasized;

    // A small unvoiced residual contribution retains stop/fricative detail that
    // a purely periodic carrier cannot reproduce. Soft limiting contains
    // occasional resonant peaks without hard clipping.
    const auto wet = std::tanh((synthesized + voice * 0.10f * unvoicedAmount) * 1.6f);
    const auto output = (wet * mix + voice * (1.0f - mix)) * gain;
    return std::isfinite(output) ? output : 0.0f;
}
