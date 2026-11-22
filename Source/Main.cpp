#include <JuceHeader.h>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment(lib, "dwmapi.lib")
#endif
#include "MainComponent.h"
#include "PrismaVoxSplash.h"

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
            static const auto artwork = juce::Drawable::createFromSVG(
                juce::XmlDocument::parse(prismaVoxSplashSvg));
            g.fillAll(juce::Colour(0xff050914));
            if (artwork != nullptr)
                artwork->drawWithin(g, bounds, juce::RectanglePlacement::stretchToFit, 1.0f);

            g.setColour(juce::Colour(0xffaeb8c6));
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawFittedText("Starting audio engine and loading instruments...",
                             getLocalBounds().withY(735).withHeight(22),
                             juce::Justification::centred, 1);

            const auto progressArea = juce::Rectangle<float>(
                getWidth() * 0.18f, 768.0f, getWidth() * 0.64f, 4.0f);
            g.setColour(juce::Colour(0xff303744));
            g.fillRoundedRectangle(progressArea, 2.0f);
            const auto segmentWidth = progressArea.getWidth() * 0.22f;
            const auto travel = progressArea.getWidth() - segmentWidth;
            const auto segmentX = progressArea.getX() + travel * progressPhase;
            g.setColour(juce::Colour(0xff62b5ff));
            g.fillRoundedRectangle({ segmentX, progressArea.getY(),
                                     segmentWidth, progressArea.getHeight() }, 2.0f);
        }

    private:
        void timerCallback() override
        {
            progressPhase = std::fmod(progressPhase + 0.018f, 1.0f);
            repaint();
        }

        std::function<void()> firstPaintCallback;
        bool firstPaintSent = false;
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
            const auto instance = GetModuleHandleW(nullptr);
            nativeIcon = static_cast<HICON>(LoadImageW(
                instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
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
            if (mainComponentInstalled)
                return;

            // MainComponent starts audio and SoundFont initialization on its
            // own worker thread. Install the usable UI immediately so a slow
            // or unavailable audio driver cannot leave the app on the splash.
            mainComponentInstalled = true;
            setContentOwned(new MainComponent(), true);
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
