#include "VocoderEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;

struct RenderStats
{
    double rms = 0.0;
    float peak = 0.0f;
    bool finite = true;
};

RenderStats render(VocoderEngine& engine, double sampleRate, int blockSize,
                   double durationSeconds, float modulatorGain = 0.45f,
                   float carrierGain = 0.5f, bool useCarrier = true)
{
    const auto total = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<float> samples;
    samples.reserve(total);
    std::vector<float> block(static_cast<size_t>(blockSize));

    for (size_t offset = 0; offset < total; offset += static_cast<size_t>(blockSize))
    {
        const auto count = std::min(static_cast<size_t>(blockSize), total - offset);
        for (size_t i = 0; i < count; ++i)
        {
            const auto t = static_cast<double>(offset + i) / sampleRate;
            const auto modulator = modulatorGain * static_cast<float>(std::sin(2.0 * pi * 220.0 * t));
            const auto carrier = carrierGain * static_cast<float>(std::sin(2.0 * pi * 440.0 * t));
            block[i] = engine.processSample(modulator, carrier, useCarrier, carrier);
        }
        samples.insert(samples.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(count));
    }

    RenderStats stats;
    double energy = 0.0;
    for (const auto sample : samples)
    {
        stats.finite = stats.finite && std::isfinite(sample);
        stats.peak = std::max(stats.peak, std::abs(sample));
        energy += static_cast<double>(sample) * sample;
    }
    stats.rms = std::sqrt(energy / static_cast<double>(std::max<size_t>(1, samples.size())));
    return stats;
}

bool testSilenceAndMissingCarrier()
{
    VocoderEngine engine;
    engine.prepare(48000.0);
    engine.setDryWet(1.0f);

    float silencePeak = 0.0f;
    for (int i = 0; i < 48000; ++i)
        silencePeak = std::max(silencePeak, std::abs(engine.processSample(0.0f, 0.0f, true, 0.0f)));

    engine.reset();
    float missingCarrierPeak = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const auto t = static_cast<double>(i) / 48000.0;
        const auto voice = 0.5f * static_cast<float>(std::sin(2.0 * pi * 220.0 * t));
        missingCarrierPeak = std::max(missingCarrierPeak,
            std::abs(engine.processSample(voice, 0.0f, true, 0.0f)));
    }

    std::cout << "Silence peak=" << silencePeak
              << "; missing-carrier peak=" << missingCarrierPeak << '\n';
    return silencePeak < 1.0e-7f && missingCarrierPeak < 1.0e-7f;
}

bool testDryWetEndpoints()
{
    VocoderEngine engine;
    engine.prepare(48000.0);
    engine.setDryWet(0.0f);
    float maximumError = 0.0f;
    for (int i = 0; i < 4096; ++i)
    {
        const auto input = 0.37f * std::sin(static_cast<float>(i) * 0.071f);
        const auto output = engine.processSample(input, 0.4f, true, 0.4f);
        maximumError = std::max(maximumError, std::abs(output - input));
    }

    engine.reset();
    engine.setDryWet(1.0f);
    const auto wet = render(engine, 48000.0, 256, 0.75);
    std::cout << "Dry bypass maximum error=" << maximumError
              << "; wet RMS=" << wet.rms << "; peak=" << wet.peak << '\n';
    return maximumError < 1.0e-6f && wet.finite && wet.rms > 1.0e-4
        && wet.peak < 1.5f;
}

bool testDeterministicReset()
{
    VocoderEngine engine;
    engine.prepare(48000.0);
    engine.setDryWet(1.0f);
    constexpr int samples = 12000;
    std::array<float, samples> first {};
    std::array<float, samples> second {};

    const auto capture = [&](float* destination)
    {
        for (int i = 0; i < samples; ++i)
        {
            const auto t = static_cast<double>(i) / 48000.0;
            const auto voice = 0.4f * static_cast<float>(std::sin(2.0 * pi * 220.0 * t));
            const auto carrier = 0.5f * static_cast<float>(std::sin(2.0 * pi * 440.0 * t));
            destination[i] = engine.processSample(voice, carrier, true, carrier);
        }
    };

    capture(first.data());
    engine.reset();
    capture(second.data());
    float maximumDifference = 0.0f;
    for (int i = 0; i < samples; ++i)
        maximumDifference = std::max(maximumDifference, std::abs(first[i] - second[i]));

    std::cout << "Reset determinism maximum difference=" << maximumDifference << '\n';
    return maximumDifference < 1.0e-6f;
}

bool testSampleRatesAndBlockSizes()
{
    constexpr std::array<double, 3> sampleRates { 44100.0, 48000.0, 96000.0 };
    constexpr std::array<int, 4> blockSizes { 64, 128, 512, 1024 };
    bool passed = true;

    for (const auto sampleRate : sampleRates)
    {
        for (const auto blockSize : blockSizes)
        {
            VocoderEngine engine;
            engine.prepare(sampleRate);
            engine.setBandCount(56);
            engine.setFilterQ(5.0f);
            engine.setAttackMs(0.5f);
            engine.setReleaseMs(30.0f);
            engine.setFrequencyRange(120.0f, static_cast<float>(sampleRate * 0.40));
            engine.setFormantShift(2.0f);
            engine.setDryWet(1.0f);
            const auto stats = render(engine, sampleRate, blockSize, 0.5);
            const auto ok = stats.finite && std::isfinite(stats.rms)
                && stats.rms > 1.0e-5 && stats.peak < 1.5f;
            std::cout << "Vocoder " << sampleRate << " Hz / block " << blockSize
                      << ": RMS=" << stats.rms << "; peak=" << stats.peak
                      << "; finite=" << stats.finite << "; " << (ok ? "PASS" : "FAIL") << '\n';
            passed = passed && ok;
        }
    }
    return passed;
}

bool testConfigurationChangesRemainFinite()
{
    VocoderEngine engine;
    engine.prepare(48000.0);
    engine.setDryWet(1.0f);
    constexpr std::array<int, 5> bandCounts { 24, 32, 40, 48, 56 };
    constexpr std::array<float, 5> qValues { 2.0f, 3.0f, 4.0f, 6.0f, 10.0f };
    float peak = 0.0f;
    bool finite = true;

    for (size_t configuration = 0; configuration < bandCounts.size(); ++configuration)
    {
        engine.setBandCount(bandCounts[configuration]);
        engine.setFilterQ(qValues[configuration]);
        for (int i = 0; i < 12000; ++i)
        {
            const auto t = static_cast<double>(i) / 48000.0;
            const auto voice = 0.45f * static_cast<float>(std::sin(2.0 * pi * 220.0 * t));
            const auto carrier = 0.5f * static_cast<float>(std::sin(2.0 * pi * 440.0 * t));
            const auto output = engine.processSample(voice, carrier, true, carrier);
            finite = finite && std::isfinite(output);
            peak = std::max(peak, std::abs(output));
        }
    }

    std::cout << "Configuration sweep peak=" << peak << "; finite=" << finite << '\n';
    return finite && peak < 1.5f;
}
}

int main()
{
    bool passed = true;
    passed = testSilenceAndMissingCarrier() && passed;
    passed = testDryWetEndpoints() && passed;
    passed = testDeterministicReset() && passed;
    passed = testSampleRatesAndBlockSizes() && passed;
    passed = testConfigurationChangesRemainFinite() && passed;

    std::cout << (passed ? "All vocoder DSP tests passed.\n"
                         : "One or more vocoder DSP tests failed.\n");
    return passed ? 0 : 1;
}
