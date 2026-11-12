#include "HarmonizerEngine.h"
#include <cmath>

void HarmonizerEngine::PitchVoice::prepare(double sampleRate, int maximumBlockSize)
{
    // A 100 ms analysis window with a 20 ms hop gives a useful quality/latency
    // compromise for live vocals. Split computation avoids large callback spikes.
    const auto block = juce::jmax(1024, static_cast<int>(sampleRate * 0.10));
    const auto interval = juce::jmax(256, static_cast<int>(sampleRate * 0.02));
    shifter.configure(1, block, interval, true);
    shifter.setFormantFactor(1.0f, true);
    shifter.setFormantBase(0.0f);
    shifted.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
}

void HarmonizerEngine::PitchVoice::start(int midiNote, float noteVelocity) noexcept
{
    note = midiNote;
    velocity = noteVelocity;
    envelope = 0.0f;
    releasing = false;
}

void HarmonizerEngine::PitchVoice::stop() noexcept
{
    releasing = true;
}

void HarmonizerEngine::prepare(double sampleRate, int blockSize) noexcept
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);
    maximumBlockSize = juce::jmax(64, blockSize);
    silence.assign(static_cast<size_t>(maximumBlockSize), 0.0f);
    for (auto& voice : voices)
        voice.prepare(currentSampleRate, maximumBlockSize);
    reset();
}

void HarmonizerEngine::reset() noexcept
{
    for (auto& voice : voices)
    {
        voice.shifter.reset();
        voice.note = -1;
        voice.velocity = 0.0f;
        voice.envelope = 0.0f;
        voice.releasing = false;
        std::fill(voice.shifted.begin(), voice.shifted.end(), 0.0f);
    }
    std::fill(pitchHistory.begin(), pitchHistory.end(), 0.0f);
    std::fill(silence.begin(), silence.end(), 0.0f);
    historyWrite = 0;
    pitchUpdateCounter = 0;
    historySamples = 0;
    detectedPitchHz = 0.0f;
}

float HarmonizerEngine::estimatePitch() const noexcept
{
    // YIN difference/CMND on a 2:1 decimated history.
    constexpr int count = 512;
    constexpr int minLag = 18;
    constexpr int maxLag = 400;
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
                   && normalized[static_cast<size_t>(lag + 1)] < normalized[static_cast<size_t>(lag)])
                ++lag;
            bestLag = lag;
            break;
        }
    }
    if (bestLag == 0)
        return 0.0f;

    const auto left = normalized[static_cast<size_t>(juce::jmax(minLag, bestLag - 1))];
    const auto centre = normalized[static_cast<size_t>(bestLag)];
    const auto right = normalized[static_cast<size_t>(juce::jmin(maxLag, bestLag + 1))];
    const auto denominator = left - 2.0f * centre + right;
    const auto offset = std::abs(denominator) > 1.0e-9f
        ? 0.5f * (left - right) / denominator : 0.0f;
    const auto refinedLag = static_cast<float>(bestLag)
        + juce::jlimit(-0.5f, 0.5f, offset);
    return static_cast<float>(currentSampleRate / (2.0 * refinedLag));
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
    if (numSamples <= 0 || numSamples > maximumBlockSize)
    {
        if (output != nullptr && numSamples > 0)
            juce::FloatVectorOperations::clear(output, numSamples);
        return;
    }

    handleMidi(midi);
    const auto* source = input != nullptr ? input : silence.data();

    // Keep every shifter fed continuously, including currently unused voices.
    // This lets a newly pressed MIDI key join the live phrase without a cold start.
    for (auto& voice : voices)
    {
        if (++pitchUpdateCounter >= 512)
        {
            pitchUpdateCounter = 0;
            if (historySamples >= 1024)
            {
                const auto estimate = estimatePitch();
                if (estimate > 0.0f)
                    detectedPitchHz = detectedPitchHz > 0.0f
                        ? detectedPitchHz + 0.35f * (estimate - detectedPitchHz)
                        : estimate;
                else
                    detectedPitchHz *= 0.72f;
                if (detectedPitchHz < 35.0f)
                    detectedPitchHz = 0.0f;
            }
        }
        break;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        pitchHistory[static_cast<size_t>(historyWrite)] = source[i];
        historyWrite = (historyWrite + 2) % 1024;
        historySamples = juce::jmin(1024, historySamples + 2);
    }

    const auto validPitch = detectedPitchHz >= 35.0f && detectedPitchHz <= 1200.0f;
    const auto sourceMidi = validPitch
        ? 69.0f + 12.0f * std::log2(detectedPitchHz / 440.0f) : 69.0f;
    float* inputChannels[1] = { const_cast<float*>(source) };

    for (auto& voice : voices)
    {
        const auto active = voice.note >= 0;
        if (active && validPitch)
        {
            const auto semitones = juce::jlimit(-48.0f, 48.0f,
                static_cast<float>(voice.note) - sourceMidi);
            voice.shifter.setTransposeSemitones(semitones);
            voice.shifter.setFormantBase(static_cast<float>(detectedPitchHz / currentSampleRate));
        }
        else
        {
            voice.shifter.setTransposeSemitones(0.0f);
            voice.shifter.setFormantBase(0.0f);
        }

        float* outputChannels[1] = { voice.shifted.data() };
        voice.shifter.process(inputChannels, numSamples, outputChannels, numSamples);
    }

    const auto mix = juce::jlimit(0.0f, 1.0f, dryWet);
    for (int i = 0; i < numSamples; ++i)
    {
        float harmony = 0.0f;
        int activeCount = 0;
        for (auto& voice : voices)
        {
            if (voice.note < 0)
                continue;

            if (voice.releasing)
            {
                voice.envelope *= 0.9970f;
                if (voice.envelope < 0.001f)
                {
                    voice.note = -1;
                    voice.velocity = 0.0f;
                    voice.releasing = false;
                    continue;
                }
            }
            else
                voice.envelope += (1.0f - voice.envelope) * 0.003f;

            harmony += voice.shifted[static_cast<size_t>(i)] * voice.velocity * voice.envelope;
            ++activeCount;
        }

        const auto normalization = activeCount > 0
            ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 0.0f;
        output[i] = (source[i] * (1.0f - mix) + harmony * normalization * mix) * outputGain;
    }
}
