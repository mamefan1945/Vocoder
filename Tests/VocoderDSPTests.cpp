#include "VocoderEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <cstdint>
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

struct SpeechMetrics
{
    double inputRms = 0.0;
    double outputRms = 0.0;
    double outputHighBandRms = 0.0;
    float outputPeak = 0.0f;
    double crestFactor = 0.0;
    double activeFrameRatio = 0.0;
    bool finite = true;
};

bool testReferenceSpeechFile(const juce::String& filename)
{
#ifndef PRISMAVOX_TEST_AUDIO_DIR
    juce::ignoreUnused(filename);
    std::cout << "Reference speech test skipped: audio directory is not configured.\n";
    return true;
#else
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto file = juce::File(PRISMAVOX_TEST_AUDIO_DIR).getChildFile(filename);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
    {
        std::cerr << "Could not read reference speech file: " << file.getFullPathName() << '\n';
        return false;
    }

    const auto sampleCount = static_cast<int>(juce::jmin<juce::int64>(
        reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 120.0)));
    juce::AudioBuffer<float> input(1, sampleCount);
    if (!reader->read(&input, 0, sampleCount, 0, true, false))
    {
        std::cerr << "Failed reading reference speech samples: " << filename << '\n';
        return false;
    }

    const auto sampleRate = reader->sampleRate;
    if (sampleRate < 8000.0 || sampleRate > 192000.0)
    {
        std::cerr << "Unsupported reference speech sample rate: " << sampleRate << '\n';
        return false;
    }

    VocoderEngine engine;
    engine.prepare(sampleRate);
    engine.setDryWet(1.0f);
    engine.setOutputGain(1.0f);

    // Representative reduced-noise tuning based on the user's normal setup.
    // UI values are percentages; the engine receives normalized 0.0-2.0 amounts.
    engine.setAttackMs(10.0f);
    engine.setReleaseMs(10.0f);
    engine.setFormantShift(-2.0f);
    engine.setBandCount(56);
    engine.setFilterQ(7.0f);
    engine.setUnvoicedNoise(0.10f);
    engine.setSibilanceAmount(0.10f);

    auto highPass = juce::dsp::IIR::Filter<float>();
    highPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        sampleRate, juce::jmin(2500.0, sampleRate * 0.40));

    SpeechMetrics metrics;
    juce::AudioBuffer<float> processed(1, sampleCount);
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    double highBandEnergy = 0.0;
    double frameEnergy = 0.0;
    int frameSamples = 0;
    int activeFrames = 0;
    int totalFrames = 0;
    double maximumFrameRms = 0.0;
    constexpr int frameDurationMs = 20;
    const auto frameLength = juce::jmax(1, juce::roundToInt(sampleRate * frameDurationMs / 1000.0));

    for (int i = 0; i < sampleCount; ++i)
    {
        const auto voice = input.getSample(0, i);
        const auto time = static_cast<double>(i) / sampleRate;
        // A deterministic, harmonically rich 110 Hz carrier gives the speech
        // envelope multiple partials to shape, without relying on a MIDI device.
        const auto phase = 2.0 * juce::MathConstants<double>::pi * 110.0 * time;
        const auto carrier = 0.13f * static_cast<float>(
            std::sin(phase) + 0.5 * std::sin(2.0 * phase)
            + 0.333333 * std::sin(3.0 * phase) + 0.25 * std::sin(4.0 * phase)
            + 0.2 * std::sin(5.0 * phase));
        const auto output = engine.processSample(voice, carrier, true, carrier);
        const auto high = highPass.processSample(output);
        processed.setSample(0, i, output);

        metrics.finite = metrics.finite && std::isfinite(output) && std::isfinite(high);
        metrics.outputPeak = juce::jmax(metrics.outputPeak, std::abs(output));
        inputEnergy += static_cast<double>(voice) * voice;
        outputEnergy += static_cast<double>(output) * output;
        highBandEnergy += static_cast<double>(high) * high;
        frameEnergy += static_cast<double>(voice) * voice;
        ++frameSamples;

        if (frameSamples == frameLength || i == sampleCount - 1)
        {
            const auto frameRms = std::sqrt(frameEnergy / static_cast<double>(frameSamples));
            maximumFrameRms = juce::jmax(maximumFrameRms, frameRms);
            ++totalFrames;
            frameEnergy = 0.0;
            frameSamples = 0;
        }
    }

    // A second pass counts active speech frames against the file's own maximum,
    // making the activity statistic independent of recording gain.
    frameEnergy = 0.0;
    frameSamples = 0;
    for (int i = 0; i < sampleCount; ++i)
    {
        const auto value = static_cast<double>(input.getSample(0, i));
        frameEnergy += value * value;
        ++frameSamples;
        if (frameSamples == frameLength || i == sampleCount - 1)
        {
            const auto frameRms = std::sqrt(frameEnergy / static_cast<double>(frameSamples));
            if (maximumFrameRms > 1.0e-8 && frameRms >= maximumFrameRms * 0.10)
                ++activeFrames;
            frameEnergy = 0.0;
            frameSamples = 0;
        }
    }

    const auto divisor = static_cast<double>(juce::jmax(1, sampleCount));
    metrics.inputRms = std::sqrt(inputEnergy / divisor);
    metrics.outputRms = std::sqrt(outputEnergy / divisor);
    metrics.outputHighBandRms = std::sqrt(highBandEnergy / divisor);
    metrics.crestFactor = metrics.outputRms > 1.0e-12
        ? static_cast<double>(metrics.outputPeak) / metrics.outputRms : 0.0;
    metrics.activeFrameRatio = totalFrames > 0
        ? static_cast<double>(activeFrames) / static_cast<double>(totalFrames) : 0.0;

#ifdef PRISMAVOX_TEST_RENDER_DIR
    const auto renderDirectory = juce::File(PRISMAVOX_TEST_RENDER_DIR);
    if (renderDirectory.createDirectory().failed())
    {
        std::cerr << "Could not create STOI render directory: "
                  << renderDirectory.getFullPathName() << '\n';
        return false;
    }
    const auto renderFile = renderDirectory.getChildFile(
        filename.upToLastOccurrenceOf(".", false, false) + "_vocoded.wav");
    auto outputStream = std::unique_ptr<juce::FileOutputStream>(renderFile.createOutputStream());
    if (outputStream == nullptr)
    {
        std::cerr << "Could not create STOI render file: "
                  << renderFile.getFullPathName() << '\n';
        return false;
    }
    juce::WavAudioFormat wavFormat;
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(wavFormat.createWriterFor(
        outputStream.get(), sampleRate, 1, 24, {}, 0));
    if (writer == nullptr)
    {
        std::cerr << "Could not initialize STOI WAV writer for "
                  << renderFile.getFullPathName() << '\\n';
        return false;
    }
    outputStream.release();
    if (!writer->writeFromAudioSampleBuffer(processed, 0, sampleCount))
    {
        std::cerr << "Could not write STOI render: "
                  << renderFile.getFullPathName() << '\\n';
        return false;
    }
    writer.reset();
    std::cout << "STOI render written: " << renderFile.getFullPathName() << '\\n';
#endif

    const auto passed = metrics.finite && metrics.inputRms > 1.0e-4
        && metrics.outputRms > 1.0e-5 && metrics.outputHighBandRms > 1.0e-7
        && metrics.outputPeak < 2.0f && metrics.activeFrameRatio > 0.05;
    std::cout << "Speech file " << filename << ": input RMS=" << metrics.inputRms
              << ", wet RMS=" << metrics.outputRms
              << ", wet >2.5 kHz RMS=" << metrics.outputHighBandRms
              << ", wet peak=" << metrics.outputPeak
              << ", crest factor=" << metrics.crestFactor
              << ", active 20 ms input frames=" << metrics.activeFrameRatio
              << "; " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
#endif
}

bool testUnvoicedDetectorUsesBothCues()
{
    const auto measureNoisePath = [](bool broadband)
    {
        VocoderEngine withNoise;
        VocoderEngine withoutNoise;
        withNoise.prepare(48000.0);
        withoutNoise.prepare(48000.0);
        for (auto* engine : { &withNoise, &withoutNoise })
        {
            engine->setDryWet(1.0f);
            engine->setGateThreshold(0.0f);
            engine->setSibilanceAmount(2.0f);
            engine->setUnvoicedNoise(2.0f);
        }
        withoutNoise.setUnvoicedNoise(0.0f);

        std::uint32_t randomState = 0x13579bdfu;
        double differenceEnergy = 0.0;
        constexpr int sampleCount = 48000;
        for (int i = 0; i < sampleCount; ++i)
        {
            float voice = 0.0f;
            if (broadband)
            {
                randomState ^= randomState << 13;
                randomState ^= randomState >> 17;
                randomState ^= randomState << 5;
                voice = 0.12f * (static_cast<float>(randomState & 0x00ffffffu)
                               / 8388607.5f - 1.0f);
            }
            else
            {
                voice = 0.12f * std::sin(static_cast<float>(2.0 * pi * 220.0 * i / 48000.0));
            }
            const auto carrier = 0.5f * std::sin(static_cast<float>(
                2.0 * pi * 440.0 * i / 48000.0));
            const auto difference = withNoise.processSample(voice, carrier, true, carrier)
                                  - withoutNoise.processSample(voice, carrier, true, carrier);
            differenceEnergy += static_cast<double>(difference) * difference;
        }
        return std::sqrt(differenceEnergy / sampleCount);
    };

    const auto voicedNoise = measureNoisePath(false);
    const auto broadbandNoise = measureNoisePath(true);
    std::cout << "Unvoiced excitation delta: voiced=" << voicedNoise
              << "; broadband=" << broadbandNoise << std::endl;
    return broadbandNoise > 1.0e-5 && broadbandNoise > voicedNoise * 2.0;
}


bool testPlosiveBurstIsTransientOnly()
{
    const auto renderDifference = [](bool transientInput)
    {
        VocoderEngine enabled;
        VocoderEngine disabled;
        enabled.prepare(48000.0);
        disabled.prepare(48000.0);
        for (auto* engine : { &enabled, &disabled })
        {
            engine->setDryWet(1.0f);
            engine->setGateThreshold(0.0f);
            engine->setSibilanceAmount(0.0f);
            engine->setUnvoicedNoise(0.0f);
        }
        enabled.setPlosiveBurst(1.0f);
        disabled.setPlosiveBurst(0.0f);

        std::uint32_t randomState = 0x2468ace1u;
        double burstDifferenceEnergy = 0.0;
        double steadyDifferenceEnergy = 0.0;
        constexpr int sampleCount = 24000;
        for (int i = 0; i < sampleCount; ++i)
        {
            float voice = 0.0f;
            if (transientInput)
            {
                if (i >= 6000 && i < 6048)
                {
                    randomState ^= randomState << 13;
                    randomState ^= randomState >> 17;
                    randomState ^= randomState << 5;
                    voice = 0.35f * (static_cast<float>(randomState & 0x00ffffffu)
                                   / 8388607.5f - 1.0f);
                }
            }
            else
            {
                voice = 0.25f * std::sin(static_cast<float>(2.0 * pi * 220.0 * i / 48000.0));
            }
            const auto carrier = 0.5f * std::sin(static_cast<float>(
                2.0 * pi * 440.0 * i / 48000.0));
            const auto difference = enabled.processSample(voice, carrier, true, carrier)
                                  - disabled.processSample(voice, carrier, true, carrier);
            if (transientInput && i >= 6000 && i < 7200)
                burstDifferenceEnergy += static_cast<double>(difference) * difference;
            if (!transientInput && i >= 12000)
                steadyDifferenceEnergy += static_cast<double>(difference) * difference;
        }
        const auto burstRms = std::sqrt(burstDifferenceEnergy / 1200.0);
        const auto steadyRms = std::sqrt(steadyDifferenceEnergy / 12000.0);
        return std::pair<double, double> { burstRms, steadyRms };
    };

    const auto transient = renderDifference(true);
    const auto steady = renderDifference(false);
    std::cout << "Plosive path A/B: transient delta RMS=" << transient.first
              << "; steady-vowel delta RMS=" << steady.second << '\n';
    return transient.first > 1.0e-6 && steady.second < 1.0e-7;
}

bool testReferenceSpeechCorpus()
{
    const auto ash = testReferenceSpeechFile("vocoder_test_ash.wav");
    const auto nova = testReferenceSpeechFile("vocoder_test_nova.wav");
    return ash && nova;
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
    passed = testUnvoicedDetectorUsesBothCues() && passed;
    passed = testPlosiveBurstIsTransientOnly() && passed;
    passed = testReferenceSpeechCorpus() && passed;

    std::cout << (passed ? "All vocoder DSP tests passed.\n"
                         : "One or more vocoder DSP tests failed.\n");
    return passed ? 0 : 1;
}
