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

            // Indeterminate startup activity bar: startup stages have very
            // different durations, so avoid presenting a misleading percentage.
            const auto progressArea = juce::Rectangle<float>(
                waveArea.getX(), waveArea.getBottom() + 16.0f,
                waveArea.getWidth(), 4.0f);
            g.setColour(juce::Colour(0xff303744));
            g.fillRoundedRectangle(progressArea, 2.0f);
            const auto segmentWidth = progressArea.getWidth() * 0.22f;
            const auto travel = progressArea.getWidth() - segmentWidth;
            const auto segmentX = progressArea.getX() + travel * progressPhase;
            g.setColour(juce::Colour(0xff62b5ff));
            g.fillRoundedRectangle({ segmentX, progressArea.getY(),
                                     segmentWidth, progressArea.getHeight() }, 2.0f);

            // A speech-shaped, harmonized waveform based on the synthesized
            // phrase "Vocoder. Harmonizer." The envelope preserves the
            // consonant bursts, changing vowel energy and short word gap.
            // 20x zoom-out: fewer carrier cycles, with enough variation to
            // read as a vocal phrase rather than a uniform band.
            const auto twoPi = juce::MathConstants<float>::twoPi;
            const std::array<float, 3> voiceCycles { 124.0f, 156.0f, 186.0f };
            const std::array<float, 3> voiceOffsets { -4.0f, 0.0f, 4.0f };
            const std::array<float, 3> voicePhase { 0.0f, 0.17f, -0.13f };
            const std::array<juce::uint32, 3> voiceColours {
                0x887bc8ff, 0xff62b5ff, 0x887fe0c4
            };

            // Relative RMS profile across "Vocoder" then "Harmonizer".
            // The low section near the middle is the deliberately short pause.
            constexpr std::array<float, 33> speechEnvelope {
                0.05f, 0.10f, 0.16f, 0.32f, 0.78f, 0.94f, 0.62f, 0.30f,
                0.16f, 0.10f, 0.28f, 0.48f, 0.34f, 0.18f, 0.38f, 0.52f,
                0.30f, 0.09f, 0.07f, 0.12f, 0.20f, 0.56f, 1.00f, 0.72f,
                0.46f, 0.32f, 0.20f, 0.52f, 0.88f, 0.60f, 0.38f, 0.26f,
                0.12f
            };

            const auto sampleEnvelope = [&speechEnvelope](float position)
            {
                const auto scaled = juce::jlimit(0.0f, 1.0f, position)
                    * static_cast<float>(speechEnvelope.size() - 1);
                const auto index = juce::jmin(static_cast<int>(scaled),
                    static_cast<int>(speechEnvelope.size()) - 2);
                const auto fraction = scaled - static_cast<float>(index);
                return juce::jmap(fraction, speechEnvelope[static_cast<size_t>(index)],
                    speechEnvelope[static_cast<size_t>(index + 1)]);
            };

            const auto columns = juce::jmax(1, juce::roundToInt(waveArea.getWidth()));
            for (size_t voice = 0; voice < voiceCycles.size(); ++voice)
            {
                juce::Path waveform;
                for (int i = 0; i <= columns; ++i)
                {
                    const auto t = static_cast<float>(i) / static_cast<float>(columns);
                    const auto x = waveArea.getX() + waveArea.getWidth() * t;
                    // Advancing the source position makes the complete vocal
                    // shape drift smoothly from right to left across the frame.
                    const auto sourceT = std::fmod(t + scrollPhase, 1.0f);
                    const auto carrierPhase = twoPi * voiceCycles[voice] * sourceT
                        - phase * (1.0f + 0.035f * static_cast<float>(voice))
                        + voicePhase[voice];
                    const auto formantMotion = 0.82f
                        + 0.12f * std::sin(twoPi * (2.0f * sourceT) - phase * 0.31f)
                        + 0.06f * std::sin(twoPi * (5.0f * sourceT) + phase * 0.23f);
                    const auto breathMotion = 0.90f
                        + 0.10f * std::sin(phase * 0.7f + sourceT * twoPi * 3.0f
                            + static_cast<float>(voice) * 0.4f);
                    const auto envelope = sampleEnvelope(sourceT) * formantMotion * breathMotion;
                    const auto harmonic = std::sin(carrierPhase)
                        + 0.20f * std::sin(carrierPhase * 2.01f + 0.4f)
                        + 0.08f * std::sin(carrierPhase * 3.97f - 0.2f);
                    const auto centre = waveArea.getCentreY() + voiceOffsets[voice];
                    const auto y = centre - harmonic * envelope * 22.0f;
                    if (i == 0)
                        waveform.startNewSubPath(x, y);
                    else
                        waveform.lineTo(x, y);
                }

                g.setColour(juce::Colour(voiceColours[voice]));
                g.strokePath(waveform, juce::PathStrokeType(
                    voice == 1 ? 1.8f : 1.3f));
            }
        }

    private:
        void timerCallback() override
        {
            phase = std::fmod(phase + 0.22f, juce::MathConstants<float>::twoPi);
            scrollPhase = std::fmod(scrollPhase + 0.0025f, 1.0f);
            progressPhase = std::fmod(progressPhase + 0.018f, 1.0f);
            repaint();
        }

        float phase = 0.0f;
        float scrollPhase = 0.0f;
        float progressPhase = 0.0f;
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
