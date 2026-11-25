#pragma once
#include <JuceHeader.h>

// Dark, hardware-inspired control surface for PrismaVox.
class ProAudioLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ProAudioLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool, bool) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int,
                      juce::ComboBox&) override;
};
