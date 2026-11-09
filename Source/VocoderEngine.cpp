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

    voicingSmoothingCoefficient = std::exp(-1.0f / (0.010f * static_cast<float>(currentSampleRate)));
    carrierActivityAttackCoefficient = std::exp(-1.0f / (0.001f * static_cast<float>(currentSampleRate)));
    carrierActivityReleaseCoefficient = std::exp(-1.0f / (0.020f * static_cast<float>(currentSampleRate)));
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
    previousModulator = 0.0f;
    zeroCrossingRate = 0.0f;
    carrierActivity = 0.0f;
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

    // A short-term zero-crossing estimate distinguishes noisy, unvoiced speech
    // from predominantly periodic vowels. It is deliberately smoothed over 10 ms
    // so individual samples do not cause rapid switching.
    const auto crossedZero = (modulator >= 0.0f) != (previousModulator >= 0.0f);
    previousModulator = modulator;
    const auto crossing = crossedZero ? 1.0f : 0.0f;
    zeroCrossingRate = voicingSmoothingCoefficient * zeroCrossingRate
                     + (1.0f - voicingSmoothingCoefficient) * crossing;
    const auto unvoicedAmount = juce::jlimit(0.0f, 1.0f,
        (zeroCrossingRate - 0.035f) / 0.105f);

    // Analyse speech as band-limited RMS energy, not a smoothed absolute peak.
    // RMS is more stable for voiced sounds and less dependent on waveform shape.

    // Generate one shared white-noise sample and pass it through the same
    // synthesis bands. Noise is added only for likely unvoiced speech and is
    // weighted toward upper frequencies, where fricatives carry most of their
    // identifying energy. This avoids the broadband wash from the earlier mix.
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    const auto noiseSample = static_cast<float>(noiseState & 0x00ffffffu)
                           / 8388607.5f - 1.0f;

    // Unvoiced excitation must still have a carrier to excite. Track the selected
    // carrier independently of the speech envelopes so this also works with an
    // external carrier input. The short release avoids clicks when notes stop.
    const auto carrierLevel = std::abs(carrier);
    const auto carrierCoefficient = carrierLevel > carrierActivity
        ? carrierActivityAttackCoefficient : carrierActivityReleaseCoefficient;
    carrierActivity = carrierCoefficient * carrierActivity
                    + (1.0f - carrierCoefficient) * carrierLevel;
    const auto carrierPresence = juce::jlimit(0.0f, 1.0f,
        (carrierActivity - 0.0001f) / 0.0009f);

    std::array<float, bandCount> carrierBands {};
    for (int band = 0; band < bandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        const auto voiceBand = modulatorFilters[index].processSample(modulator);
        const auto power = voiceBand * voiceBand;
        const auto coefficient = power > envelopePowers[index] ? attack : release;
        envelopePowers[index] = coefficient * envelopePowers[index] + (1.0f - coefficient) * power;
        envelopes[index] = std::sqrt(juce::jmax(0.0f, envelopePowers[index]));

        const auto tonalBand = carrierFilters[index].processSample(carrier);
        const auto noiseBand = sibilanceNoiseFilters[index].processSample(noiseSample);
        const auto highFrequencyWeight = juce::jlimit(0.0f, 1.0f,
            (bandCentres[index] - 900.0f) / 2600.0f);
        const auto noiseAmount = 0.18f * unvoicedAmount * highFrequencyWeight * carrierPresence;
        carrierBands[index] = tonalBand * (1.0f - noiseAmount)
                            + noiseBand * noiseAmount;
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
    attackMs.store(juce::jlimit(0.1f, 30.0f, milliseconds), std::memory_order_relaxed);
    updateEnvelopeTimes();
}

void VocoderEngine::setReleaseMs(float milliseconds) noexcept
{
    releaseMs.store(juce::jlimit(1.0f, 250.0f, milliseconds), std::memory_order_relaxed);
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
