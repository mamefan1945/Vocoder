#pragma once

#include <JuceHeader.h>
#include <atomic>

class CarrierSynth final : public juce::Synthesiser
{
public:
    enum Waveform { sine = 0, saw, square, triangle, noise, rich };

    CarrierSynth();

    void setWaveform(int value) noexcept { waveform.store(juce::jlimit(0, 5, value)); }
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
            frequency = juce::MidiMessage::getMidiNoteInHertz(shiftedNote);
            level = velocity * 0.22f;
            phase = 0.0;
            pwmPhase = 0.0;
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

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

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
        double phase = 0.0;
        double pwmPhase = 0.0;
        float level = 0.0f;
    };

    std::atomic<int> waveform { saw };
    std::atomic<int> octave { 0 };
};
