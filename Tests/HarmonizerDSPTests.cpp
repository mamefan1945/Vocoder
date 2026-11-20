#include "HarmonizerEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr double pi = 3.14159265358979323846;

struct Measurement
{
    float rms = 0.0f;
    float peak = 0.0f;
    double dominantHz = 0.0;
    double targetMagnitude = 0.0;
    double sourceMagnitude = 0.0;
};

double magnitudeAt(const std::vector<float>& samples, size_t first,
                   size_t count, double frequency)
{
    const auto omega = 2.0 * pi * frequency / sampleRate;
    const auto coefficient = 2.0 * std::cos(omega);
    double q1 = 0.0;
    double q2 = 0.0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto window = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(i) / static_cast<double>(count - 1));
        const auto value = static_cast<double>(samples[first + i]) * window;
        const auto q0 = value + coefficient * q1 - q2;
        q2 = q1;
        q1 = q0;
    }
    return std::sqrt(std::max(0.0, q1 * q1 + q2 * q2 - coefficient * q1 * q2));
}

Measurement measure(const std::vector<float>& samples)
{
    Measurement result;
    constexpr size_t first = static_cast<size_t>(sampleRate * 0.30);
    constexpr size_t count = static_cast<size_t>(sampleRate * 0.80);
    double squareSum = 0.0;
    for (size_t i = first; i < first + count; ++i)
    {
        const auto value = samples[i];
        squareSum += static_cast<double>(value) * value;
        result.peak = std::max(result.peak, std::abs(value));
    }
    result.rms = static_cast<float>(std::sqrt(squareSum / static_cast<double>(count)));
    result.targetMagnitude = magnitudeAt(samples, first, count, 440.0);
    result.sourceMagnitude = magnitudeAt(samples, first, count, 220.0);
    double strongest = 0.0;
    for (int hz = 380; hz <= 500; ++hz)
    {
        const auto value = magnitudeAt(samples, first, count, static_cast<double>(hz));
        if (value > strongest)
        {
            strongest = value;
            result.dominantHz = static_cast<double>(hz);
        }
    }
    return result;
}

bool testPitchShift(HarmonizerEngine::Algorithm algorithm, const char* name)
{
    auto engine = std::make_unique<HarmonizerEngine>();
    engine->setAlgorithm(algorithm);
    engine->prepare(sampleRate, blockSize);

    constexpr size_t total = static_cast<size_t>(sampleRate * 1.5);
    std::vector<float> rendered(total, 0.0f);
    std::array<float, blockSize> input {};
    std::array<float, blockSize> output {};
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.9f), 0);

    size_t position = 0;
    while (position < total)
    {
        const auto count = static_cast<int>(std::min(
            static_cast<size_t>(blockSize), total - position));
        for (int i = 0; i < count; ++i)
        {
            const auto t = static_cast<double>(position + static_cast<size_t>(i)) / sampleRate;
            input[static_cast<size_t>(i)] =
                0.55f * static_cast<float>(std::sin(2.0 * pi * 220.0 * t))
              + 0.22f * static_cast<float>(std::sin(2.0 * pi * 440.0 * t))
              + 0.10f * static_cast<float>(std::sin(2.0 * pi * 660.0 * t));
        }
        engine->processBlock(input.data(), output.data(), count, midi, 1.0f, 1.0f, 0);
        midi.clear();
        std::copy_n(output.data(), count, rendered.data() + position);
        position += static_cast<size_t>(count);
    }

    const auto result = measure(rendered);
    const auto ratio = result.sourceMagnitude > 1.0e-9
        ? result.targetMagnitude / result.sourceMagnitude : 0.0;
    std::cout << name << ": dominant " << result.dominantHz
              << " Hz; RMS " << result.rms << "; peak " << result.peak
              << "; 440/220 magnitude ratio " << ratio << '\n';

    return result.dominantHz >= 425.0 && result.dominantHz <= 455.0
        && result.rms > 0.005f
        && std::isfinite(result.rms) && std::isfinite(result.peak)
        && result.peak < 2.0f;
}


bool testPitchBend(HarmonizerEngine::Algorithm algorithm, const char* name)
{
    auto engine = std::make_unique<HarmonizerEngine>();
    engine->setAlgorithm(algorithm);
    engine->prepare(sampleRate, blockSize);
    constexpr size_t total = static_cast<size_t>(sampleRate * 1.5);
    std::vector<float> rendered(total, 0.0f);
    std::array<float, blockSize> input {};
    std::array<float, blockSize> output {};
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.9f), 0);
    size_t position = 0;
    while (position < total)
    {
        const auto count = static_cast<int>(std::min(
            static_cast<size_t>(blockSize), total - position));
        for (int i = 0; i < count; ++i)
        {
            const auto t = static_cast<double>(position + static_cast<size_t>(i)) / sampleRate;
            input[static_cast<size_t>(i)] = 0.6f * static_cast<float>(
                std::sin(2.0 * pi * 220.0 * t));
        }
        engine->processBlock(input.data(), output.data(), count, midi, 1.0f, 1.0f, 0);
        midi.clear();
        std::copy_n(output.data(), count, rendered.data() + position);
        position += static_cast<size_t>(count);
    }
    const auto first = static_cast<size_t>(sampleRate * 0.30);
    const auto count = static_cast<size_t>(sampleRate * 0.80);
    double bestMagnitude = 0.0;
    double measuredHz = 0.0;
    for (int hz = 470; hz <= 515; ++hz)
    {
        const auto magnitude = magnitudeAt(rendered, first, count, static_cast<double>(hz));
        if (magnitude > bestMagnitude)
        {
            bestMagnitude = magnitude;
            measuredHz = static_cast<double>(hz);
        }
    }
    std::cout << name << " +2-semitone pitch bend: " << measuredHz << " Hz\n";
    return measuredHz >= 480.0 && measuredHz <= 505.0 && bestMagnitude > 1.0;
}

bool testPolyphony(HarmonizerEngine::Algorithm algorithm, const char* name)
{
    auto engine = std::make_unique<HarmonizerEngine>();
    engine->setAlgorithm(algorithm);
    engine->prepare(sampleRate, blockSize);
    constexpr size_t total = static_cast<size_t>(sampleRate * 1.5);
    std::vector<float> rendered(total, 0.0f);
    std::array<float, blockSize> input {};
    std::array<float, blockSize> output {};
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.8f), 0);
    size_t position = 0;
    while (position < total)
    {
        const auto count = static_cast<int>(std::min(
            static_cast<size_t>(blockSize), total - position));
        for (int i = 0; i < count; ++i)
        {
            const auto t = static_cast<double>(position + static_cast<size_t>(i)) / sampleRate;
            input[static_cast<size_t>(i)] = 0.6f * static_cast<float>(
                std::sin(2.0 * pi * 220.0 * t));
        }
        engine->processBlock(input.data(), output.data(), count, midi, 1.0f, 1.0f, 0);
        midi.clear();
        std::copy_n(output.data(), count, rendered.data() + position);
        position += static_cast<size_t>(count);
    }
    const auto first = static_cast<size_t>(sampleRate * 0.30);
    const auto count = static_cast<size_t>(sampleRate * 0.80);
    const auto noteA = magnitudeAt(rendered, first, count, 440.0);
    const auto noteB = magnitudeAt(rendered, first, count, 523.251);
    std::cout << name << " polyphony: 440 Hz=" << noteA
              << ", 523 Hz=" << noteB << '\n';
    return noteA > 0.5 && noteB > 0.5
        && std::isfinite(noteA) && std::isfinite(noteB);
}

bool testSilenceProducesSilence()
{
    auto engine = std::make_unique<HarmonizerEngine>();
    engine->prepare(sampleRate, blockSize);
    std::array<float, blockSize> input {};
    std::array<float, blockSize> output {};
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.9f), 0);
    float peak = 0.0f;
    for (int block = 0; block < 80; ++block)
    {
        engine->processBlock(input.data(), output.data(), blockSize, midi, 1.0f, 1.0f, 0);
        midi.clear();
        for (const auto value : output)
            peak = std::max(peak, std::abs(value));
    }
    std::cout << "Silence/no-input peak: " << peak << '\n';
    return peak < 1.0e-6f;
}

bool testNoMidiProducesNoWetOutput()
{
    auto engine = std::make_unique<HarmonizerEngine>();
    engine->prepare(sampleRate, blockSize);
    std::array<float, blockSize> input {};
    std::array<float, blockSize> output {};
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto t = static_cast<double>(block * blockSize + i) / sampleRate;
            input[static_cast<size_t>(i)] =
                0.5f * static_cast<float>(std::sin(2.0 * pi * 220.0 * t));
        }
        engine->processBlock(input.data(), output.data(), blockSize, midi, 1.0f, 1.0f, 0);
        for (const auto value : output)
            peak = std::max(peak, std::abs(value));
    }
    std::cout << "No-MIDI wet-output peak: " << peak << '\n';
    return peak < 1.0e-6f;
}
}

int main()
{
    bool passed = true;
    passed &= testPitchShift(HarmonizerEngine::Algorithm::timeDomain, "TD-PSOLA");
    passed &= testPitchShift(HarmonizerEngine::Algorithm::lpc, "LP-PSOLA");
    passed &= testPitchShift(HarmonizerEngine::Algorithm::frequencyDomain, "FD-PSOLA");
    passed &= testPitchBend(HarmonizerEngine::Algorithm::timeDomain, "TD-PSOLA");
    passed &= testPitchBend(HarmonizerEngine::Algorithm::lpc, "LP-PSOLA");
    passed &= testPitchBend(HarmonizerEngine::Algorithm::frequencyDomain, "FD-PSOLA");
    passed &= testPolyphony(HarmonizerEngine::Algorithm::timeDomain, "TD-PSOLA");
    passed &= testPolyphony(HarmonizerEngine::Algorithm::lpc, "LP-PSOLA");
    passed &= testPolyphony(HarmonizerEngine::Algorithm::frequencyDomain, "FD-PSOLA");
    passed &= testSilenceProducesSilence();
    passed &= testNoMidiProducesNoWetOutput();
    std::cout << (passed ? "All harmonizer DSP tests passed.\n"
                         : "One or more harmonizer DSP tests failed.\n");
    return passed ? 0 : 1;
}
