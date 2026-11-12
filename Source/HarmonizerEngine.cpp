#include "HarmonizerEngine.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float minimumPitchHz = 50.0f;
    constexpr float maximumPitchHz = 700.0f;
    constexpr float minimumCorrelation = 0.58f;
    constexpr float envelopeFloor = 0.001f;
}

void HarmonizerEngine::PitchVoice::prepare()
{
    overlap.assign(outputRingSize, 0.0f);
    weights.assign(outputRingSize, 0.0f);
}

void HarmonizerEngine::PitchVoice::start(int midiNote, float noteVelocity) noexcept
{
    note = midiNote;
    velocity = noteVelocity;
    envelope = 0.0f;
    nextSynthesisMark = 0.0;
    synthesisMarkInitialized = false;
    releasing = false;
}

void HarmonizerEngine::PitchVoice::stop() noexcept
{
    releasing = true;
    synthesisMarkInitialized = false;
}

void HarmonizerEngine::prepare(double sampleRate, int blockSize) noexcept
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);
    maximumBlockSize = juce::jmax(64, blockSize);
    latencySamples = static_cast<int>(std::ceil(currentSampleRate * 0.040));
    for (auto& voice : voices)
        voice.prepare();
    reset();
}

void HarmonizerEngine::reset() noexcept
{
    for (auto& voice : voices)
    {
        std::fill(voice.overlap.begin(), voice.overlap.end(), 0.0f);
        std::fill(voice.weights.begin(), voice.weights.end(), 0.0f);
        voice.note = -1;
        voice.velocity = 0.0f;
        voice.envelope = 0.0f;
        voice.nextSynthesisMark = 0.0;
        voice.synthesisMarkInitialized = false;
        voice.releasing = false;
    }

    inputHistory.fill(0.0f);
    totalSamples = 0;
    pitchUpdateCounter = 0;
    previousPitchMark = -1;
    previousPitchPeriod = 0.0;
    nextExpectedPitchMark = 0.0;
    detectedPitchHz = 0.0f;
    voicedMix = 0.0f;
    havePitchMark = false;
}

float HarmonizerEngine::estimatePitch() const noexcept
{
    // Normalised autocorrelation over a 32 ms decimated window.
    if (totalSamples < static_cast<uint64_t>(pitchWindowSize * 2))
        return 0.0f;

    std::array<float, pitchWindowSize> samples {};
    const auto newest = static_cast<int64_t>(totalSamples) - 1;
    double mean = 0.0;
    for (int i = 0; i < pitchWindowSize; ++i)
    {
        const auto absolute = newest - static_cast<int64_t>(pitchWindowSize - 1 - i) * 2;
        const auto index = static_cast<size_t>(absolute) & inputRingMask;
        samples[static_cast<size_t>(i)] = inputHistory[index];
        mean += samples[static_cast<size_t>(i)];
    }
    mean /= static_cast<double>(pitchWindowSize);

    double energy = 0.0;
    for (auto& sample : samples)
    {
        sample -= static_cast<float>(mean);
        energy += static_cast<double>(sample) * sample;
    }
    if (energy / pitchWindowSize < 1.0e-7)
        return 0.0f;

    const auto decimatedRate = currentSampleRate * 0.5;
    const auto minLag = juce::jmax(2, static_cast<int>(decimatedRate / maximumPitchHz));
    const auto maxLag = juce::jmin(pitchWindowSize / 2,
                                  static_cast<int>(decimatedRate / minimumPitchHz));
    std::array<float, pitchWindowSize / 2 + 1> correlations {};
    float bestCorrelation = 0.0f;
    int bestLag = 0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double cross = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (int i = lag; i < pitchWindowSize; ++i)
        {
            const auto a = samples[static_cast<size_t>(i)];
            const auto b = samples[static_cast<size_t>(i - lag)];
            cross += static_cast<double>(a) * b;
            energyA += static_cast<double>(a) * a;
            energyB += static_cast<double>(b) * b;
        }

        const auto denominator = std::sqrt(energyA * energyB);
        const auto correlation = denominator > 1.0e-12
            ? static_cast<float>(cross / denominator) : 0.0f;
        correlations[static_cast<size_t>(lag)] = correlation;
        if (correlation > bestCorrelation)
        {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    if (bestLag == 0 || bestCorrelation < minimumCorrelation)
        return 0.0f;

    const auto left = correlations[static_cast<size_t>(juce::jmax(minLag, bestLag - 1))];
    const auto centre = correlations[static_cast<size_t>(bestLag)];
    const auto right = correlations[static_cast<size_t>(juce::jmin(maxLag, bestLag + 1))];
    const auto denominator = left - 2.0f * centre + right;
    const auto offset = std::abs(denominator) > 1.0e-8f
        ? 0.5f * (left - right) / denominator : 0.0f;
    const auto refinedLag = static_cast<float>(bestLag)
        + juce::jlimit(-0.5f, 0.5f, offset);
    return static_cast<float>(decimatedRate / refinedLag);
}

void HarmonizerEngine::scheduleGrain(PitchVoice& voice, int64_t sourceCentre,
                                     double sourcePeriod, int64_t synthesisCentre,
                                     double targetPeriod) noexcept
{
    const auto halfWindow = juce::jlimit(8, 1024,
        static_cast<int>(std::ceil(juce::jmax(sourcePeriod, targetPeriod))));
    const auto inverseHalf = 1.0f / static_cast<float>(halfWindow);

    for (int offset = -halfWindow; offset <= halfWindow; ++offset)
    {
        const auto sourceAbsolute = sourceCentre + offset;
        if (sourceAbsolute < 0)
            continue;

        const auto phase = static_cast<float>(offset) * inverseHalf;
        const auto window = 0.5f + 0.5f * std::cos(pi * phase);
        const auto sourceIndex = static_cast<size_t>(sourceAbsolute) & inputRingMask;
        const auto outputAbsolute = synthesisCentre + offset;
        if (outputAbsolute < 0)
            continue;
        const auto outputIndex = static_cast<size_t>(outputAbsolute) & outputRingMask;

        voice.overlap[outputIndex] += inputHistory[sourceIndex] * window;
        voice.weights[outputIndex] += window;
    }
}

void HarmonizerEngine::processPitchMark(int64_t mark, double sourcePeriod) noexcept
{
    const auto delayedMark = mark + latencySamples;
    for (auto& voice : voices)
    {
        if (voice.note < 0 || voice.releasing)
            continue;

        const auto targetHz = 440.0 * std::pow(2.0, (voice.note - 69) / 12.0);
        const auto targetPeriod = currentSampleRate / targetHz;
        if (!voice.synthesisMarkInitialized)
        {
            voice.nextSynthesisMark = static_cast<double>(delayedMark);
            voice.synthesisMarkInitialized = true;
        }

        const auto interval = juce::jmax(1.0, static_cast<double>(mark - previousPitchMark));
        while (voice.nextSynthesisMark <= static_cast<double>(delayedMark))
        {
            const auto sourcePosition = voice.nextSynthesisMark - latencySamples;
            const auto usePrevious = previousPitchMark >= 0
                && sourcePosition < static_cast<double>(previousPitchMark) + interval * 0.5;
            const auto selectedCentre = usePrevious ? previousPitchMark : mark;
            const auto selectedPeriod = usePrevious && previousPitchPeriod > 0.0
                ? previousPitchPeriod : sourcePeriod;

            scheduleGrain(voice, selectedCentre, selectedPeriod,
                          static_cast<int64_t>(std::llround(voice.nextSynthesisMark)),
                          targetPeriod);
            voice.nextSynthesisMark += targetPeriod;
        }
    }

    previousPitchMark = mark;
    previousPitchPeriod = sourcePeriod;
}

void HarmonizerEngine::detectAndProcessPitchMark() noexcept
{
    if (detectedPitchHz < minimumPitchHz || detectedPitchHz > maximumPitchHz)
    {
        havePitchMark = false;
        previousPitchMark = -1;
        previousPitchPeriod = 0.0;
        for (auto& voice : voices)
            voice.synthesisMarkInitialized = false;
        return;
    }

    const auto period = currentSampleRate / detectedPitchHz;
    const auto searchRadius = juce::jmax(2, static_cast<int>(std::ceil(period * 0.22)));
    if (!havePitchMark)
    {
        nextExpectedPitchMark = static_cast<double>(totalSamples - 1)
                              - static_cast<double>(searchRadius);
        havePitchMark = true;
    }

    if (static_cast<double>(totalSamples - 1)
        < nextExpectedPitchMark + searchRadius)
        return;

    const auto expected = static_cast<int64_t>(std::llround(nextExpectedPitchMark));
    const auto first = juce::jmax<int64_t>(0, expected - searchRadius);
    const auto last = juce::jmin<int64_t>(static_cast<int64_t>(totalSamples) - 1,
                                          expected + searchRadius);
    auto bestMark = expected;
    float bestMagnitude = -1.0f;

    for (auto candidate = first; candidate <= last; ++candidate)
    {
        const auto index = static_cast<size_t>(candidate) & inputRingMask;
        const auto magnitude = std::abs(inputHistory[index]);
        if (magnitude > bestMagnitude)
        {
            bestMagnitude = magnitude;
            bestMark = candidate;
        }
    }

    if (previousPitchMark >= 0 && bestMark <= previousPitchMark)
        bestMark = previousPitchMark + juce::jmax(1, static_cast<int>(std::llround(period)));

    processPitchMark(bestMark, period);
    nextExpectedPitchMark = static_cast<double>(bestMark) + period;
}

void HarmonizerEngine::handleMidi(const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const auto note = message.getNoteNumber();
            bool found = false;
            for (auto& voice : voices)
            {
                if (voice.note == note)
                {
                    voice.start(note, message.getFloatVelocity());
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                auto* slot = &voices[0];
                for (auto& voice : voices)
                {
                    if (voice.note < 0)
                    {
                        slot = &voice;
                        break;
                    }
                    if (voice.releasing && voice.envelope < slot->envelope)
                        slot = &voice;
                }
                slot->start(note, message.getFloatVelocity());
            }
        }
        else if (message.isNoteOff())
        {
            for (auto& voice : voices)
                if (voice.note == message.getNoteNumber())
                    voice.stop();
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& voice : voices)
                voice.stop();
        }
    }
}

void HarmonizerEngine::processBlock(const float* input, float* output, int numSamples,
                                    const juce::MidiBuffer& midi, float dryWet,
                                    float outputGain) noexcept
{
    if (numSamples <= 0 || numSamples > maximumBlockSize || output == nullptr)
    {
        if (output != nullptr && numSamples > 0)
            juce::FloatVectorOperations::clear(output, numSamples);
        return;
    }

    handleMidi(midi);
    const auto mix = juce::jlimit(0.0f, 1.0f, dryWet);
    const auto sampleRate = static_cast<float>(currentSampleRate);
    const auto attackCoefficient = 1.0f - std::exp(-1.0f / (0.008f * sampleRate));
    const auto releaseCoefficient = std::exp(-1.0f / (0.045f * sampleRate));
    const auto voicedAttack = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
    const auto voicedRelease = 1.0f - std::exp(-1.0f / (0.008f * sampleRate));
    const auto updateInterval = juce::jmax(1, static_cast<int>(currentSampleRate * 0.010));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto absolute = static_cast<int64_t>(totalSamples);
        const auto source = input != nullptr ? input[i] : 0.0f;
        inputHistory[static_cast<size_t>(totalSamples) & inputRingMask] = source;
        ++totalSamples;

        if (++pitchUpdateCounter >= updateInterval)
        {
            pitchUpdateCounter = 0;
            const auto estimate = estimatePitch();
            if (estimate >= minimumPitchHz && estimate <= maximumPitchHz)
                detectedPitchHz = detectedPitchHz > 0.0f
                    ? detectedPitchHz + 0.30f * (estimate - detectedPitchHz) : estimate;
            else
                detectedPitchHz *= 0.60f;
            if (detectedPitchHz < minimumPitchHz)
                detectedPitchHz = 0.0f;
        }

        const auto voiced = detectedPitchHz >= minimumPitchHz
                         && detectedPitchHz <= maximumPitchHz;
        voicedMix += ((voiced ? 1.0f : 0.0f) - voicedMix)
                   * (voiced ? voicedAttack : voicedRelease);

        detectAndProcessPitchMark();

        const auto delayedAbsolute = absolute - latencySamples;
        const auto delayedInput = delayedAbsolute >= 0
            ? inputHistory[static_cast<size_t>(delayedAbsolute) & inputRingMask] : 0.0f;
        float harmony = 0.0f;
        int activeCount = 0;

        for (auto& voice : voices)
        {
            const auto ringIndex = static_cast<size_t>(absolute) & outputRingMask;
            const auto weight = voice.weights[ringIndex];
            const auto psola = weight > 1.0e-5f
                ? voice.overlap[ringIndex] / weight : 0.0f;
            voice.overlap[ringIndex] = 0.0f;
            voice.weights[ringIndex] = 0.0f;

            if (voice.note < 0)
                continue;

            if (voice.releasing)
            {
                voice.envelope *= releaseCoefficient;
                if (voice.envelope < envelopeFloor)
                {
                    voice.note = -1;
                    voice.velocity = 0.0f;
                    voice.envelope = 0.0f;
                    voice.releasing = false;
                    voice.synthesisMarkInitialized = false;
                    continue;
                }
            }
            else
                voice.envelope += (1.0f - voice.envelope) * attackCoefficient;

            const auto voiceSignal = psola * voicedMix + delayedInput * (1.0f - voicedMix);
            harmony += voiceSignal * voice.velocity * voice.envelope;
            ++activeCount;
        }

        const auto normalization = activeCount > 0
            ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 0.0f;
        output[i] = (delayedInput * (1.0f - mix)
                   + harmony * normalization * mix) * outputGain;
    }
}
