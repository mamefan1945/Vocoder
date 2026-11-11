#include "HarmonizerEngine.h"
#include <cmath>

void HarmonizerEngine::prepare(double sampleRate) noexcept
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);
    reset();
}

void HarmonizerEngine::reset() noexcept
{
    for (auto& voice : voices)
    {
        voice.delay.fill(0.0f);
        voice.writePosition = 0;
        voice.note = -1;
        voice.velocity = 0.0f;
        voice.phase = 0.0f;
        voice.envelope = 0.0f;
        voice.releasing = false;
        voice.synthesisHistory.fill(0.0f);
    }
    analysisHistory.fill(0.0f);
    formantCoefficients.fill(0.0f);
    smoothedFormants.fill(0.0f);
    pitchHistory.fill(0.0f);
    historyWrite = 0;
    pitchUpdateCounter = 0;
    historySamples = 0;
    detectedPitchHz = 0.0f;
}

void HarmonizerEngine::PitchVoice::start(int midiNote, float noteVelocity) noexcept
{
    note = midiNote;
    velocity = noteVelocity;
    phase = 0.0f;
    envelope = 0.0f;
    synthesisHistory.fill(0.0f);
    releasing = false;
}

void HarmonizerEngine::PitchVoice::stop() noexcept
{
    releasing = true;
}

float HarmonizerEngine::PitchVoice::process(float input, float sourceHz,
                                             double sampleRate,
                                             const std::array<float, lpcOrder + 1>& formants) noexcept
{
    delay[static_cast<size_t>(writePosition)] = input;
    writePosition = (writePosition + 1) % ringSize;
    if (note < 0)
        return 0.0f;
    if (sourceHz < 35.0f || sourceHz > 1200.0f)
    {
        if (releasing)
        {
            envelope *= 0.9979f;
            if (envelope < 0.001f)
            {
                note = -1;
                velocity = 0.0f;
                releasing = false;
            }
        }
        return 0.0f;
    }

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(note);
    const auto ratio = juce::jlimit(0.125f, 8.0f, static_cast<float>(targetHz / sourceHz));
    const auto maxDelay = static_cast<float>(sampleRate * 0.030);
    const auto phaseRate = std::abs(1.0f - ratio) / juce::jmax(1.0f, maxDelay);
    phase += phaseRate;
    phase -= std::floor(phase);

    const auto readTap = [this, maxDelay](float p)
    {
        const auto delaySamples = 8.0f + p * maxDelay;
        auto position = static_cast<float>(writePosition) - delaySamples;
        while (position < 0.0f)
            position += static_cast<float>(ringSize);
        const auto index0 = static_cast<int>(position) % ringSize;
        const auto index1 = (index0 + 1) % ringSize;
        const auto fraction = position - std::floor(position);
        return delay[static_cast<size_t>(index0)] * (1.0f - fraction)
             + delay[static_cast<size_t>(index1)] * fraction;
    };

    const auto phaseA = phase;
    const auto phaseB = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
    const auto gainA = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phaseA);
    const auto gainB = 1.0f - gainA;
    const auto tapA = readTap(ratio >= 1.0f ? 1.0f - phaseA : phaseA);
    const auto tapB = readTap(ratio >= 1.0f ? 1.0f - phaseB : phaseB);
    const auto shifted = tapA * gainA + tapB * gainB;
    if (releasing)
    {
        envelope *= 0.9979f;
        if (envelope < 0.001f)
        {
            note = -1;
            velocity = 0.0f;
            releasing = false;
        }
    }
    else
        envelope += (1.0f - envelope) * 0.002f;
    // Reconstruct the original vocal spectral envelope after pitch shifting.
    // The input was LPC-whitened before entering the delay line.
    auto reconstructed = shifted;
    for (int k = 1; k <= lpcOrder; ++k)
        reconstructed += formants[static_cast<size_t>(k)]
                       * synthesisHistory[static_cast<size_t>(k - 1)];
    for (int k = lpcOrder - 1; k > 0; --k)
        synthesisHistory[static_cast<size_t>(k)] = synthesisHistory[static_cast<size_t>(k - 1)];
    synthesisHistory[0] = juce::jlimit(-4.0f, 4.0f, reconstructed);
    return synthesisHistory[0] * velocity * envelope;
}

float HarmonizerEngine::estimatePitch() const noexcept
{
    // YIN difference/CMND on a 2:1 decimated history. This is substantially
    // less prone to octave errors than selecting the largest autocorrelation.
    constexpr int count = 512;
    constexpr int minLag = 18;   // about 612 Hz at 22.05 kHz
    constexpr int maxLag = 400;  // about 27.6 Hz at 22.05 kHz
    std::array<float, maxLag + 1> difference {};

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float sum = 0.0f;
        for (int i = 0; i < count - lag; ++i)
        {
            const auto a = pitchHistory[static_cast<size_t>((historyWrite + i * 2) % 1024)];
            const auto b = pitchHistory[static_cast<size_t>((historyWrite + (i + lag) * 2) % 1024)];
            const auto delta = a - b;
            sum += delta * delta;
        }
        difference[static_cast<size_t>(lag)] = sum;
    }

    float runningSum = 0.0f;
    std::array<float, maxLag + 1> normalized {};
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        runningSum += difference[static_cast<size_t>(lag)];
        normalized[static_cast<size_t>(lag)] = runningSum > 1.0e-12f
            ? difference[static_cast<size_t>(lag)] * static_cast<float>(lag) / runningSum
            : 1.0f;
    }

    int bestLag = 0;
    constexpr float yinThreshold = 0.18f;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        if (normalized[static_cast<size_t>(lag)] < yinThreshold)
        {
            while (lag < maxLag
                   && normalized[static_cast<size_t>(lag + 1)]
                        < normalized[static_cast<size_t>(lag)])
                ++lag;
            bestLag = lag;
            break;
        }
    }

    if (bestLag == 0)
        return 0.0f;

    // Parabolic interpolation reduces quantization/jitter between lag bins.
    const auto left = normalized[static_cast<size_t>(juce::jmax(minLag, bestLag - 1))];
    const auto centre = normalized[static_cast<size_t>(bestLag)];
    const auto right = normalized[static_cast<size_t>(juce::jmin(maxLag, bestLag + 1))];
    const auto denominator = left - 2.0f * centre + right;
    const auto offset = std::abs(denominator) > 1.0e-9f
        ? 0.5f * (left - right) / denominator : 0.0f;
    const auto refinedLag = static_cast<float>(bestLag) + juce::jlimit(-0.5f, 0.5f, offset);

    // The history is decimated by two, so its effective sample rate is Fs / 2.
    return static_cast<float>(currentSampleRate / (2.0 * refinedLag));
}

std::array<float, HarmonizerEngine::lpcOrder + 1>
HarmonizerEngine::estimateFormants() const noexcept
{
    constexpr int frameSize = 256;
    std::array<double, lpcOrder + 1> autocorrelation {};
    std::array<double, lpcOrder + 1> coefficients {};
    std::array<double, lpcOrder + 1> previous {};
    constexpr double epsilon = 1.0e-9;

    // A Hann-windowed, decimated frame gives a smooth vocal-tract envelope.
    for (int lag = 0; lag <= lpcOrder; ++lag)
    {
        double sum = 0.0;
        for (int n = lag; n < frameSize; ++n)
        {
            const auto windowA = 0.5 - 0.5 * std::cos(
                juce::MathConstants<double>::twoPi * n / (frameSize - 1));
            const auto windowB = 0.5 - 0.5 * std::cos(
                juce::MathConstants<double>::twoPi * (n - lag) / (frameSize - 1));
            const auto a = static_cast<double>(pitchHistory[static_cast<size_t>(
                (historyWrite + (n + 256) * 2) % 1024)]) * windowA;
            const auto b = static_cast<double>(pitchHistory[static_cast<size_t>(
                (historyWrite + (n - lag + 256) * 2 + 1024) % 1024)]) * windowB;
            sum += a * b;
        }
        autocorrelation[static_cast<size_t>(lag)] = sum;
    }

    if (autocorrelation[0] < epsilon)
        return {};

    double predictionError = autocorrelation[0];
    for (int order = 1; order <= lpcOrder; ++order)
    {
        double reflection = autocorrelation[static_cast<size_t>(order)];
        for (int j = 1; j < order; ++j)
            reflection -= coefficients[static_cast<size_t>(j)]
                        * autocorrelation[static_cast<size_t>(order - j)];
        reflection /= juce::jmax(epsilon, predictionError);
        reflection = juce::jlimit(-0.82, 0.82, reflection);

        previous = coefficients;
        coefficients[static_cast<size_t>(order)] = reflection;
        for (int j = 1; j < order; ++j)
            coefficients[static_cast<size_t>(j)] =
                previous[static_cast<size_t>(j)]
                - reflection * previous[static_cast<size_t>(order - j)];
        predictionError *= 1.0 - reflection * reflection;
        predictionError = juce::jmax(predictionError, epsilon);
    }

    std::array<float, lpcOrder + 1> result {};
    for (int k = 1; k <= lpcOrder; ++k)
        result[static_cast<size_t>(k)] =
            static_cast<float>(coefficients[static_cast<size_t>(k)]);
    return result;
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
                if (voice.note == note)
                {
                    voice.start(note, message.getFloatVelocity());
                    found = true;
                    break;
                }
            if (!found)
            {
                auto* slot = &voices[0];
                for (auto& voice : voices)
                    if (voice.note < 0) { slot = &voice; break; }
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
    handleMidi(midi);
    const auto mix = juce::jlimit(0.0f, 1.0f, dryWet);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto sample = input != nullptr ? input[i] : 0.0f;
        pitchHistory[static_cast<size_t>(historyWrite)] = sample;
        historyWrite = (historyWrite + 2) % 1024;
        historySamples = juce::jmin(1024, historySamples + 2);
        if (++pitchUpdateCounter >= 512)
        {
            pitchUpdateCounter = 0;
            if (historySamples >= 1024)
                {
                const auto newFormants = estimateFormants();
                for (int k = 1; k <= lpcOrder; ++k)
                    smoothedFormants[static_cast<size_t>(k)] += 0.12f
                        * (newFormants[static_cast<size_t>(k)]
                           - smoothedFormants[static_cast<size_t>(k)]);
                formantCoefficients = smoothedFormants;
                const auto estimate = estimatePitch();
                if (estimate > 0.0f)
                    detectedPitchHz = detectedPitchHz > 0.0f
                        ? detectedPitchHz + 0.35f * (estimate - detectedPitchHz)
                        : estimate;
            }
        }

        // LPC analysis filter: remove the current vocal-tract envelope before
        // pitch shifting so the shifted residual can be resynthesized with it.
        float residual = sample;
        for (int k = 1; k <= lpcOrder; ++k)
            residual -= formantCoefficients[static_cast<size_t>(k)]
                      * analysisHistory[static_cast<size_t>(k - 1)];
        for (int k = lpcOrder - 1; k > 0; --k)
            analysisHistory[static_cast<size_t>(k)] = analysisHistory[static_cast<size_t>(k - 1)];
        analysisHistory[0] = sample;

        float harmony = 0.0f;
        int activeCount = 0;
        for (auto& voice : voices)
            if (voice.note >= 0)
            {
                harmony += voice.process(residual, detectedPitchHz, currentSampleRate, formantCoefficients);
                ++activeCount;
            }
        const auto normalization = activeCount > 0
            ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 0.0f;
        output[i] = (sample * (1.0f - mix) + harmony * normalization * mix) * outputGain;
    }
}
