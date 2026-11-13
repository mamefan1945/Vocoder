#include "HarmonizerEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

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

void HarmonizerEngine::PitchVoice::start(int midiNote, int originalMidiNote, float noteVelocity) noexcept
{
    note = midiNote;
    sourceNote = originalMidiNote;
    velocity = noteVelocity;
    envelope = 0.0f;
    nextSynthesisMark = 0.0;
    synthesisMarkInitialized = false;
    releasing = false;
    formantState.fill(0.0f);
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
        voice.sourceNote = -1;
        voice.velocity = 0.0f;
        voice.envelope = 0.0f;
        voice.nextSynthesisMark = 0.0;
        voice.synthesisMarkInitialized = false;
        voice.releasing = false;
        voice.formantState.fill(0.0f);
    }

    inputHistory.fill(0.0f);
    residualHistory.fill(0.0f);
    for (auto& coefficients : formantCoefficientHistory)
        coefficients.fill(0.0f);
    formantCoefficients.fill(0.0f);
    targetFormantCoefficients.fill(0.0f);
    totalSamples = 0;
    pitchUpdateCounter = 0;
    formantUpdateCounter = 0;
    fastEnergy = 0.0f;
    slowEnergy = 0.0f;
    slopeEnergy = 0.0f;
    previousInputSample = 0.0f;
    transientMix = 0.0f;
    transientHoldSamples = 0;
    transientCooldownSamples = 0;
    previousPitchMark = -1;
    previousPitchPeriod = 0.0;
    nextExpectedPitchMark = 0.0;
    detectedPitchHz = 0.0f;
    voicedMix = 0.0f;
    havePitchMark = false;
    pitchMarkPolarity = 1.0f;
    pitchBendSemitones = 0.0f;
    targetPitchBendSemitones = 0.0f;
}

float HarmonizerEngine::estimatePitch() const noexcept
{
    // YIN-style difference analysis over a decimated ~43 ms window at 48 kHz.
    // The cumulative-mean normalized difference is less likely than a raw
    // autocorrelation maximum to lock onto a strong octave/subharmonic.
    if (totalSamples < static_cast<uint64_t>(pitchWindowSize * 2))
        return 0.0f;

    std::array<float, pitchWindowSize> samples {};
    const auto newest = static_cast<int64_t>(totalSamples) - 1;
    double mean = 0.0;
    for (int i = 0; i < pitchWindowSize; ++i)
    {
        const auto absolute = newest - static_cast<int64_t>(pitchWindowSize - 1 - i) * 2;
        samples[static_cast<size_t>(i)] =
            inputHistory[static_cast<size_t>(absolute) & inputRingMask];
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
    std::array<float, pitchWindowSize / 2 + 1> normalizedDifference {};

    double cumulativeDifference = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < pitchWindowSize - lag; ++i)
        {
            const auto delta = static_cast<double>(samples[static_cast<size_t>(i)])
                             - samples[static_cast<size_t>(i + lag)];
            sum += delta * delta;
        }
        cumulativeDifference += sum;
        normalizedDifference[static_cast<size_t>(lag)] =
            cumulativeDifference > 1.0e-12
                ? static_cast<float>(sum * lag / cumulativeDifference) : 1.0f;
    }

    // Select the first convincing local trough. Choosing the global trough
    // alone often favours a multiple of the true period for harmonic voices.
    constexpr float yinThreshold = 0.18f;
    int bestLag = 0;
    for (int lag = minLag; lag < maxLag; ++lag)
    {
        const auto value = normalizedDifference[static_cast<size_t>(lag)];
        if (value < yinThreshold
            && value <= normalizedDifference[static_cast<size_t>(lag + 1)])
        {
            bestLag = lag;
            break;
        }
    }

    // Some breathy/noisy voices never cross the strict threshold. Permit the
    // best trough only when it still has reasonable periodicity confidence.
    if (bestLag == 0)
    {
        float bestValue = 1.0f;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const auto value = normalizedDifference[static_cast<size_t>(lag)];
            if (value < bestValue)
            {
                bestValue = value;
                bestLag = lag;
            }
        }
        if (bestValue > 0.35f)
            return 0.0f;
    }

    const auto left = normalizedDifference[static_cast<size_t>(juce::jmax(minLag, bestLag - 1))];
    const auto centre = normalizedDifference[static_cast<size_t>(bestLag)];
    const auto right = normalizedDifference[static_cast<size_t>(juce::jmin(maxLag, bestLag + 1))];
    const auto curvature = left - 2.0f * centre + right;
    const auto offset = std::abs(curvature) > 1.0e-8f
        ? 0.5f * (left - right) / curvature : 0.0f;
    const auto refinedLag = static_cast<float>(bestLag)
        + juce::jlimit(-0.5f, 0.5f, offset);
    return static_cast<float>(decimatedRate / refinedLag);
}


void HarmonizerEngine::estimateFormantEnvelope() noexcept
{
    // LPC spectral-envelope analysis. A Hann-windowed frame and bandwidth
    // expansion keep the all-pole synthesis filter well behaved between updates.
    std::array<double, lpcOrder + 1> autocorrelation {};
    double mean = 0.0;
    const auto newest = static_cast<int64_t>(totalSamples) - 1;
    if (newest < lpcFrameSize)
        return;

    for (int i = 0; i < lpcFrameSize; ++i)
    {
        const auto absolute = newest - (lpcFrameSize - 1 - i);
        mean += inputHistory[static_cast<size_t>(absolute) & inputRingMask];
    }
    mean /= static_cast<double>(lpcFrameSize);

    std::array<double, lpcFrameSize> frame {};
    for (int i = 0; i < lpcFrameSize; ++i)
    {
        const auto absolute = newest - (lpcFrameSize - 1 - i);
        const auto window = 0.5 - 0.5 * std::cos(
            2.0 * 3.14159265358979323846 * static_cast<double>(i)
            / static_cast<double>(lpcFrameSize - 1));
        frame[static_cast<size_t>(i)] =
            (static_cast<double>(inputHistory[static_cast<size_t>(absolute) & inputRingMask]) - mean)
            * window;
    }

    for (int lag = 0; lag <= lpcOrder; ++lag)
        for (int i = lag; i < lpcFrameSize; ++i)
            autocorrelation[static_cast<size_t>(lag)] +=
                frame[static_cast<size_t>(i)] * frame[static_cast<size_t>(i - lag)];

    if (autocorrelation[0] < 1.0e-8)
    {
        targetFormantCoefficients.fill(0.0f);
        return;
    }

    std::array<double, lpcOrder + 1> predictor {};
    std::array<double, lpcOrder + 1> updated {};
    predictor[0] = 1.0;
    auto predictionError = autocorrelation[0];

    for (int order = 1; order <= lpcOrder; ++order)
    {
        auto residual = autocorrelation[static_cast<size_t>(order)];
        for (int j = 1; j < order; ++j)
            residual += predictor[static_cast<size_t>(j)]
                      * autocorrelation[static_cast<size_t>(order - j)];

        const auto reflection = juce::jlimit(-0.98, 0.98,
            -residual / juce::jmax(1.0e-12, predictionError));
        updated = predictor;
        for (int j = 1; j < order; ++j)
            updated[static_cast<size_t>(j)] =
                predictor[static_cast<size_t>(j)]
                + reflection * predictor[static_cast<size_t>(order - j)];
        updated[static_cast<size_t>(order)] = reflection;
        predictor = updated;
        predictionError *= 1.0 - reflection * reflection;
    }

    constexpr double bandwidthExpansion = 0.994;
    double expansion = bandwidthExpansion;
    for (int i = 0; i < lpcOrder; ++i)
    {
        targetFormantCoefficients[static_cast<size_t>(i)] =
            static_cast<float>(predictor[static_cast<size_t>(i + 1)] * expansion);
        expansion *= bandwidthExpansion;
    }
}

void HarmonizerEngine::scheduleGrain(PitchVoice& voice, int64_t sourceCentre,
                                     double sourcePeriod, int64_t synthesisCentre) noexcept
{
    // TD-PSOLA grains span two source periods. Their centres are spaced at
    // the target period; changing the window to the target period as well
    // changes the source waveform segment and creates avoidable timbral
    // modulation during large pitch shifts.
    const auto halfWindow = juce::jlimit(8, 1024,
        static_cast<int>(std::ceil(sourcePeriod)));
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

        voice.overlap[outputIndex] += residualHistory[sourceIndex] * window;
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

        const auto targetHz = 440.0 * std::pow(2.0,
            (static_cast<double>(voice.note - 69) + pitchBendSemitones) / 12.0);
        const auto targetPeriod = currentSampleRate / targetHz;
        if (!voice.synthesisMarkInitialized)
        {
            voice.nextSynthesisMark = static_cast<double>(delayedMark);
            voice.synthesisMarkInitialized = true;
        }

        // Classic TD-PSOLA correspondence: every detected input pitch mark
        // contributes exactly one source grain to each active harmony voice.
        // The source marks therefore remain sequential and the synthesis marks
        // are spaced at the requested target period. Scheduling grains by
        // comparing the output clock with the input clock can reuse or skip
        // source marks during large transpositions, creating roughness and
        // crackling that becomes more obvious in chords.
        scheduleGrain(voice, mark, sourcePeriod,
                      static_cast<int64_t>(std::llround(voice.nextSynthesisMark)));
        voice.nextSynthesisMark += targetPeriod;
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

    const auto estimatedPeriod = currentSampleRate / detectedPitchHz;
    const auto searchRadius = juce::jmax(2,
        static_cast<int>(std::ceil(estimatedPeriod * 0.22)));
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
    float bestPeak = -std::numeric_limits<float>::max();

    // Keep marks on one waveform polarity. This avoids alternating between
    // positive and negative peaks, which can invert adjacent PSOLA grains.
    for (auto candidate = first; candidate <= last; ++candidate)
    {
        const auto sample = residualHistory[static_cast<size_t>(candidate) & inputRingMask]
                          * pitchMarkPolarity;
        if (sample > bestPeak)
        {
            bestPeak = sample;
            bestMark = candidate;
        }
    }

    if (bestPeak <= 1.0e-6f)
    {
        pitchMarkPolarity = -pitchMarkPolarity;
        bestPeak = -std::numeric_limits<float>::max();
        for (auto candidate = first; candidate <= last; ++candidate)
        {
            const auto sample = residualHistory[static_cast<size_t>(candidate) & inputRingMask]
                              * pitchMarkPolarity;
            if (sample > bestPeak)
            {
                bestPeak = sample;
                bestMark = candidate;
            }
        }
    }

    auto sourcePeriod = estimatedPeriod;
    if (previousPitchMark >= 0 && bestMark > previousPitchMark)
    {
        const auto measuredPeriod = static_cast<double>(bestMark - previousPitchMark);
        // Reject implausible half/double-cycle jumps, but let actual mark
        // spacing gently correct the analysis estimate between pitch updates.
        if (measuredPeriod >= estimatedPeriod * 0.70
            && measuredPeriod <= estimatedPeriod * 1.30)
            sourcePeriod = 0.70 * estimatedPeriod + 0.30 * measuredPeriod;
    }

    if (previousPitchMark >= 0 && bestMark <= previousPitchMark)
        bestMark = previousPitchMark
                 + juce::jmax(1, static_cast<int>(std::llround(estimatedPeriod)));

    processPitchMark(bestMark, sourcePeriod);
    nextExpectedPitchMark = static_cast<double>(bestMark) + sourcePeriod;
}

void HarmonizerEngine::handleMidi(const juce::MidiBuffer& midi, int octaveShift) noexcept
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const auto sourceNote = message.getNoteNumber();
            const auto note = juce::jlimit(0, 127, sourceNote + octaveShift);
            bool found = false;
            for (auto& voice : voices)
            {
                if (voice.sourceNote == sourceNote)
                {
                    voice.start(note, sourceNote, message.getFloatVelocity());
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
                slot->start(note, sourceNote, message.getFloatVelocity());
            }
        }
        else if (message.isNoteOff())
        {
            for (auto& voice : voices)
                if (voice.sourceNote == message.getNoteNumber())
                    voice.stop();
        }
        else if (message.isPitchWheel())
        {
            // Standard two-semitone bend range, centred at MIDI value 8192.
            targetPitchBendSemitones = (static_cast<float>(message.getPitchWheelValue())
                                      - 8192.0f) * (2.0f / 8192.0f);
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
                                    float outputGain, int octaveShift) noexcept
{
    if (numSamples <= 0 || numSamples > maximumBlockSize || output == nullptr)
    {
        if (output != nullptr && numSamples > 0)
            juce::FloatVectorOperations::clear(output, numSamples);
        return;
    }

    handleMidi(midi, juce::jlimit(-2, 2, octaveShift) * 12);
    const auto mix = juce::jlimit(0.0f, 1.0f, dryWet);
    const auto sampleRate = static_cast<float>(currentSampleRate);
    const auto attackCoefficient = 1.0f - std::exp(-1.0f / (0.008f * sampleRate));
    const auto releaseCoefficient = std::exp(-1.0f / (0.045f * sampleRate));
    const auto voicedAttack = 1.0f - std::exp(-1.0f / (0.006f * sampleRate));
    const auto voicedRelease = 1.0f - std::exp(-1.0f / (0.008f * sampleRate));
    const auto updateInterval = juce::jmax(1, static_cast<int>(currentSampleRate * 0.010));
    const auto formantSmoothing = 1.0f - std::exp(-1.0f / (0.025f * sampleRate));
    const auto transientAttack = 1.0f - std::exp(-1.0f / (0.0015f * sampleRate));
    const auto transientRelease = 1.0f - std::exp(-1.0f / (0.010f * sampleRate));
    const auto fastEnergyCoefficient = 1.0f - std::exp(-1.0f / (0.0015f * sampleRate));
    const auto slowEnergyCoefficient = 1.0f - std::exp(-1.0f / (0.050f * sampleRate));
    const auto bendSmoothing = 1.0f - std::exp(-1.0f / (0.010f * sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        pitchBendSemitones += (targetPitchBendSemitones - pitchBendSemitones) * bendSmoothing;
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

        if (++formantUpdateCounter >= updateInterval)
        {
            formantUpdateCounter = 0;
            estimateFormantEnvelope();
        }
        for (int coefficient = 0; coefficient < lpcOrder; ++coefficient)
            formantCoefficients[static_cast<size_t>(coefficient)] +=
                (targetFormantCoefficients[static_cast<size_t>(coefficient)]
                 - formantCoefficients[static_cast<size_t>(coefficient)]) * formantSmoothing;

        const auto historyIndex = static_cast<size_t>(absolute) & inputRingMask;
        float residual = source;
        for (int coefficient = 0; coefficient < lpcOrder; ++coefficient)
        {
            const auto previousAbsolute = absolute - coefficient - 1;
            if (previousAbsolute >= 0)
                residual += formantCoefficients[static_cast<size_t>(coefficient)]
                    * inputHistory[static_cast<size_t>(previousAbsolute) & inputRingMask];
            formantCoefficientHistory[static_cast<size_t>(coefficient)][historyIndex] =
                formantCoefficients[static_cast<size_t>(coefficient)];
        }
        residualHistory[historyIndex] = residual;

        const auto slope = source - previousInputSample;
        previousInputSample = source;
        fastEnergy += fastEnergyCoefficient * (source * source - fastEnergy);
        slowEnergy += slowEnergyCoefficient * (source * source - slowEnergy);
        slopeEnergy += fastEnergyCoefficient * (slope * slope - slopeEnergy);
        if (transientCooldownSamples > 0)
            --transientCooldownSamples;
        if (transientCooldownSamples == 0
            && fastEnergy > slowEnergy * 3.5f
            && slopeEnergy > fastEnergy * 0.12f
            && fastEnergy > 1.0e-5f)
        {
            transientHoldSamples = juce::jmax(transientHoldSamples,
                static_cast<int>(currentSampleRate * 0.012));
            transientCooldownSamples = static_cast<int>(currentSampleRate * 0.030);
        }
        const auto transientDetected = transientHoldSamples > 0;
        if (transientHoldSamples > 0)
            --transientHoldSamples;
        transientMix += ((transientDetected ? 1.0f : 0.0f) - transientMix)
                      * (transientDetected ? transientAttack : transientRelease);

        const auto voiced = detectedPitchHz >= minimumPitchHz
                         && detectedPitchHz <= maximumPitchHz;
        voicedMix += ((voiced ? 1.0f : 0.0f) - voicedMix)
                   * (voiced ? voicedAttack : voicedRelease);

        if (transientDetected)
        {
            // Keep broadband attacks and consonant onsets out of the periodic
            // grain train; they are passed through on the aligned dry path.
            havePitchMark = false;
            previousPitchMark = -1;
            previousPitchPeriod = 0.0;
            for (auto& voice : voices)
                voice.synthesisMarkInitialized = false;
        }
        else
            detectAndProcessPitchMark();

        const auto delayedAbsolute = absolute - latencySamples;
        const auto delayedInput = delayedAbsolute >= 0
            ? inputHistory[static_cast<size_t>(delayedAbsolute) & inputRingMask] : 0.0f;
        float voicedHarmony = 0.0f;
        float unvoicedGain = 0.0f;
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
                    voice.sourceNote = -1;
                    voice.velocity = 0.0f;
                    voice.envelope = 0.0f;
                    voice.releasing = false;
                    voice.synthesisMarkInitialized = false;
                    continue;
                }
            }
            else
                voice.envelope += (1.0f - voice.envelope) * attackCoefficient;

            // Reconstruct the source vocal-tract spectral envelope after
            // pitch-shifting the LPC residual. Coefficients are read from the
            // matching delayed input time so formant motion stays aligned.
            auto formantOutput = psola;
            if (delayedAbsolute >= 0)
            {
                const auto coefficientIndex =
                    static_cast<size_t>(delayedAbsolute) & inputRingMask;
                for (int coefficient = 0; coefficient < lpcOrder; ++coefficient)
                    formantOutput -=
                        formantCoefficientHistory[static_cast<size_t>(coefficient)][coefficientIndex]
                        * voice.formantState[static_cast<size_t>(coefficient)];
            }
            for (int coefficient = lpcOrder - 1; coefficient > 0; --coefficient)
                voice.formantState[static_cast<size_t>(coefficient)] =
                    voice.formantState[static_cast<size_t>(coefficient - 1)];
            voice.formantState[0] = formantOutput;

            const auto voiceLevel = voice.velocity * voice.envelope;
            voicedHarmony += formantOutput * voiceLevel;
            unvoicedGain = juce::jmax(unvoicedGain, voiceLevel);
            ++activeCount;
        }

        // Unvoiced consonants contain no stable pitch to transpose. Route them
        // once, shared by the active chord, instead of summing an identical dry
        // consonant through every voice (which boosts S/SH/F levels by sqrt(N)).
        const auto normalization = activeCount > 0
            ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 0.0f;
        auto harmony = voicedHarmony * normalization * voicedMix
                     + delayedInput * (1.0f - voicedMix) * unvoicedGain;
        // Briefly favour the latency-aligned direct signal at detected attacks.
        // This avoids smearing plosives and sharp consonant onsets across PSOLA grains.
        harmony += transientMix * (delayedInput * unvoicedGain - harmony);
        output[i] = (delayedInput * (1.0f - mix)
                   + harmony * mix) * outputGain;
    }
}
