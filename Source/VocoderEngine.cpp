#include "VocoderEngine.h"

namespace
{
constexpr float lowestBandHz = 120.0f;
constexpr float highestBandHz = 8000.0f;
constexpr float filterQ = 1.35f;
constexpr std::array<float, 3> chordFrequencies { 110.0f, 164.81f, 220.0f };
constexpr float twoPi = juce::MathConstants<float>::twoPi;
}

void VocoderEngine::prepare(double sampleRate)
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);

    for (int band = 0; band < bandCount; ++band)
    {
        const auto proportion = static_cast<float>(band) / static_cast<float>(bandCount - 1);
        const auto centre = lowestBandHz * std::pow(highestBandHz / lowestBandHz, proportion);
        const auto safeCentre = juce::jmin(centre, static_cast<float>(currentSampleRate * 0.45));

        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(
            currentSampleRate, safeCentre, filterQ);

        *modulatorFilters[static_cast<size_t>(band)].coefficients = *coefficients;
        *carrierFilters[static_cast<size_t>(band)].coefficients = *coefficients;
    }

    updateEnvelopeTimes();
    reset();
}

void VocoderEngine::reset()
{
    for (auto& filter : modulatorFilters)
        filter.reset();

    for (auto& filter : carrierFilters)
        filter.reset();

    envelopes.fill(0.0f);
    oscillatorPhases.fill(0.0f);
}

float VocoderEngine::processSample(float modulator, float externalCarrier, bool useExternalCarrier)
{
    const auto carrier = useExternalCarrier ? externalCarrier : nextInternalCarrier();
    float output = 0.0f;

    for (int band = 0; band < bandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        const auto voiceBand = std::abs(modulatorFilters[index].processSample(modulator));
        const auto carrierBand = carrierFilters[index].processSample(carrier);

        const auto coefficient = voiceBand > envelopes[index]
            ? attackCoefficient
            : releaseCoefficient;

        envelopes[index] = coefficient * envelopes[index]
                         + (1.0f - coefficient) * voiceBand;

        // The envelope scale compensates for the low level of narrow band-pass
        // outputs. The final band sum is normalized to avoid excessive gain.
        output += carrierBand * envelopes[index] * 18.0f;
    }

    return output * (outputGain / static_cast<float>(bandCount)) * 32.0f;
}

void VocoderEngine::setAttackMs(float milliseconds) noexcept
{
    juce::ignoreUnused(milliseconds);
    updateEnvelopeTimes();
}

void VocoderEngine::setReleaseMs(float milliseconds) noexcept
{
    juce::ignoreUnused(milliseconds);
    updateEnvelopeTimes();
}

void VocoderEngine::setOutputGain(float gain) noexcept
{
    outputGain = juce::jlimit(0.0f, 4.0f, gain);
}

void VocoderEngine::updateEnvelopeTimes() noexcept
{
    const auto attackSeconds = 0.008f;
    const auto releaseSeconds = 0.075f;
    attackCoefficient = std::exp(-1.0f / (attackSeconds * static_cast<float>(currentSampleRate)));
    releaseCoefficient = std::exp(-1.0f / (releaseSeconds * static_cast<float>(currentSampleRate)));
}

float VocoderEngine::nextInternalCarrier() noexcept
{
    float sample = 0.0f;

    for (size_t oscillator = 0; oscillator < oscillatorPhases.size(); ++oscillator)
    {
        auto& phase = oscillatorPhases[oscillator];
        sample += std::sin(phase);
        phase += twoPi * chordFrequencies[oscillator] / static_cast<float>(currentSampleRate);

        if (phase >= twoPi)
            phase -= twoPi;
    }

    return sample * 0.18f;
}
