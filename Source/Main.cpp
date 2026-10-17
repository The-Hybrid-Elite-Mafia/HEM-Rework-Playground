// HEM Rework Playground - application entry.

#include "UI/MainComponent.h"
#include "UI/Editors/EliteEditor.h"
#include "Session/SessionFile.h"

namespace pg
{
class PlaygroundApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "HEM Rework Playground"; }
    const juce::String getApplicationVersion() override { return HEM_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Rework Playground";
        o.folderName = "Hybrid Elite Mafia";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);

        ui::EliteEditor::install();
        engine.newSession();

        auto saved = settings->getXmlValue ("audioDevice");
        const auto err = devices.initialise (0, 2, saved.get(), true);
        player.setProcessor (&engine);
        devices.addAudioCallback (&player);

        window = std::make_unique<MainWindow> (engine, devices, *settings);
        if (err.isNotEmpty())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Sound card").withMessage ("The audio device could not start: " + err
                                              + "\n\nOpen File > Audio Settings to pick one.").withButton ("OK"), nullptr);
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        window.reset();
        devices.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        devices.closeAudioDevice();
        if (settings != nullptr) settings->saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr) { quit(); return; }
        window->content->checkSaveThen ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (window != nullptr) window->toFront (true);
        openFromCommandLine (commandLine);
    }

private:
    void openFromCommandLine (const juce::String& commandLine)
    {
        const auto arg = commandLine.trim().unquoted();
        if (arg.isEmpty() || window == nullptr) return;
        const juce::File f (arg);
        if (f.existsAsFile() && f.hasFileExtension (SessionFile::kExtension))
            window->content->checkSaveThen ([this, f] { if (window != nullptr) window->content->openSession (f); });
    }

    struct MainWindow final : juce::DocumentWindow
    {
        MainWindow (PlaygroundEngine& e, juce::AudioDeviceManager& d, juce::PropertiesFile& s)
            : DocumentWindow ("HEM Rework Playground", ui::colours::navy, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new ui::MainComponent (e, d, s);
            setContentOwned (content, true);
            setResizable (true, false);
            constrainer.setFixedAspectRatio (16.0 / 9.0);
            constrainer.setMinimumSize (1120, 630);
            setConstrainer (&constrainer);
            if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = disp->userArea;
                int w = juce::jmin (1600, area.getWidth() * 92 / 100);
                int h = w * 9 / 16;
                if (h > area.getHeight() * 92 / 100) { h = area.getHeight() * 92 / 100; w = h * 16 / 9; }
                centreWithSize (w, h);
            }
            else
                centreWithSize (1600, 900);
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        ui::MainComponent* content = nullptr;
        juce::ComponentBoundsConstrainer constrainer;
    };

    std::unique_ptr<juce::PropertiesFile> settings;
    PlaygroundEngine engine;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<MainWindow> window;
};
} // namespace pg

START_JUCE_APPLICATION (pg::PlaygroundApp)
