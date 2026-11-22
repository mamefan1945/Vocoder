#include "SoundFontEngine.h"

namespace
{
    void writeSoundFontTrace(const juce::String& message)
    {
        static juce::CriticalSection traceLock;
        const juce::ScopedLock guard(traceLock);
        const auto file = juce::File::getSpecialLocation(
            juce::File::userDesktopDirectory).getChildFile("PrismaVox-startup.log");
        file.appendText(juce::Time::getCurrentTime().toISO8601(true)
            + " | SoundFontEngine | " + message + "\r\n");
    }
}

SoundFontEngine::~SoundFontEngine()
{
    const juce::ScopedLock guard(lock);
    if (synth != nullptr)
        delete_fluid_synth(synth);
}

bool SoundFontEngine::load(const juce::File& file, juce::String& error, double sampleRate)
{
    double loadSampleRate = 44100.0;
    {
        const juce::ScopedLock guard(lock);
        loadSampleRate = sampleRate > 0.0 ? sampleRate : currentSampleRate;
    }
    if (!file.existsAsFile())
    {
        error = "SoundFont file does not exist: " + file.getFullPathName();
        return false;
    }

    auto* settings = new_fluid_settings();
    if (settings == nullptr)
    {
        error = "FluidSynth could not create settings.";
        return false;
    }

    fluid_settings_setnum(settings, "synth.sample-rate", loadSampleRate);
    fluid_settings_setnum(settings, "synth.gain", 0.8);
    fluid_settings_setint(settings, "synth.polyphony", 128);
    fluid_settings_setint(settings, "synth.reverb.active", 0);
    fluid_settings_setint(settings, "synth.chorus.active", 0);

    auto* newSynth = new_fluid_synth(settings);
    delete_fluid_settings(settings);
    if (newSynth == nullptr)
    {
        error = "FluidSynth could not create the synthesizer.";
        return false;
    }

    const auto newSoundFontId = fluid_synth_sfload(newSynth,
        file.getFullPathName().toRawUTF8(), 1);
    if (newSoundFontId < 0)
    {
        delete_fluid_synth(newSynth);
        error = "FluidSynth could not load " + file.getFileName();
        return false;
    }

    std::vector<Preset> newPresets;
    if (auto* soundFont = fluid_synth_get_sfont_by_id(newSynth, newSoundFontId))
    {
        fluid_sfont_iteration_start(soundFont);
        while (auto* preset = fluid_sfont_iteration_next(soundFont))
        {
            Preset item;
            item.name = juce::String::fromUTF8(fluid_preset_get_name(preset));
            item.bank = fluid_preset_get_banknum(preset);
            item.program = fluid_preset_get_num(preset);
            newPresets.push_back(std::move(item));
        }
    }

    if (newPresets.empty())
    {
        delete_fluid_synth(newSynth);
        error = "The SoundFont loaded, but it contains no selectable instruments.";
        return false;
    }

    auto defaultPreset = 0;
    for (size_t i = 0; i < newPresets.size(); ++i)
        if (newPresets[i].bank == 0 && newPresets[i].program == 0)
        {
            defaultPreset = static_cast<int>(i);
            break;
        }

    const auto& initial = newPresets[static_cast<size_t>(defaultPreset)];
    for (int channel = 0; channel < 16; ++channel)
        fluid_synth_program_select(newSynth, channel, newSoundFontId,
                                   initial.bank, initial.program);

    fluid_synth_t* oldSynth = nullptr;
    {
        const juce::ScopedLock guard(lock);
        oldSynth = synth;
        synth = newSynth;
        soundFontId = newSoundFontId;
        presets = std::move(newPresets);
        selectedPreset = defaultPreset;
        loadedFileName = file.getFileName();
        loadedFile = file;
        currentSampleRate = loadSampleRate;
    }

    if (oldSynth != nullptr)
        delete_fluid_synth(oldSynth);

    error.clear();
    return true;
}

void SoundFontEngine::prepare(double sampleRate, int maximumBlockSize)
{
    writeSoundFontTrace("prepare entered; requested sample rate="
        + juce::String(sampleRate, 2) + "; block size=" + juce::String(maximumBlockSize));
    const juce::ScopedLock guard(lock);
    writeSoundFontTrace("prepare acquired engine lock");
    const auto requestedRate = juce::jmax(8000.0, sampleRate);
    writeSoundFontTrace("current sample rate=" + juce::String(currentSampleRate, 2)
        + "; requested rate=" + juce::String(requestedRate, 2)
        + "; synth=" + juce::String(synth != nullptr ? "present" : "null")
        + "; loaded file exists=" + juce::String(loadedFile.existsAsFile() ? "yes" : "no"));

    // FluidSynth 2.5 does not support changing a live synth's sample rate.
    // On a device-rate change, destroy the old synth before loading the bank
    // into its replacement. Avoid two concurrent synths loading the same SF2.
    if (synth != nullptr && std::abs(requestedRate - currentSampleRate) > 0.5
        && loadedFile.existsAsFile())
    {
        writeSoundFontTrace("sample-rate change requires sequential synth recreation");
        auto* settings = new_fluid_settings();
        if (settings != nullptr)
        {
            fluid_settings_setnum(settings, "synth.sample-rate", requestedRate);
            fluid_settings_setnum(settings, "synth.gain", 0.8);
            fluid_settings_setint(settings, "synth.polyphony", 128);
            fluid_settings_setint(settings, "synth.reverb.active", 0);
            fluid_settings_setint(settings, "synth.chorus.active", 0);
            auto* replacement = new_fluid_synth(settings);
            delete_fluid_settings(settings);
            if (replacement != nullptr)
            {
                auto* previous = synth;
                synth = nullptr;
                soundFontId = -1;
                delete_fluid_synth(previous);
                writeSoundFontTrace("Loading SoundFont after releasing previous synth");
                const auto replacementSoundFontId = fluid_synth_sfload(
                    replacement, loadedFile.getFullPathName().toRawUTF8(), 1);
                writeSoundFontTrace("replacement fluid_synth_sfload returned id="
                    + juce::String(replacementSoundFontId));
                if (replacementSoundFontId >= 0
                    && juce::isPositiveAndBelow(selectedPreset, static_cast<int>(presets.size())))
                {
                    const auto& preset = presets[static_cast<size_t>(selectedPreset)];
                    for (int channel = 0; channel < 16; ++channel)
                        fluid_synth_program_select(replacement, channel,
                            replacementSoundFontId, preset.bank, preset.program);
                    synth = replacement;
                    soundFontId = replacementSoundFontId;
                    currentSampleRate = requestedRate;
                    writeSoundFontTrace("Sequential synth recreation completed");
                }
                else
                {
                    delete_fluid_synth(replacement);
                    juce::Logger::writeToLog("FluidSynth could not reload the SoundFont at the requested sample rate.");
                }
            }
        }
    }
    else if (synth == nullptr)
    {
        currentSampleRate = requestedRate;
    }
    writeSoundFontTrace("Resizing stereo render buffer");
    stereoBuffer.setSize(2, juce::jmax(1, maximumBlockSize), false, false, true);
    writeSoundFontTrace("prepare completed");
}

void SoundFontEngine::selectPreset(int presetIndex)
{
    const juce::ScopedLock guard(lock);
    if (synth == nullptr || !juce::isPositiveAndBelow(presetIndex,
                                                       static_cast<int>(presets.size())))
        return;

    const auto& preset = presets[static_cast<size_t>(presetIndex)];
    for (int channel = 0; channel < 16; ++channel)
        fluid_synth_program_select(synth, channel, soundFontId,
                                   preset.bank, preset.program);
    selectedPreset = presetIndex;
}

int SoundFontEngine::getSelectedPresetIndex() const
{
    const juce::ScopedLock guard(lock);
    return selectedPreset;
}

void SoundFontEngine::handleMidiMessage(const juce::MidiMessage& message)
{
    if (synth == nullptr)
        return;

    const auto channel = juce::jlimit(0, 15, message.getChannel() - 1);
    if (message.isNoteOn())
        fluid_synth_noteon(synth, channel, message.getNoteNumber(),
                           juce::jlimit(1, 127, juce::roundToInt(message.getVelocity() * 127.0f)));
    else if (message.isNoteOff())
        fluid_synth_noteoff(synth, channel, message.getNoteNumber());
    else if (message.isController())
        fluid_synth_cc(synth, channel, message.getControllerNumber(),
                       message.getControllerValue());
    else if (message.isPitchWheel())
        fluid_synth_pitch_bend(synth, channel, message.getPitchWheelValue());
    else if (message.isProgramChange())
        fluid_synth_program_change(synth, channel, message.getProgramChangeNumber());
    else if (message.isChannelPressure())
        fluid_synth_channel_pressure(synth, channel, message.getChannelPressureValue());
    else if (message.isAftertouch())
        fluid_synth_key_pressure(synth, channel, message.getNoteNumber(),
                                 message.getAfterTouchValue());
}

void SoundFontEngine::renderSegment(int startSample, int numSamples)
{
    if (synth == nullptr || numSamples <= 0)
        return;

    auto* left = stereoBuffer.getWritePointer(0);
    auto* right = stereoBuffer.getWritePointer(1);
    fluid_synth_write_float(synth, numSamples, left, startSample, 1,
                            right, startSample, 1);
}

void SoundFontEngine::renderNextBlock(juce::AudioBuffer<float>& monoOutput,
                                      const juce::MidiBuffer& midiMessages)
{
    monoOutput.clear();
    const juce::ScopedLock guard(lock);
    if (synth == nullptr || stereoBuffer.getNumSamples() < monoOutput.getNumSamples())
        return;

    stereoBuffer.clear();
    int renderedThrough = 0;
    juce::MidiBuffer::Iterator iterator(midiMessages);
    juce::MidiMessage message;
    int eventPosition = 0;

    while (iterator.getNextEvent(message, eventPosition))
    {
        const auto position = juce::jlimit(renderedThrough,
            monoOutput.getNumSamples(), eventPosition);
        renderSegment(renderedThrough, position - renderedThrough);
        renderedThrough = position;
        handleMidiMessage(message);
    }

    renderSegment(renderedThrough, monoOutput.getNumSamples() - renderedThrough);

    auto* mono = monoOutput.getWritePointer(0);
    const auto* left = stereoBuffer.getReadPointer(0);
    const auto* right = stereoBuffer.getReadPointer(1);
    for (int i = 0; i < monoOutput.getNumSamples(); ++i)
        mono[i] = 0.5f * (left[i] + right[i]);
}
