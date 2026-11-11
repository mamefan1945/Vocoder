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
    }
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
    releasing = false;
}

void HarmonizerEngine::PitchVoice::stop() noexcept
{
    releasing = true;
}

float HarmonizerEngine::PitchVoice::process(float input, float sourceHz,
                                             double sampleRate) noexcept
{
    delay[static_cast<size_t>(writePosition)] = input;
    writePosition = (writePosition + 1) % ringSize;
    if (note < 0 || sourceHz < 35.0f || sourceHz > 1200.0f)
        return 0.0f;

    const auto targetHz = juce::MidiMessage::getMidiNoteInHertz(note);
    const auto ratio = juce::jlimit(0.5f, 2.0f, static_cast<float>(targetHz / sourceHz));
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
    return shifted * velocity * envelope;
}

float HarmonizerEngine::estimatePitch() const noexcept
{
    constexpr int count = 512;
    constexpr int minLag = 20;
    constexpr int maxLag = 400;
    float bestCorrelation = 0.0f;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float cross = 0.0f;
        float energyA = 0.0f;
        float energyB = 0.0f;
        for (int i = 0; i < count - lag; ++i)
        {
            const auto a = pitchHistory[static_cast<size_t>((historyWrite + i * 2) % 1024)];
            const auto b = pitchHistory[static_cast<size_t>((historyWrite + (i + lag) * 2) % 1024)];
            cross += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const auto normalizer = std::sqrt(energyA * energyB);
        const auto correlation = normalizer > 1.0e-9f ? cross / normalizer : 0.0f;
        if (correlation > bestCorrelation)
        {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }
    if (bestCorrelation < 0.55f || bestLag == 0)
        return 0.0f;
    return static_cast<float>(currentSampleRate / (2.0 * bestLag));
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
        if (++pitchUpdateCounter >= 256)
        {
            pitchUpdateCounter = 0;
            if (historySamples >= 1024)
                detectedPitchHz = estimatePitch();
        }

        float harmony = 0.0f;
        int activeCount = 0;
        for (auto& voice : voices)
            if (voice.note >= 0)
            {
                harmony += voice.process(sample, detectedPitchHz, currentSampleRate);
                ++activeCount;
            }
        const auto normalization = activeCount > 0
            ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 0.0f;
        output[i] = (sample * (1.0f - mix) + harmony * normalization * mix) * outputGain;
    }
}
