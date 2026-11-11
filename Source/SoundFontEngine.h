#pragma once

#include <JuceHeader.h>
#include <fluidsynth.h>
#include <vector>

class SoundFontEngine
{
public:
    struct Preset
    {
        juce::String name;
        int bank = 0;
        int program = 0;
    };

    SoundFontEngine() = default;
    ~SoundFontEngine();

    bool load(const juce::File& file, juce::String& error);
    void prepare(double sampleRate, int maximumBlockSize);
    void renderNextBlock(juce::AudioBuffer<float>& monoOutput,
                         const juce::MidiBuffer& midiMessages);
    void selectPreset(int presetIndex);

    const std::vector<Preset>& getPresets() const noexcept { return presets; }
    juce::String getLoadedFileName() const { return loadedFileName; }
    bool isLoaded() const noexcept { return synth != nullptr; }

private:
    void renderSegment(int startSample, int numSamples);
    void handleMidiMessage(const juce::MidiMessage& message);

    mutable juce::CriticalSection lock;
    fluid_synth_t* synth = nullptr;
    int soundFontId = -1;
    std::vector<Preset> presets;
    int selectedPreset = 0;
    juce::String loadedFileName;
    double currentSampleRate = 44100.0;
    juce::AudioBuffer<float> stereoBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundFontEngine)
};
