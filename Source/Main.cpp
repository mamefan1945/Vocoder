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
                getWidth() * 0.64f, 54.0f);
            g.setColour(juce::Colour(0xff343b48));
            g.drawRoundedRectangle(waveArea, 8.0f, 1.0f);

            // Animated multi-partial waveform: a visible signal trace rather than
            // a spinner that can appear stuck when startup work takes longer.
            juce::Path wave;
            constexpr int points = 120;
            for (int i = 0; i <= points; ++i)
            {
                const auto x = waveArea.getX() + waveArea.getWidth()
                    * static_cast<float>(i) / static_cast<float>(points);
                const auto t = static_cast<float>(i) / static_cast<float>(points);
                const auto envelope = 0.35f + 0.65f
                    * std::pow(std::sin(juce::MathConstants<float>::pi * t), 0.55f);
                const auto y = waveArea.getCentreY()
                    - envelope * (12.0f * std::sin(t * 6.0f * juce::MathConstants<float>::twoPi - phase)
                    + 4.0f * std::sin(t * 13.0f * juce::MathConstants<float>::twoPi - phase * 1.7f));
                if (i == 0)
                    wave.startNewSubPath(x, y);
                else
                    wave.lineTo(x, y);
            }
            g.setColour(juce::Colour(0xff62b5ff));
            g.strokePath(wave, juce::PathStrokeType(2.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

            // Let the splash paint at least once before constructing MainComponent.
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
            setContentOwned(new MainComponent(), true);
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VocoderApplication)
