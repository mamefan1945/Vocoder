#include "VocoderEngine.h"

namespace
{
constexpr float lowestBandHz = 120.0f;
constexpr float highestBandHz = 8000.0f;
// Adjacent logarithmic bands are spaced by roughly 16%; Q=4 gives
// useful overlap without smearing the spectral envelope across many bands.
constexpr float filterQ = 4.0f;
}

void VocoderEngine::prepare(double sampleRate)
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);

    for (int band = 0; band < bandCount; ++band)
    {
        const auto proportion = static_cast<float>(band) / static_cast<float>(bandCount - 1);
        const auto centre = lowestBandHz * std::pow(highestBandHz / lowestBandHz, proportion);
        const auto safeCentre = juce::jmin(centre, static_cast<float>(currentSampleRate * 0.45));
        bandCentres[static_cast<size_t>(band)] = safeCentre;

        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(
            currentSampleRate, safeCentre, filterQ);

        modulatorFilters[static_cast<size_t>(band)].coefficients = coefficients;
        carrierFilters[static_cast<size_t>(band)].coefficients = coefficients;
        sibilanceNoiseFilters[static_cast<size_t>(band)].coefficients = coefficients;
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
    for (auto& filter : sibilanceNoiseFilters)
        filter.reset();
    noiseState = 0x6d2b79f5u;
    envelopes.fill(0.0f);
    envelopePowers.fill(0.0f);
}

float VocoderEngine::processSample(float modulator, float externalCarrier, bool useExternalCarrier,
                                   float internalCarrier)
{
    const auto carrier = useExternalCarrier ? externalCarrier : internalCarrier;
    const auto low = lowFrequency.load(std::memory_order_relaxed);
    const auto high = highFrequency.load(std::memory_order_relaxed);
    const auto attack = attackCoefficient.load(std::memory_order_relaxed);
    const auto release = releaseCoefficient.load(std::memory_order_relaxed);
    const auto shift = formantShift.load(std::memory_order_relaxed);
    const auto bandsPerOctave = static_cast<float>(bandCount - 1) / std::log2(highestBandHz / lowestBandHz);
    const auto bandOffset = shift * (bandsPerOctave / 12.0f);

    // Analyse speech as band-limited RMS energy, not a smoothed absolute peak.
    // RMS is more stable for voiced sounds and less dependent on waveform shape.

    std::array<float, bandCount> carrierBands {};
    for (int band = 0; band < bandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        const auto voiceBand = modulatorFilters[index].processSample(modulator);
        const auto power = voiceBand * voiceBand;
        const auto coefficient = power > envelopePowers[index] ? attack : release;
        envelopePowers[index] = coefficient * envelopePowers[index] + (1.0f - coefficient) * power;
        envelopes[index] = std::sqrt(juce::jmax(0.0f, envelopePowers[index]));

        // Keep the carrier purely tonal for now. Broadband noise excitation
        // obscures the formant pattern when its level is not calibrated to the
        // microphone and carrier, so consonant noise is intentionally disabled.
        carrierBands[index] = carrierFilters[index].processSample(carrier);
    }

    // Convert each band level to a compressed modulation depth. This calibration
    // lifts quiet speech bands while the square-root curve preserves consonant
    // and vowel dynamics instead of letting a few loud bands dominate.
    float wetOutput = 0.0f;
    for (int band = 0; band < bandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        const auto centre = bandCentres[index];
        if (centre < low || centre > high)
            continue;

        const auto sourcePosition = juce::jlimit(0.0f, static_cast<float>(bandCount - 1),
                                                 static_cast<float>(band) - bandOffset);
        const auto lower = static_cast<int>(sourcePosition);
        const auto upper = juce::jmin(lower + 1, bandCount - 1);
        const auto fraction = sourcePosition - static_cast<float>(lower);
        const auto shiftedLevel = envelopes[static_cast<size_t>(lower)] * (1.0f - fraction)
                                + envelopes[static_cast<size_t>(upper)] * fraction;
        // A real gate prevents microphone self-noise and room noise from
        // continuously opening the carrier. The threshold is expressed as
        // per-band RMS amplitude and remains adjustable from the UI.
        const auto aboveGate = juce::jmax(0.0f,
            shiftedLevel - gateThreshold.load(std::memory_order_relaxed));
        const auto modulation = std::sqrt(juce::jlimit(0.0f, 1.0f, aboveGate * 24.0f));
        wetOutput += carrierBands[index] * modulation;
    }

    // The compressed envelopes are much larger than the previous raw levels;
    // use a conservative summing scale to retain headroom across 32 bands.
    wetOutput *= 0.45f * outputGain.load(std::memory_order_relaxed);
    const auto mix = dryWet.load(std::memory_order_relaxed);
    return wetOutput * mix + modulator * (1.0f - mix);
}

void VocoderEngine::setAttackMs(float milliseconds) noexcept
{
    attackMs.store(juce::jlimit(0.5f, 100.0f, milliseconds), std::memory_order_relaxed);
    updateEnvelopeTimes();
}

void VocoderEngine::setReleaseMs(float milliseconds) noexcept
{
    releaseMs.store(juce::jlimit(5.0f, 1000.0f, milliseconds), std::memory_order_relaxed);
    updateEnvelopeTimes();
}

void VocoderEngine::setOutputGain(float gain) noexcept
{
    outputGain.store(juce::jlimit(0.0f, 4.0f, gain), std::memory_order_relaxed);
}

void VocoderEngine::setDryWet(float mix) noexcept
{
    dryWet.store(juce::jlimit(0.0f, 1.0f, mix), std::memory_order_relaxed);
}

void VocoderEngine::setFrequencyRange(float lowHz, float highHz) noexcept
{
    const auto low = juce::jlimit(lowestBandHz, highestBandHz, lowHz);
    const auto high = juce::jlimit(low, highestBandHz, highHz);
    lowFrequency.store(low, std::memory_order_relaxed);
    highFrequency.store(high, std::memory_order_relaxed);
}

void VocoderEngine::setFormantShift(float semitones) noexcept
{
    formantShift.store(juce::jlimit(-12.0f, 12.0f, semitones), std::memory_order_relaxed);
}

void VocoderEngine::setGateThreshold(float level) noexcept
{
    gateThreshold.store(juce::jlimit(0.0f, 0.02f, level), std::memory_order_relaxed);
}

void VocoderEngine::updateEnvelopeTimes() noexcept
{
    const auto attackSeconds = attackMs.load(std::memory_order_relaxed) * 0.001f;
    const auto releaseSeconds = releaseMs.load(std::memory_order_relaxed) * 0.001f;
    attackCoefficient.store(std::exp(-1.0f / (attackSeconds * static_cast<float>(currentSampleRate))),
                            std::memory_order_relaxed);
    releaseCoefficient.store(std::exp(-1.0f / (releaseSeconds * static_cast<float>(currentSampleRate))),
                             std::memory_order_relaxed);
}
