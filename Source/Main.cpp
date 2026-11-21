#include <JuceHeader.h>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment(lib, "dwmapi.lib")
#endif
#include "MainComponent.h"
#include "PrismaVoxArtwork.h"

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
            g.fillAll(juce::Colour(0xff050914));

            static const auto artwork = []
            {
                auto xml = juce::XmlDocument::parse(prismaVoxSplashSvg);
                return xml != nullptr ? juce::Drawable::createFromSVG(*xml)
                                      : std::unique_ptr<juce::Drawable> {};
            }();
            if (artwork != nullptr)
                artwork->drawWithin(g, bounds, juce::RectanglePlacement::stretchToFit, 1.0f);

            g.setColour(juce::Colour(0xffaeb8c6));
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawFittedText("Starting audio engine and loading instruments...",
                             getLocalBounds().withY(735).withHeight(22),
                             juce::Justification::centred, 1);

            const auto waveArea = juce::Rectangle<float>(
                getWidth() * 0.18f, 748.0f,
                getWidth() * 0.64f, 12.0f);
            g.setColour(juce::Colour(0xff343b48));
            g.drawRoundedRectangle(waveArea, 8.0f, 1.0f);

            const auto progressArea = juce::Rectangle<float>(
                waveArea.getX(), waveArea.getBottom() + 8.0f,
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
            applySystemTitleBarTheme();
            applyNativeApplicationIcon();
        }

        ~MainWindow() override
        {
#if JUCE_WINDOWS
            if (nativeIcon != nullptr) DestroyIcon(nativeIcon);
#endif
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        void applyNativeApplicationIcon()
        {
#if JUCE_WINDOWS
            if (getPeer() == nullptr) return;
            auto xml = juce::XmlDocument::parse(prismaVoxIconSvg);
            auto drawable = xml != nullptr ? juce::Drawable::createFromSVG(*xml) : std::unique_ptr<juce::Drawable> {};
            if (drawable == nullptr) return;
            constexpr int size = 128;
            juce::Image image(juce::Image::ARGB, size, size, true);
            juce::Graphics graphics(image);
            drawable->drawWithin(graphics, { 0.0f, 0.0f, 128.0f, 128.0f }, juce::RectanglePlacement::stretchToFit, 1.0f);
            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = size; info.bmiHeader.biHeight = -size;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP colourBitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (colourBitmap == nullptr || bits == nullptr) return;
            auto* dst = static_cast<unsigned char*>(bits);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    const auto c = image.getPixelAt(x, y);
                    const auto p = (y * size + x) * 4;
                    dst[p] = c.getBlue(); dst[p + 1] = c.getGreen();
                    dst[p + 2] = c.getRed(); dst[p + 3] = c.getAlpha();
                }
            HBITMAP maskBitmap = CreateBitmap(size, size, 1, 1, nullptr);
            ICONINFO ii {};
            ii.fIcon = TRUE; ii.hbmColor = colourBitmap; ii.hbmMask = maskBitmap;
            nativeIcon = CreateIconIndirect(&ii);
            DeleteObject(colourBitmap);
            if (maskBitmap != nullptr) DeleteObject(maskBitmap);
            if (nativeIcon != nullptr)
            {
                const auto hwnd = static_cast<HWND>(getPeer()->getNativeHandle());
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(nativeIcon));
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(nativeIcon));
            }
#endif
        }

        void applySystemTitleBarTheme()
        {
#if JUCE_WINDOWS
            if (getPeer() == nullptr)
                return;

            DWORD useLightTheme = 1;
            DWORD valueSize = sizeof(useLightTheme);
            const auto keyResult = RegGetValueW(
                HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                &useLightTheme, &valueSize);
            const BOOL useDarkTitleBar =
                keyResult == ERROR_SUCCESS && useLightTheme == 0;
            const auto hwnd = static_cast<HWND>(getPeer()->getNativeHandle());

            if (DwmSetWindowAttribute(hwnd, 20, &useDarkTitleBar,
                                      sizeof(useDarkTitleBar)) != S_OK)
                DwmSetWindowAttribute(hwnd, 19, &useDarkTitleBar,
                                      sizeof(useDarkTitleBar));
#endif
        }

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
#if JUCE_WINDOWS
        HICON nativeIcon = nullptr;
#endif
        std::unique_ptr<MainComponent> pendingMainComponent;
        bool mainComponentInstalled = false;
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(VocoderApplication)
