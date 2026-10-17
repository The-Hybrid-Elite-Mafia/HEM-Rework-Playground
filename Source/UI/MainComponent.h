#pragma once

#include "UI/AppContext.h"
#include "UI/PluginWindows.h"
#include "UI/WaveCache.h"
#include "UI/Widgets.h"
#include "UI/Pages/EditPage.h"
#include "UI/Pages/MixPage.h"
#include "UI/Pages/ForgePage.h"
#include "UI/Pages/MasterPage.h"

namespace pg::ui
{
/**
    The Rework Playground main window, laid out on Steven's main faceplate (3840 x 2160, always
    shown at 16:9): menu bar on the top chrome strip, song plate + 6 transport sockets + counter
    glass on the second row, EDIT / MIX / SONG FORGE / MASTER in the 4 tab slots, the page's side
    panel on the left, the page in the big grey window, status + master meter on the bottom strip.
*/
class MainComponent final : public juce::Component,
                            public juce::MenuBarModel,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    ForgePage* getForgePage() noexcept { return forgePage.get(); }   // previews / tests
    MainComponent (PlaygroundEngine&, juce::AudioDeviceManager&, juce::PropertiesFile&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // menus
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int itemId, int topLevelMenuIndex) override;

    // drag & drop of audio / session files anywhere on the window
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    /** Asks to save unsaved changes, then runs `then` (unless the user cancelled). */
    void checkSaveThen (std::function<void()> then);
    void openSession (const juce::File&);
    void showTab (int tab);
    int getTab() const noexcept { return currentTab; }

    /** 4K faceplate rectangle -> window rectangle. */
    juce::Rectangle<float> fp (float x, float y, float w, float h) const;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshAll();
    void importFiles (const juce::Array<juce::File>&);
    void newSession();
    void openDialog();
    bool saveTo (const juce::File&);
    void save (std::function<void (bool)> done = {});
    void saveAs (std::function<void (bool)> done = {});
    bool isDirty() const;
    void markSaved();
    void togglePlay();
    void toggleLoop();
    void showAudioSettings();
    void showShortcuts();
    void status (const juce::String&);
    juce::String songName() const;

    PlaygroundEngine& engine;
    juce::AudioDeviceManager& devices;
    juce::PropertiesFile& settings;
    PlaygroundLookAndFeel lnf;
    WaveCache waves;
    PluginWindows windows;
    AppContext ctx;

    juce::Image background;            // faceplate scaled to the window (rebuilt on resize)
    juce::Image logo;
    juce::MenuBarComponent menuBar { this };
    std::array<std::unique_ptr<TransportButton>, 6> transport;
    std::array<std::unique_ptr<juce::TextButton>, tabs::count> tabButtons;
    juce::Label bpmLabel;

    EditView editView;
    std::unique_ptr<TrackList> trackList;
    std::unique_ptr<Timeline> timeline;
    std::unique_ptr<MixPage> mixPage;
    std::unique_ptr<PluginLibrary> library;
    std::unique_ptr<ForgePage> forgePage;
    std::unique_ptr<MasterPage> masterPage;
    int currentTab = tabs::edit;

    juce::File sessionFile;
    juce::String savedState;           // session XML at the last save / open (dirty check)
    juce::RecentlyOpenedFilesList recent;
    juce::String statusText { "Ready. Drag your stems in, or File > Open Session." };
    juce::uint32 statusTime = 0;
    StripMeter masterMeter;
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (2).withThreadName ("Audio loader") };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    int loadingCount = 0;
    bool openingSession = false;
    bool dirtyCached = false;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace pg::ui
