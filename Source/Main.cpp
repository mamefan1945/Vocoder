#include <JuceHeader.h>
#include "MainComponent.h"
#include "PrismaVoxLogo.h"

namespace
{
    class StartupSplash final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit StartupSplash(std::function<void()> onFirstPaint)
            : firstPaintCallback(std::move(onFirstPaint))
        {
            startTimerHz(30);
        }

        void paint(juce::Graphics& g) override
        {
            if (!firstPaintSent)
            {
                firstPaintSent = true;
                auto callback = std::move(firstPaintCallback);
                if (callback)
                    juce::MessageManager::callAsync(std::move(callback));
            }
            const auto bounds = getLocalBounds().toFloat();
            g.fillAll(juce::Colour(0xff171a20));
            g.setColour(juce::Colour(0xff202936));
            g.fillRect(bounds);

            g.setColour(juce::Colour(0xff2b303a));
            g.drawRoundedRectangle(bounds.reduced(12.0f), 10.0f, 1.0f);

            static const auto logo = []
            {
                juce::MemoryOutputStream encoded;
                encoded.writeText(juce::String(prismaVoxLogoSvg), false, false, nullptr);
                auto xml = juce::XmlDocument::parse(encoded.toString());
                return xml != nullptr ? juce::Drawable::createFromSVG(*xml)
                                      : std::unique_ptr<juce::Drawable> {};
            }();
            if (logo != nullptr)
                logo->drawWithin(g, juce::Rectangle<float>(
                    getWidth() * 0.12f, 22.0f, getWidth() * 0.76f, 278.0f),
                    juce::RectanglePlacement::centred, 1.0f);

            g.setColour(juce::Colour(0xffaeb8c6));
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawFittedText("Starting audio engine and loading instruments...",
                             getLocalBounds().withY(430).withHeight(26),
                             juce::Justification::centred, 1);

            const auto waveArea = juce::Rectangle<float>(
                getWidth() * 0.18f, 326.0f,
                getWidth() * 0.64f, 82.0f);
            g.setColour(juce::Colour(0xff343b48));
            g.drawRoundedRectangle(waveArea, 8.0f, 1.0f);

            const auto progressArea = juce::Rectangle<float>(
                waveArea.getX(), waveArea.getBottom() + 64.0f,
                waveArea.getWidth(), 4.0f);
            g.setColour(juce::Colour(0xff303744));
            g.fillRoundedRectangle(progressArea, 2.0f);
            const auto segmentWidth = progressArea.getWidth() * 0.22f;
            const auto travel = progressArea.getWidth() - segmentWidth;
            const auto segmentX = progressArea.getX() + travel * progressPhase;
            g.setColour(juce::Colour(0xff62b5ff));
            g.fillRoundedRectangle({ segmentX, progressArea.getY(),
                                     segmentWidth, progressArea.getHeight() }, 2.0f);

            const auto twoPi = juce::MathConstants<float>::twoPi;
            const std::array<float, 3> voiceCycles { 124.0f, 156.0f, 186.0f };
            const std::array<float, 3> voiceOffsets { -4.0f, 0.0f, 4.0f };
            const std::array<float, 3> voicePhase { 0.0f, 0.17f, -0.13f };
            const std::array<juce::uint32, 3> voiceColours {
                0x887bc8ff, 0xff62b5ff, 0x887fe0c4
            };
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
                    if (i == 0) waveform.startNewSubPath(x, y);
                    else waveform.lineTo(x, y);
                }
                g.setColour(juce::Colour(voiceColours[voice]));
                g.strokePath(waveform, juce::PathStrokeType(voice == 1 ? 1.8f : 1.3f));
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

        std::function<void()> firstPaintCallback;
        bool firstPaintSent = false;
        float phase = 0.0f;
        float scrollPhase = 0.0f;
        float progressPhase = 0.0f;
    };
}

class VocoderApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "PrismaVox"; }
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
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel()
                                 .findColour(juce::ResizableWindow::backgroundColourId),
                             juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            juce::Component::SafePointer<MainWindow> safeThis(this);
            setContentOwned(new StartupSplash([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->startMainComponent();
            }), true);
            setResizable(true, true);
            setResizeLimits(1180, 700, 1920, 1200);
            centreWithSize(1380, 790);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        void startMainComponent()
        {
            if (pendingMainComponent != nullptr || mainComponentInstalled)
                return;
            pendingMainComponent = std::make_unique<MainComponent>([this]
            {
                if (pendingMainComponent != nullptr)
                {
                    mainComponentInstalled = true;
                    setContentOwned(pendingMainComponent.release(), true);
                }
            });
        }
        std::unique_ptr<MainComponent> pendingMainComponent;
        bool mainComponentInstalled = false;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VocoderApplication)
