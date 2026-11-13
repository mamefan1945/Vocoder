#pragma once

#include <JuceHeader.h>
#include <atomic>

class CarrierSynth final : public juce::Synthesiser
{
public:
    enum Waveform { sine = 0, saw, square, triangle, noise, rich, choir };

    CarrierSynth();

    void setWaveform(int value) noexcept { waveform.store(juce::jlimit(0, 6, value)); }
    void setOctave(int value) noexcept { octave.store(juce::jlimit(-2, 2, value)); }

private:
    class CarrierSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    class CarrierVoice final : public juce::SynthesiserVoice
    {
    public:
        CarrierVoice(std::atomic<int>& selectedWaveform, std::atomic<int>& octaveShift)
            : waveform(selectedWaveform), octave(octaveShift)
        {
            adsr.setParameters({ 0.008f, 0.08f, 0.82f, 0.12f });
        }

        bool canPlaySound(juce::SynthesiserSound* sound) override
        {
            return dynamic_cast<CarrierSound*>(sound) != nullptr;
        }

        void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*,
                       int) override
        {
            const auto shiftedNote = juce::jlimit(0, 127, midiNoteNumber + 12 * octave.load());
            baseFrequency = juce::MidiMessage::getMidiNoteInHertz(shiftedNote);
            updatePitchBend();
            level = velocity * 0.22f;
            phase = 0.0;
            pwmPhase = 0.0;
            choirPhases.fill(0.0);
            choirVibratoPhases = { 0.0, 0.37, 0.73 };
            adsr.noteOn();
        }

        void stopNote(float, bool allowTailOff) override
        {
            if (allowTailOff)
                adsr.noteOff();
            else
            {
                adsr.reset();
                clearCurrentNote();
            }
        }

        void pitchWheelMoved(int newValue) override
        {
            pitchWheel = juce::jlimit(0, 16383, newValue);
            updatePitchBend();
        }
        void controllerMoved(int, int) override {}

        void updatePitchBend() noexcept
        {
            const auto semitones = (static_cast<double>(pitchWheel) - 8192.0) * (2.0 / 8192.0);
            frequency = baseFrequency * std::pow(2.0, semitones / 12.0);
        }

        void setCurrentPlaybackSampleRate(double newRate) override
        {
            sampleRate = juce::jmax(8000.0, newRate);
            adsr.setSampleRate(sampleRate);
        }

        static float polyBlep(float phase01, float phaseIncrement) noexcept
        {
            if (phase01 < phaseIncrement)
            {
                const auto t = phase01 / phaseIncrement;
                return t + t - t * t - 1.0f;
            }

            if (phase01 > 1.0f - phaseIncrement)
            {
                const auto t = (phase01 - 1.0f) / phaseIncrement;
                return t * t + t + t + 1.0f;
            }

            return 0.0f;
        }

        void renderNextBlock(juce::AudioBuffer<float>& output, int startSample,
                             int numSamples) override
        {
            if (!isVoiceActive())
                return;

            auto* destination = output.getWritePointer(0, startSample);
            for (int i = 0; i < numSamples; ++i)
            {
                if (!adsr.isActive())
                {
                    clearCurrentNote();
                    break;
                }

                const auto phase01 = static_cast<float>(phase);
                const auto phaseIncrement = static_cast<float>(frequency / sampleRate);
                float sample = 0.0f;
                switch (waveform.load())
                {
                    // PolyBLEP removes the discontinuity's aliased high-frequency
                    // energy while retaining the dense harmonic spectrum a vocoder needs.
                    case saw:
                        sample = 2.0f * phase01 - 1.0f - polyBlep(phase01, phaseIncrement);
                        break;
                    case square:
                        sample = phase01 < 0.5f ? 1.0f : -1.0f;
                        sample += polyBlep(phase01, phaseIncrement);
                        sample -= polyBlep(std::fmod(phase01 + 0.5f, 1.0f), phaseIncrement);
                        break;
                    case triangle: sample = 1.0f - 4.0f * std::abs(phase01 - 0.5f); break;
                    case rich:
                    {
                        // A richer but still stable carrier: a band-limited saw,
                        // a band-limited pulse with slow per-voice PWM, and a
                        // quieter triangle to fill out the lower harmonics.
                        const auto pulseWidth = 0.35f + 0.12f
                            * std::sin(static_cast<float>(pwmPhase
                                * juce::MathConstants<double>::twoPi));
                        auto pulse = phase01 < pulseWidth ? 1.0f : -1.0f;
                        pulse += polyBlep(phase01, phaseIncrement);
                        pulse -= polyBlep(std::fmod(phase01 - pulseWidth + 1.0f, 1.0f),
                                          phaseIncrement);
                        const auto saw = 2.0f * phase01 - 1.0f
                            - polyBlep(phase01, phaseIncrement);
                        const auto triangle = 1.0f - 4.0f * std::abs(phase01 - 0.5f);
                        sample = 0.50f * saw + 0.35f * pulse + 0.15f * triangle;
                        break;
                    }
                    case choir:
                    {
                        // Three gently detuned choir layers with independent vibrato.
                        // A vowel-coloured harmonic stack keeps the carrier rich while
                        // retaining a strong fundamental for stable pitch perception.
                        constexpr double detune[] = { 0.9965, 1.0, 1.0035 };
                        constexpr float harmonics[] = { 1.0f, 0.46f, 0.30f,
                                                        0.18f, 0.11f, 0.065f };
                        float choirSample = 0.0f;
                        for (size_t layer = 0; layer < 3; ++layer)
                        {
                            const auto vibrato = 1.0
                                + 0.0015 * std::sin(choirVibratoPhases[layer]);
                            const auto layerIncrement = frequency * detune[layer]
                                * vibrato / sampleRate;
                            for (size_t harmonic = 0; harmonic < 6; ++harmonic)
                            {
                                const auto harmonicPhase = choirPhases[layer]
                                    * static_cast<double>(harmonic + 1);
                                choirSample += harmonics[harmonic]
                                    * static_cast<float>(std::sin(
                                        harmonicPhase * juce::MathConstants<double>::twoPi));
                            }
                            choirPhases[layer] += layerIncrement;
                            choirPhases[layer] -= std::floor(choirPhases[layer]);
                            choirVibratoPhases[layer] += juce::MathConstants<double>::twoPi
                                * (4.7 + 0.13 * static_cast<double>(layer)) / sampleRate;
                            choirVibratoPhases[layer] -= std::floor(
                                choirVibratoPhases[layer] / juce::MathConstants<double>::twoPi)
                                * juce::MathConstants<double>::twoPi;
                        }
                        sample = choirSample / 6.345f;
                        break;
                    }
                    case noise:    sample = random.nextFloat() * 2.0f - 1.0f; break;
                    default:       sample = std::sin(static_cast<float>(phase * juce::MathConstants<double>::twoPi)); break;
                }

                destination[i] += sample * level * adsr.getNextSample();
                phase += phaseIncrement;
                phase -= std::floor(phase);
                pwmPhase += 0.23 / sampleRate;
                pwmPhase -= std::floor(pwmPhase);
            }
        }

    private:
        std::atomic<int>& waveform;
        std::atomic<int>& octave;
        juce::ADSR adsr;
        juce::Random random;
        double sampleRate = 44100.0;
        double frequency = 440.0;
        double baseFrequency = 440.0;
        int pitchWheel = 8192;
        double phase = 0.0;
        double pwmPhase = 0.0;
        std::array<double, 3> choirPhases {};
        std::array<double, 3> choirVibratoPhases {};
        float level = 0.0f;
    };

    std::atomic<int> waveform { saw };
    std::atomic<int> octave { 0 };
};
