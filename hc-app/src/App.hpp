#pragma once
// =============================================================================
//  HEM Rework Playground on The Hybrid Core: the main window (faceplate, menu bar, transport,
//  EDIT / MIX / SONG FORGE / MASTER), what every page shares, and the main loop.
//  UI thread only, except the engine's audio callback (AudioHost) and background jobs (Worker).
// =============================================================================
#include "AudioHost.hpp"
#include "Settings.hpp"
#include "WaveCache.hpp"
#include "gui/Dialog.hpp"
#include "gui/Ui.hpp"

#include "PlaygroundEngine.hpp"
#include "session/UndoHistory.hpp"

#include "hybrid/ui/FontAtlas.hpp"
#include "hybrid/ui/GlWindow.hpp"
#include "hybrid/ui/Parts.hpp"
#include "hybrid/ui/Renderer.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hem::app
{
namespace fs = std::filesystem;
using E = pg::PlaygroundEngine;

namespace tabs { enum { edit = 0, mix, forge, master, count }; }

class PluginWindows;
class EditPage;
class MixPage;
class MasterPage;
class ForgePage;

struct StartOptions
{
    fs::path session;            // a .hemplay given on the command line
    fs::path screenshot;         // write a PNG of the window after `exitAfter` seconds (tests, CI)
    double exitAfter = 0.0;      // > 0: quit after this many seconds (tests)
    int startTab = -1;
    bool offline = false;        // no sound card (CI)
    std::vector<fs::path> stems; // load these on start (CI screenshots)
    std::string pluginWindow;    // open this plug-in's window on the selected strip (CI screenshots)
    int width = 1600, height = 900;
};

/** A stereo peak meter that falls back smoothly (0.10.0's StripMeter). */
struct MeterState
{
    float levL = -100.0f, levR = -100.0f, holdL = -100.0f, holdR = -100.0f;
    bool clipped = false;
    void update (float l, float r);
};
void drawMeter (Paint&, Rect r, const MeterState&, bool horizontal);

class App
{
public:
    App();
    ~App();

    /** Creates the window, loads the art, starts the sound card. False + `error` if it cannot. */
    bool start (const StartOptions&, std::string& error);
    /** The main loop. Returns the process exit code. */
    int run();

    //============================================================================== shared with the pages
    pg::PlaygroundEngine engine;
    Settings settings;
    WaveCache waves;
    AudioHost audio { engine };
    std::unique_ptr<PluginWindows> windows;
    int selectedStrip = E::kMaster;
    fs::path assetDir;

    Ui& ui() noexcept { return *mainUi; }
    const hc::ui::PartsAtlas& partsAtlas() const noexcept { return parts; }
    const hc::ui::FontSet& fontSet() const noexcept { return *fonts; }
    void* hwnd() const noexcept;
    void status (const std::string&);
    /** Tracks / inserts / names changed: waveforms are pruned, the unsaved star is checked soon. */
    void structureChanged();
    void importFiles (std::vector<fs::path>);
    void showTab (int);
    int tab() const noexcept { return currentTab; }
    bool isLoading() const noexcept { return loadingCount > 0 || openingSession; }

    pg::Plugin* insertPlugin (int strip, int slot, std::string_view id);
    void removePlugin (int strip, int slot);
    void movePlugin (int strip, int from, int to);
    void openPlugin (int strip, int slot);
    void removeTrack (int track);
    int firstFreeSlot (int strip) const;
    void addPluginToStrip (int strip, std::string_view id);
    void showInsertMenu (int strip, int slot, float x, float y);
    void togglePlay();
    void toggleLoop();
    void startExport (bool mp3);

    static fs::path songsFolder();
    std::string songName() const;

    // mixer controls (0..1 position / real value)
    float mixNorm (const std::string& id) const;
    void setMixNorm (const std::string& id, float norm);
    float mixValue (const std::string& id) const;
    void setMixValue (const std::string& id, float value);
    std::string mixText (const std::string& id) const;

    /** Asks to save unsaved changes, then runs `then` (unless cancelled). */
    void checkSaveThen (std::function<void()> then);
    void openSession (const fs::path&);

    MeterState& stripMeter (int strip) { return meters[(std::size_t) strip]; }

private:
    void frame();
    void handleEvents();
    void drawChrome();
    void drawMenuBar (Rect bar);
    std::vector<MenuItem> menuFor (int index);
    void menuPicked (int id);
    void drawTransport();
    void drawCounter();
    void drawTabs();
    void drawStatusBar();
    void handleKeys();
    void handleDrops();
    void refreshTitle();
    void newSession();
    void openDialog();
    bool saveTo (const fs::path&);
    void save (std::function<void (bool)> done = {});
    void saveAs (std::function<void (bool)> done = {});
    bool isDirty() const;
    void markSaved();
    void undoRedo (bool redo);
    void showAudioSettings();
    void showShortcuts();
    void showAbout();
    void askTempo();
    void openDevice();
    void addRecent (const fs::path&);
    std::vector<fs::path> recentFiles() const;
    void quit();
    bool loadArt (std::string& error);
    bool resizeTargets (std::string& error);
    void drawStatic();
    Rect fp (float x, float y, float w, float h) const;   // faceplate rectangle (design px) -> L, following the stretch band

    StartOptions options;
    hc::ui::GlWindow window;
    hc::ui::Renderer renderer;
    hc::ui::FontAtlas font;
    std::optional<hc::ui::FontSet> fonts;
    hc::ui::PartsAtlas parts;
    std::optional<hc::ui::FaceplateArt> art;
    hc::ui::Canvas canvas;
    std::uint32_t chassisWidth = 0;
    float fontScale = 0.0f;
    bool haveCanvas = false, staticDirty = true, hasLogo = false;
    std::unique_ptr<Paint> paint;
    std::unique_ptr<TextFields> fields;
    std::unique_ptr<Ui> mainUi;
    Input input;
    double startTime = 0.0;

    std::unique_ptr<EditPage> editPage;
    std::unique_ptr<MixPage> mixPage;
    std::unique_ptr<ForgePage> forgePage;
    std::unique_ptr<MasterPage> masterPage;
    int currentTab = tabs::edit;

    std::array<MeterState, E::kNumStrips> meters {};
    fs::path sessionFile;
    std::string savedState;
    bool dirtyCached = false;
    std::string statusText = "Ready. Drag your stems in, or File > Open Session.";
    double statusTime = 0.0;
    bool lastPlaying = false;
    int loadingCount = 0;
    bool openingSession = false;
    bool quitRequested = false, quitting = false;
    std::unique_ptr<pg::UndoHistory> history;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::unique_ptr<Worker> loader;
    int tick = 0;
    int openMenuIndex = -1;
    bool shotTaken = false;
};
} // namespace hem::app
