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

            const auto centre = juce::Point<float>(getWidth() * 0.5f,
                                                    getHeight() * 0.5f + 42.0f);
            const auto radius = 15.0f;
            g.setColour(juce::Colour(0xff343b48));
            g.drawEllipse(centre.x - radius, centre.y - radius,
                          radius * 2.0f, radius * 2.0f, 3.0f);

            g.setColour(juce::Colour(0xff62b5ff));
            const auto startAngle = phase - 1.0f;
            const auto endAngle = phase + 1.0f;
            juce::Path arc;
            arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                              startAngle, endAngle, true);
            g.strokePath(arc, juce::PathStrokeType(3.0f,
                           juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        void timerCallback() override
        {
            phase = std::fmod(phase + 0.12f, juce::MathConstants<float>::twoPi);
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
