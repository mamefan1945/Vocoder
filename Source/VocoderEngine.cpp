#include "VocoderEngine.h"

namespace
{
constexpr float lowestBandHz = 120.0f;
constexpr float highestBandHz = 8000.0f;
constexpr std::array<int, 5> supportedBandCounts { 24, 32, 40, 48, 56 };
constexpr int qSteps = 17;
constexpr float minimumQ = 2.0f;
constexpr float qStep = 0.5f;
}

void VocoderEngine::prepare(double sampleRate)
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);

    // Precompute all configurations before audio processing starts.
    for (size_t countIndex = 0; countIndex < supportedBandCounts.size(); ++countIndex)
    {
        const auto count = supportedBandCounts[countIndex];
        for (int qIndex = 0; qIndex < qSteps; ++qIndex)
        {
            auto& config = configurations[countIndex * qSteps + static_cast<size_t>(qIndex)];
            config.bandCount = count;
            const auto q = minimumQ + static_cast<float>(qIndex) * qStep;
            for (int band = 0; band < count; ++band)
            {
                const auto proportion = static_cast<float>(band) / static_cast<float>(count - 1);
                const auto centre = lowestBandHz * std::pow(highestBandHz / lowestBandHz, proportion);
                const auto safeCentre = juce::jmin(centre, static_cast<float>(currentSampleRate * 0.45));
                config.centres[static_cast<size_t>(band)] = safeCentre;
                config.coefficients[static_cast<size_t>(band)] =
                    juce::dsp::IIR::Coefficients<float>::makeBandPass(currentSampleRate, safeCentre, q);
            }
        }
    }
    activeConfiguration = -1;
    applyPendingConfiguration();

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
    const auto articulationAmount = articulationLift.load(std::memory_order_relaxed);
    const auto noiseAmount = unvoicedNoise.load(std::memory_order_relaxed);
    const auto sibilanceGain = sibilanceAmount.load(std::memory_order_relaxed);
    applyPendingConfiguration();
    const auto bandsPerOctave = static_cast<float>(activeBandCount - 1) / std::log2(highestBandHz / lowestBandHz);
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

    std::array<float, maxBandCount> carrierBands {};
    std::array<float, maxBandCount> noiseBands {};
    for (int band = 0; band < activeBandCount; ++band)
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
        // Keep the harmonic carrier intact. Unvoiced consonants need their own
        // noise excitation path; mixing noise into the tonal carrier can actually
        // weaken useful harmonics and leave fricatives dependent on carrier pitch.
        carrierBands[index] = tonalBand;
        noiseBands[index] = noiseBand;
    }

    // Convert each band level to a compressed modulation depth. This calibration
    // lifts quiet speech bands while the square-root curve preserves consonant
    // and vowel dynamics instead of letting a few loud bands dominate.
    float wetOutput = 0.0f;
    for (int band = 0; band < activeBandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        const auto centre = bandCentres[index];
        if (centre < low || centre > high)
            continue;

        const auto sourcePosition = juce::jlimit(0.0f, static_cast<float>(activeBandCount - 1),
                                                 static_cast<float>(band) - bandOffset);
        const auto lower = static_cast<int>(sourcePosition);
        const auto upper = juce::jmin(lower + 1, activeBandCount - 1);
        const auto fraction = sourcePosition - static_cast<float>(lower);
        const auto shiftedLevel = envelopes[static_cast<size_t>(lower)] * (1.0f - fraction)
                                + envelopes[static_cast<size_t>(upper)] * fraction;
        // Use a soft-knee expander rather than subtracting a hard threshold.
        // Quiet consonants often sit well below vowel energy; a hard per-band
        // gate erases them and makes otherwise clear speech unintelligible.
        // The user threshold is the centre of a 30 dB transition region.
        const auto threshold = gateThreshold.load(std::memory_order_relaxed);
        const auto gateFloor = threshold * 0.15f;
        const auto gateCeiling = juce::jmax(gateFloor + 0.00001f, threshold * 1.5f);
        const auto gatePosition = juce::jlimit(0.0f, 1.0f,
            (shiftedLevel - gateFloor) / (gateCeiling - gateFloor));
        const auto gateCurve = gatePosition * gatePosition * (3.0f - 2.0f * gatePosition);
        const auto gateGain = threshold <= 0.000001f ? 1.0f : gateCurve;
        const auto modulation = std::sqrt(juce::jlimit(0.0f, 1.0f, shiftedLevel * 24.0f))
                              * gateGain;
        // A slightly more sensitive envelope for the noise path retains the
        // low-level broadband detail that identifies S, F, SH and TH sounds.
        const auto noiseModulation = std::sqrt(juce::jlimit(0.0f, 1.0f, shiftedLevel * 40.0f))
                                   * gateGain;

        // Speech intelligibility depends heavily on upper-mid and high-frequency
        // consonant detail. Apply a gentle upward tilt after the soft gate so
        // quiet room noise is not amplified enough to open otherwise-closed bands.
        // The lift is 0 dB at 500 Hz and reaches about +3.5 dB at 8 kHz.
        const auto articulationPosition = juce::jlimit(0.0f, 1.0f,
            std::log2(juce::jmax(centre, 500.0f) / 500.0f) / 4.0f);
        const auto articulationGain = 1.0f + 0.5f * articulationPosition * articulationAmount;
        wetOutput += carrierBands[index] * modulation * articulationGain;

        // Independent, band-shaped noise excitation restores the turbulent
        // energy of F/S/SH/TH consonants even where the pitched carrier has no
        // harmonic near a synthesis-band centre. It is only active for unvoiced
        // speech, only in the upper bands, and only while a carrier note is held.
        const auto highFrequencyWeight = juce::jlimit(0.0f, 1.0f,
            (centre - 900.0f) / 2600.0f);
        const auto sibilance = 0.35f * sibilanceGain * unvoicedAmount
                             * highFrequencyWeight * carrierPresence * noiseAmount;
        wetOutput += noiseBands[index] * noiseModulation * sibilance;
    }

    // Keep headroom while avoiding an unnecessarily quiet wet signal.
    wetOutput *= 0.55f * outputGain.load(std::memory_order_relaxed);
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

void VocoderEngine::setArticulationLift(float amount) noexcept
{
    articulationLift.store(juce::jlimit(0.0f, 2.0f, amount), std::memory_order_relaxed);
}

void VocoderEngine::setUnvoicedNoise(float amount) noexcept
{
    unvoicedNoise.store(juce::jlimit(0.0f, 2.0f, amount), std::memory_order_relaxed);
}

void VocoderEngine::setSibilanceAmount(float amount) noexcept
{
    sibilanceAmount.store(juce::jlimit(0.0f, 2.0f, amount), std::memory_order_relaxed);
}


void VocoderEngine::setBandCount(int bands) noexcept
{
    const auto supported = bands <= 24 ? 24 : bands <= 32 ? 32 : bands <= 40 ? 40
                         : bands <= 48 ? 48 : 56;
    requestedBandCount.store(supported, std::memory_order_relaxed);
}

void VocoderEngine::setFilterQ(float q) noexcept
{
    const auto index = juce::jlimit(0, qSteps - 1, juce::roundToInt((q - minimumQ) / qStep));
    requestedQIndex.store(index, std::memory_order_relaxed);
}

void VocoderEngine::applyPendingConfiguration() noexcept
{
    const auto bands = requestedBandCount.load(std::memory_order_relaxed);
    const auto qIndex = requestedQIndex.load(std::memory_order_relaxed);
    size_t countIndex = 0;
    while (countIndex + 1 < supportedBandCounts.size() && supportedBandCounts[countIndex] != bands)
        ++countIndex;
    const auto configuration = static_cast<int>(countIndex * qSteps) + juce::jlimit(0, qSteps - 1, qIndex);
    if (configuration == activeConfiguration)
        return;
    const auto& config = configurations[static_cast<size_t>(configuration)];
    activeBandCount = config.bandCount;
    for (int band = 0; band < activeBandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        bandCentres[index] = config.centres[index];
        modulatorFilters[index].coefficients = config.coefficients[index];
        carrierFilters[index].coefficients = config.coefficients[index];
        sibilanceNoiseFilters[index].coefficients = config.coefficients[index];
    }
    for (int band = activeBandCount; band < maxBandCount; ++band)
    {
        const auto index = static_cast<size_t>(band);
        envelopes[index] = 0.0f;
        envelopePowers[index] = 0.0f;
    }
    activeConfiguration = configuration;
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
