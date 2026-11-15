#include <JuceHeader.h>
#include "MainComponent.h"

namespace
{
    class StartupSplash final : public juce::Component,
                                private juce::Timer
    {
    public:
        StartupSplash()
        {
            startTimerHz(30);
        }

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            g.fillAll(juce::Colour(0xff171a20));

            g.setColour(juce::Colour(0xff2b303a));
            g.drawRoundedRectangle(bounds.reduced(12.0f), 10.0f, 1.0f);

            g.setColour(juce::Colour(0xfff0f2f5));
            g.setFont(juce::Font(juce::FontOptions(30.0f, juce::Font::bold)));
            g.drawFittedText("VOCODER", getLocalBounds().withY(getHeight() / 2 - 78)
                                .withHeight(42), juce::Justification::centred, 1);

            g.setColour(juce::Colour(0xff9ba5b4));
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawFittedText("Starting audio engine and loading instruments...",
                             getLocalBounds().withY(getHeight() / 2 - 24).withHeight(28),
                             juce::Justification::centred, 1);

            const auto waveArea = juce::Rectangle<float>(
                getWidth() * 0.18f, getHeight() * 0.5f + 20.0f,
                getWidth() * 0.64f, 76.0f);
            g.setColour(juce::Colour(0xff343b48));
            g.drawRoundedRectangle(waveArea, 8.0f, 1.0f);

            // Animated three-voice trace, shaped like a harmonized vocal:
            // a strong fundamental, formant-rich upper partials, and two
            // slightly detuned harmony voices moving together.
            constexpr int points = 180;
            const auto twoPi = juce::MathConstants<float>::twoPi;
            const auto pi = juce::MathConstants<float>::pi;
            const std::array<float, 3> voiceCycles { 6.2f, 7.8f, 9.3f };
            const std::array<float, 3> voiceOffsets { -17.0f, 0.0f, 17.0f };
            const std::array<juce::uint32, 3> voiceColours {
                0x887bc8ff, 0xff62b5ff, 0x887fe0c4
            };

            for (size_t voice = 0; voice < voiceCycles.size(); ++voice)
            {
                juce::Path wave;
                for (int i = 0; i <= points; ++i)
                {
                    const auto t = static_cast<float>(i) / static_cast<float>(points);
                    const auto x = waveArea.getX() + waveArea.getWidth() * t;
                    const auto phasePosition = t * voiceCycles[voice] * twoPi - phase
                        * (1.0f + 0.07f * static_cast<float>(voice));
                    const auto fundamental = std::sin(phasePosition);
                    const auto second = std::sin(phasePosition * 2.0f + 0.35f);
                    const auto third = std::sin(phasePosition * 3.0f + 0.8f);
                    const auto fourth = std::sin(phasePosition * 4.0f + 1.1f);
                    const auto fifth = std::sin(phasePosition * 5.0f + 1.6f);

                    // Broad, moving vocal-formant emphasis rather than a
                    // perfectly uniform electronic test-tone waveform.
                    const auto formantA = 0.68f + 0.32f
                        * std::sin(twoPi * (1.15f * t) - phase * 0.32f);
                    const auto formantB = 0.72f + 0.28f
                        * std::sin(twoPi * (2.1f * t) + phase * 0.21f);
                    const auto vocalShape = fundamental
                        + 0.42f * formantA * second
                        + 0.24f * formantB * third
                        + 0.12f * fourth + 0.07f * fifth;
                    const auto envelopeShape = juce::jmax(0.0f, std::sin(pi * t));
                    const auto envelope = 0.48f + 0.52f
                        * std::pow(envelopeShape, 0.42f);
                    const auto amplitudePulse = 0.86f + 0.14f
                        * std::sin(twoPi * (2.4f * t) - phase * 0.55f);
                    const auto y = waveArea.getCentreY() + voiceOffsets[voice]
                        - envelope * amplitudePulse * vocalShape
                            * (voice == 1 ? 10.5f : 8.0f);

                    if (i == 0)
                        wave.startNewSubPath(x, y);
                    else
                        wave.lineTo(x, y);
                }

                g.setColour(juce::Colour(voiceColours[voice]));
                g.strokePath(wave, juce::PathStrokeType(
                    voice == 1 ? 2.4f : 1.7f,
                    juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
            }
        }

    private:
        void timerCallback() override
        {
            phase = std::fmod(phase + 0.22f, juce::MathConstants<float>::twoPi);
            repaint();
        }

        float phase = 0.0f;
    };
}

class VocoderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Vocoder"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    class MainWindow final : public juce::DocumentWindow,
                             private juce::Timer
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(juce::ResizableWindow::backgroundColourId),
                             juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new StartupSplash(), true);
            setResizable(true, true);
            centreWithSize(900, 620);
            setVisible(true);

            // Let the splash paint, then construct the application component
            // asynchronously while keeping the animated splash visible.
            startTimer(250);
        }

        ~MainWindow() override
        {
            stopTimer();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            pendingMainComponent = std::make_unique<MainComponent>([this]
            {
                if (pendingMainComponent != nullptr)
                    setContentOwned(pendingMainComponent.release(), true);
            });
        }

        std::unique_ptr<MainComponent> pendingMainComponent;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VocoderApplication)
