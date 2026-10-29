#include "App.hpp"
#include "PluginWindows.hpp"
#include "pages/EditPage.hpp"
#include "pages/ForgePage.hpp"
#include "pages/LibraryPanel.hpp"
#include "pages/MasterPage.hpp"
#include "pages/MixPage.hpp"
#include "pages/Strips.hpp"
#include "platform/Platform.hpp"

#include "Presets.hpp"
#include "Registry.hpp"
#include "hem/pg/Files.hpp"
#include "hem/pg/MessageQueue.hpp"
#include "media/MediaLoader.hpp"
#include "session/SessionFile.hpp"

#include "hybrid/ui/Faceplate.hpp"
#include "hybrid/ui/Image.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>

#ifndef HEM_PLAYGROUND_VERSION
 #define HEM_PLAYGROUND_VERSION "0.11.1"
#endif

namespace hem::app
{
namespace
{
    constexpr std::uint32_t kVkReturn = 0x0D, kVkSpace = 0x20, kVkEscape = 0x1B;

    enum MenuIds
    {
        mNew = 1, mOpen, mSave, mSaveAs, mImport, mExport, mAudio, mQuit, mClearRecent, mExportMp3,
        mRename = 20, mRemoveTrack, mClearSolo, mClearMute, mResetFader,
        mUndo = 30, mRedo,
        mTabEdit = 40, mTabMix, mTabForge, mTabMaster, mZoomIn, mZoomOut, mFit, mCloseWindows,
        mPresetsFolder = 80, mSongsFolder,
        mShortcuts = 90, mAbout,
        mRecentBase = 100,
        mPluginBase = 300
    };

    double steadySeconds()
    {
        using namespace std::chrono;
        return duration<double> (steady_clock::now().time_since_epoch()).count();
    }

    std::string timeText (double s)
    {
        const int total = (int) s;
        char b[32];
        std::snprintf (b, sizeof b, "%02d:%02d.%d", total / 60, total % 60, (int) (std::fmod (std::max (0.0, s), 1.0) * 10.0));
        return b;
    }

    /** Session text for the "unsaved changes" star: values rounded to 4 decimals, play position left out
        (a value that went through a knob or an undo can come back 0.000001 off, which is not a change). */
    void roundValues (pg::Tree& node)
    {
        if (node.hasProperty ("value") && node.hasProperty ("id"))
        {
            const double v = node.getDouble ("value");
            node.set ("value", std::round (v * 1.0e4) / 1.0e4);
        }
        for (auto& c : node.children()) roundValues (c);
    }

    std::string comparable (pg::Tree t)
    {
        t.remove ("position");
        roundValues (t);
        return t.toXml();
    }

    const char* kTabNames[tabs::count] { "EDIT", "MIX", "SONG FORGE", "MASTER" };
    constexpr float kTabX[4] { 108.0f, 1024.0f, 1940.0f, 2857.0f };
}

//==============================================================================
void MeterState::update (float l, float r)
{
    auto fall = [] (float& shown, float target)
    {
        const float db = pg::params::gainToDb (target);
        shown = db > shown ? db : std::max (db, shown - 1.6f * 0.5f);   // the old fall per 30 Hz tick, at 60 Hz
    };
    fall (levL, l);
    fall (levR, r);
    holdL = std::max (holdL - 0.15f, levL);
    holdR = std::max (holdR - 0.15f, levR);
    if (l >= 1.0f || r >= 1.0f) clipped = true;
}

void drawMeter (Paint& p, Rect b, const MeterState& m, bool horizontal)
{
    auto frac = [] (float db) { return std::clamp ((db + 60.0f) / 66.0f, 0.0f, 1.0f); };   // -60 .. +6 dB
    const Rgba green = colours::hemGreen, red = colours::meterRed, amber = colours::amber;
    p.fillRound (b, 2.0f, colours::black);
    // the bar is drawn in three colour zones: green to -12 dB, amber to 0 dB, red above
    auto bar = [&] (Rect r, float db, float hold, bool vertical)
    {
        const float f = frac (db), z12 = frac (-12.0f), z0 = frac (0.0f);
        auto seg = [&] (float a0, float a1, Rgba c0, Rgba c1)
        {
            const float e = std::min (f, a1);
            if (e <= a0) return;
            if (vertical)
            {
                const Rect s { r.x, r.bottom() - r.h * e, r.w, r.h * (e - a0) };
                p.verticalGradient (s, mix (c0, c1, (e - a0) / (a1 - a0)), c0, true);
            }
            else
            {
                const Rect s { r.x + r.w * a0, r.y, r.w * (e - a0), r.h };
                p.horizontalGradient (s, c0, mix (c0, c1, (e - a0) / (a1 - a0)), true);
            }
        };
        seg (0.0f, z12, darker (green, 0.3f), green);
        seg (z12, z0, green, amber);
        seg (z0, 1.0f, amber, red);
        if (vertical)
        {
            p.fill ({ r.x, r.bottom() - r.h * frac (hold), r.w, 1.0f }, alpha (colours::white, 0.7f));
            p.fill ({ r.x, r.bottom() - r.h * z0, r.w, 1.0f }, alpha (colours::white, 0.25f));
        }
        else
        {
            p.fill ({ r.x + r.w * frac (hold), r.y, 1.0f, r.h }, alpha (colours::white, 0.7f));
            p.fill ({ r.x + r.w * z0, r.y, 1.0f, r.h }, alpha (colours::white, 0.25f));
        }
    };
    if (horizontal)
    {
        Rect r = b;
        const Rect led = takeRight (r, std::min (10.0f, r.w * 0.04f));
        p.fill (reduced (led, 1.0f, 2.0f), m.clipped ? red : argb (0xff2a1414), m.clipped);
        r = reduced (r, 2.0f);
        const float h = (r.h - 2.0f) * 0.5f;
        bar (withHeight (r, h), m.levL, m.holdL, false);
        bar (trimTop (r, h + 2.0f), m.levR, m.holdR, false);
        return;
    }
    Rect r = b;
    const Rect led = takeTop (r, std::min (6.0f, r.h * 0.05f));
    p.fill (reduced (led, 1.0f, 0.0f), m.clipped ? red : argb (0xff2a1414), m.clipped);
    takeTop (r, 1.0f);
    const float w = (r.w - 1.0f) * 0.5f;
    bar (withWidth (r, w), m.levL, m.holdL, true);
    bar (trimLeft (r, w + 1.0f), m.levR, m.holdR, true);
}

//==============================================================================
App::App() = default;

App::~App()
{
    alive->store (false);
    if (loader) loader->stop();
    audio.close();
    if (windows) windows->closeAll();
    window.makeCurrent();
    editPage.reset();
    mixPage.reset();
    forgePage.reset();
    masterPage.reset();
    windows.reset();
    mainUi.reset();
    fields.reset();
    renderer.shutdown();
    window.destroy();
}

void* App::hwnd() const noexcept { return window.nativeHandle(); }

fs::path App::songsFolder()
{
    const fs::path d = pg::files::playgroundDocuments();
    std::error_code ec;
    fs::create_directories (d, ec);
    return d;
}

std::string App::songName() const { return sessionFile.empty() ? std::string ("Untitled Song") : pg::files::toText (sessionFile.stem()); }

Rect App::fp (float x, float y, float w, float h) const { return fromDesign (canvas.plateRect ({ x, y, w, h })); }

float App::mixNorm (const std::string& id) const
{
    const auto* p = engine.mixer().find (id);
    return p != nullptr ? p->getNormalised() : 0.0f;
}

void App::setMixNorm (const std::string& id, float norm)
{
    if (auto* p = engine.mixer().find (id)) p->setNormalised (norm);
}

float App::mixValue (const std::string& id) const { return engine.getMixer (id); }
void App::setMixValue (const std::string& id, float value) { engine.setMixer (id, value); }

std::string App::mixText (const std::string& id) const
{
    const auto* p = engine.mixer().find (id);
    return p != nullptr ? p->spec().toText (p->get()) : std::string();
}

//==============================================================================
bool App::loadArt (std::string& error)
{
    art = hc::ui::loadFaceplateArt (hc::ui::resolveFaceplate (assetDir, pg::Presets::appDataFolder()), &error);
    if (! art) return false;
    fonts = hc::ui::FontSet::load (assetDir / "fonts", &error);
    if (! fonts) return false;
    parts = hc::ui::buildPartsAtlas (art->master);
    if (! renderer.setParts (parts, &error)) return false;
    // the HEM logo, cut round for the corner socket
    if (auto logo = hc::ui::loadImageFile (assetDir / "brand" / "HEM-Icon-1024.png"))
    {
        auto img = hc::ui::resizeSrgb (*logo, 256, 256);
        for (std::uint32_t y = 0; y < img.height; ++y)
            for (std::uint32_t x = 0; x < img.width; ++x)
            {
                const float dx = (float) x + 0.5f - 128.0f, dy = (float) y + 0.5f - 128.0f;
                const float a = std::clamp (122.0f - std::sqrt (dx * dx + dy * dy), 0.0f, 1.0f);
                auto* px = &img.rgba[((std::size_t) y * img.width + x) * 4];
                px[3] = (std::uint8_t) std::lround ((float) px[3] * a);
            }
        std::string err;
        hasLogo = renderer.setAtlas (hc::ui::buildMips (hc::ui::premultiply (img)), &err);
    }
    return true;
}

bool App::resizeTargets (std::string& error)
{
    int w = window.pixelWidth(), h = window.pixelHeight();
    // Test screenshots draw off-screen at the asked size (a CI desktop is smaller than 4K).
    if (! options.screenshot.empty() && options.exitAfter > 0.0 && options.width > 0 && options.height > 0)
    {
        w = options.width;
        h = options.height;
    }
    const auto c = hc::ui::Canvas::fit ((float) w, (float) h);
    if (haveCanvas && c == canvas) return true;
    if (! renderer.setTargetSize (w, h, &error)) return false;
    const std::uint32_t texW = hc::ui::chassisTextureWidth (c.scale);
    if (texW != chassisWidth)
    {
        if (! renderer.setChassis (hc::ui::bakeChassis (*art, texW), &error)) return false;
        chassisWidth = texW;
    }
    if (std::abs (c.scale - fontScale) > 0.005f * std::max (fontScale, 0.01f))
    {
        if (! font.bake (*fonts, c.scale, true)) { error = "could not bake the Poppins atlas"; return false; }
        if (! renderer.setFont (font, &error)) return false;
        fontScale = c.scale;
    }
    canvas = c;
    haveCanvas = true;
    staticDirty = true;
    return true;
}

void App::drawStatic()
{
    if (! staticDirty) return;
    renderer.beginStatic (canvas);
    using namespace hc::ui::plate;
    const float E = canvas.extraH;
    auto bands = [&] (float sx, float sw, float dx, float dw)
    {
        renderer.chassis ({ sx, 0, sw, kStretchY0 }, { dx, 0, dw, kStretchY0 });
        renderer.chassis ({ sx, kStretchY0, sw, kStretchY1 - kStretchY0 }, { dx, kStretchY0, dw, kStretchY1 - kStretchY0 + E });
        renderer.chassis ({ sx, kStretchY1, sw, kHeight - kStretchY1 }, { dx, kStretchY1 + E, dw, kHeight - kStretchY1 });
    };
    bands (0, kWidth, 0, kWidth);
    if (canvas.visibleLeft() < 0.0f)
    {
        bands (0, kEdgeColumn, canvas.visibleLeft(), -canvas.visibleLeft());
        bands (kWidth - kEdgeColumn, kEdgeColumn, kWidth, canvas.visibleRight() - kWidth);
    }
    renderer.endStatic();
    staticDirty = false;
}

//==============================================================================
bool App::start (const StartOptions& o, std::string& error)
{
    options = o;
    startTime = steadySeconds();
    settings.load();
    engine.newSession();
    history = std::make_unique<pg::UndoHistory> (engine);
    loader = std::make_unique<Worker> (2);

    if (! window.create (L"HEM Rework Playground", o.width, o.height, o.screenshot.empty() || o.exitAfter <= 0.0, &error)) return false;
    window.setCloseAsks (true);
    if (! renderer.init (8, &error)) return false;
    if (! loadArt (error)) return false;
    if (! resizeTargets (error)) return false;
    platform::loadPrivateFonts (assetDir / "fonts");
    fields = platform::makeTextFields (window.nativeHandle());
    paint = std::make_unique<Paint> (renderer);
    mainUi = std::make_unique<Ui> (*paint, fields.get());
    windows = std::make_unique<PluginWindows> (*this);
    editPage = std::make_unique<EditPage> (*this);
    mixPage = std::make_unique<MixPage> (*this);
    forgePage = std::make_unique<ForgePage> (*this);
    masterPage = std::make_unique<MasterPage> (*this);

    if (o.offline) audio.open ({ DeviceChoice::Kind::offline, L"offline", "No sound card (offline clock)", "Offline clock" });
    else openDevice();

    markSaved();
    masterPage->resetFileName (songName());
    showTab (o.startTab >= 0 ? o.startTab : tabs::edit);
    if (! o.pluginWindow.empty())   // CI screenshots: that plug-in's window (the master's own one if it is already there)
    {
        int slot = -1;
        for (int s = 0; s < E::kSlots; ++s)
            if (auto* p = engine.getPlugin (E::kMaster, s); p != nullptr && o.pluginWindow == p->getInfo().id) slot = s;
        if (slot < 0 && insertPlugin (E::kMaster, 0, o.pluginWindow) != nullptr) slot = 0;
        if (slot >= 0) openPlugin (E::kMaster, slot);
    }
    if (settings.importedCount() > 0) status ("Welcome to 0.11: your " + std::to_string (settings.importedCount()) + " settings from the old version came along.");
    if (! o.stems.empty()) importFiles (o.stems);
    if (! o.session.empty()) openSession (o.session);
    if (o.startTab >= 0) showTab (o.startTab);   // the import above jumps to EDIT; --tab wins
    return true;
}

void App::openDevice()
{
    const auto list = AudioHost::listDevices();
    const auto kind = DeviceChoice::kindFromKey (settings.get ("audioKind", "wasapi-shared"));
    const std::wstring id = platform::wide (settings.get ("audioId"));
    for (const auto& d : list)
        if (d.kind == kind && d.id == id)
        {
            if (audio.open (d)) return;
            break;
        }
    // the saved device is gone or would not start: Windows' default output, which always works
    if (! audio.open (list.front()))
        status ("No sound: " + audio.status().problem);
    else if (kind == DeviceChoice::Kind::asio && ! AudioHost::hasAsio())
        status ("This version has no ASIO - playing through WASAPI instead (File > Audio Settings to change)");
}

//==============================================================================
int App::run()
{
    while (! quitting)
    {
        if (! window.pump()) break;
        frame();
        windows->frame (steadySeconds() - startTime);
        if (quitRequested) quit();
        if (options.exitAfter > 0.0 && steadySeconds() - startTime >= options.exitAfter) break;
    }
    settings.saveIfNeeded();
    return 0;
}

void App::quit()
{
    quitRequested = false;
    checkSaveThen ([this]
    {
        quitting = true;
        window.requestClose();
    });
}

void App::handleEvents()
{
    input.pressed = input.released = input.doubleClick = input.rightPressed = false;
    input.wheel = 0.0f;
    input.keys.clear();
    input.dropped.clear();
    for (auto& e : window.takeEvents())
    {
        using T = hc::ui::InputEvent::Type;
        const float x = canvas.toDesignX (e.x) / kL, y = canvas.toDesignY (e.y) / kL;
        switch (e.type)
        {
            case T::MouseDown:
                input.down = true; input.pressed = true; input.doubleClick = e.doubleClick;
                input.pressX = input.x = x; input.pressY = input.y = y; input.ctrl = e.ctrl; input.shift = e.shift;
                break;
            case T::MouseUp:   input.down = false; input.released = true; input.x = x; input.y = y; break;
            case T::MouseMove: input.x = x; input.y = y; input.ctrl = e.ctrl; input.shift = e.shift; break;
            case T::Wheel:     input.x = x; input.y = y; input.wheel += e.wheel; input.ctrl = e.ctrl; input.shift = e.shift; break;
            case T::RightDown: input.rightPressed = true; input.rightX = x; input.rightY = y; break;
            case T::Key:       input.keys.push_back ({ e.key, e.ctrl, e.shift }); break;
            case T::Drop:      input.dropped = e.files; input.dropX = x; input.dropY = y; break;
            case T::OpenFile:
                if (! e.files.empty())
                {
                    const fs::path f = e.files.front();
                    platform::bringToFront (hwnd());
                    checkSaveThen ([this, f] { openSession (f); });
                }
                break;
            case T::Close: quitRequested = true; break;
            case T::Resize: break;
        }
    }
}

void App::frame()
{
    ++tick;
    pg::MessageQueue::instance().drain();
    audio.poll();
    handleEvents();
    window.makeCurrent();
    std::string err;
    if (! resizeTargets (err)) { status ("GPU error: " + err); return; }

    for (int s = 0; s < E::kNumStrips; ++s) meters[(std::size_t) s].update (engine.getPeak (s, 0), engine.getPeak (s, 1));
    if (tick % 15 == 0) engine.collectGarbage();
    if (tick % 12 == 0 && ! openingSession) history->poll();
    if (tick % 60 == 0)
    {
        const bool d = isDirty();
        if (d != dirtyCached || tick == 60) { dirtyCached = d; refreshTitle(); }
        settings.saveIfNeeded();
    }
    const double now = steadySeconds() - startTime;
    if (const bool p = engine.isPlaying(); p != lastPlaying)
    {
        lastPlaying = p;
        if (statusTime == 0.0) statusText = p ? "Playing" : "Stopped";
    }
    if (statusTime != 0.0 && now - statusTime > 12.0) { statusTime = 0.0; statusText = engine.isPlaying() ? "Playing" : "Ready"; }

    if (window.minimized()) { std::this_thread::sleep_for (std::chrono::milliseconds (30)); return; }

    drawStatic();
    renderer.beginFrame (canvas);
    auto& ui = *mainUi;
    ui.beginFrame (input, canvas, now);

    const Rect left = fp (92.0f, 708.0f, 596.0f, 1220.0f);
    const Rect main = fp (746.0f, 708.0f, 3004.0f, 1220.0f);
    switch (currentTab)
    {
        case tabs::edit:   editPage->drawLeft (ui, left);  editPage->drawMain (ui, main); break;
        case tabs::mix:    mixPage->drawLeft (ui, left);   mixPage->drawMain (ui, main); break;
        case tabs::forge:  forgePage->drawLeft (ui, left); forgePage->drawMain (ui, main); break;
        case tabs::master: masterPage->drawLeft (ui, left); masterPage->drawMain (ui, main); break;
        default: break;
    }
    if (currentTab != tabs::master) masterPage->hidden();
    if (isLoading())
    {
        paint->fill (main, alpha (colours::navy, 0.55f));
        paint->panelText (openingSession ? "OPENING SESSION..." : "LOADING " + std::to_string (loadingCount) + " FILE(S)...", main, 26.0f, HAlign::centre);
    }
    drawChrome();
    handleDrops();
    handleKeys();
    ui.endFrame();

    if (! options.screenshot.empty() && ! shotTaken && options.exitAfter > 0.0 && now >= std::max (1.0, options.exitAfter - 1.0))
    {
        renderer.endFrameToCapture();
        hc::ui::writePng (options.screenshot, renderer.capture());
        shotTaken = true;
        if (! options.pluginWindow.empty())
        {
            auto plugShot = options.screenshot;
            plugShot.replace_filename (pg::files::toText (options.screenshot.stem()) + "-plugin.png");
            windows->snapshot (plugShot);
        }
        return;
    }
    renderer.endFrame (0);
    window.swap();
}

//==============================================================================
void App::drawChrome()
{
    auto& ui = *mainUi;
    auto& p = *paint;
    drawMenuBar (fp (250.0f, 62.0f, 3450.0f, 125.0f));

    // HEM logo in the corner socket
    if (hasLogo)
    {
        const Rect r = fromDesign ({ 158.0f - 52.0f, 125.0f - 52.0f, 104.0f, 104.0f });
        renderer.atlas ({ 0, 0, 1, 1 }, toDesign (r));
    }

    // song plate: song name + details, engraved
    Rect plate = fp (165.0f, 272.0f, 980.0f, 145.0f);
    p.engraved (songName() + (dirtyCached ? " *" : ""), takeTop (plate, plate.h * 0.6f), 26.7f, HAlign::left, VAlign::bottom);
    char sr[32];
    std::snprintf (sr, sizeof sr, "%.1f", engine.getSampleRate() / 1000.0);
    p.engraved (std::to_string (engine.getNumUsedTracks()) + " TRACKS  -  " + sr + " kHz  -  HEM REWORK PLAYGROUND", plate, 14.2f);

    drawTransport();
    drawCounter();
    drawTabs();
    drawStatusBar();
    (void) ui;
}

void App::drawMenuBar (Rect bar)
{
    auto& ui = *mainUi;
    auto& p = *paint;
    const char* names[] { "File", "Edit", "View", "Track", "Plugins", "Help" };
    const float th = std::clamp (bar.h * 0.42f, 12.0f, 20.0f);
    float x = bar.x;
    for (int i = 0; i < 6; ++i)
    {
        const float w = p.textWidth (names[i], th) + bar.h;
        const Rect r { x, bar.y, w, bar.h };
        x += w;
        const bool open = ui.menuOpen() && openMenuIndex == i;
        // moving over another title while a menu is open switches to that menu
        if (ui.menuOpen() && openMenuIndex >= 0 && openMenuIndex != i && r.contains (ui.input().x, ui.input().y))
        {
            openMenuIndex = i;
            ui.openMenu (menuFor (i), r.x, r.bottom(), [this] (int id) { menuPicked (id); });
        }
        const auto pr = ui.behave (makeId ("menubar", (std::uint64_t) i), r);
        if (open || pr.hover) p.fillRound (reduced (r, 2.0f, bar.h * 0.14f), 5.0f, open ? alpha (colours::glow, 0.35f) : alpha (colours::ink, 0.12f));
        p.engraved (names[i], r, th, HAlign::centre);
        if (pr.held && ui.input().pressed)   // menus open on the press, like Windows' own
        {
            openMenuIndex = i;
            ui.openMenu (menuFor (i), r.x, r.bottom(), [this] (int id) { menuPicked (id); });
        }
    }
    if (! ui.menuOpen()) openMenuIndex = -1;
}

std::vector<MenuItem> App::menuFor (int index)
{
    std::vector<MenuItem> m;
    const bool trackSelected = E::isTrack (selectedStrip) && engine.isTrackUsed (selectedStrip);
    switch (index)
    {
        case 0:
        {
            m.push_back (MenuItem::item ("New Session        Ctrl+N", mNew));
            m.push_back (MenuItem::item ("Open Session...   Ctrl+O", mOpen));
            std::vector<MenuItem> rec;
            const auto recent = recentFiles();
            for (std::size_t i = 0; i < recent.size(); ++i) rec.push_back (MenuItem::item (pg::files::toText (recent[i]), mRecentBase + (int) i));
            if (! rec.empty()) { rec.push_back (MenuItem::sep()); rec.push_back (MenuItem::item ("Clear list", mClearRecent)); }
            m.push_back (MenuItem::submenu ("Open Recent", rec, ! rec.empty()));
            m.push_back (MenuItem::item ("Save        Ctrl+S", mSave));
            m.push_back (MenuItem::item ("Save As...  Ctrl+Shift+S", mSaveAs));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Import Audio (stems)...   Ctrl+I", mImport));
            m.push_back (MenuItem::item ("Export WAV...   Ctrl+E", mExport));
            m.push_back (MenuItem::item ("Export MP3...   Ctrl+Shift+E", mExportMp3));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Audio Settings (sound card)...", mAudio));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Quit", mQuit));
            break;
        }
        case 1:
            m.push_back (MenuItem::item ("Undo      Ctrl+Z", mUndo, history->canUndo()));
            m.push_back (MenuItem::item ("Redo      Ctrl+Y", mRedo, history->canRedo()));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Rename Selected Track...", mRename, trackSelected));
            m.push_back (MenuItem::item ("Remove Selected Track", mRemoveTrack, trackSelected));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Clear All Solos", mClearSolo));
            m.push_back (MenuItem::item ("Clear All Mutes", mClearMute));
            m.push_back (MenuItem::item ("Selected Fader to 0 dB", mResetFader));
            break;
        case 2:
            m.push_back (MenuItem::item ("Edit      Ctrl+1", mTabEdit, true, currentTab == tabs::edit));
            m.push_back (MenuItem::item ("Mix       Ctrl+2", mTabMix, true, currentTab == tabs::mix));
            m.push_back (MenuItem::item ("Song Forge   Ctrl+3", mTabForge, true, currentTab == tabs::forge));
            m.push_back (MenuItem::item ("Master    Ctrl+4", mTabMaster, true, currentTab == tabs::master));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Zoom In  (Ctrl + mouse wheel)", mZoomIn));
            m.push_back (MenuItem::item ("Zoom Out", mZoomOut));
            m.push_back (MenuItem::item ("Fit Whole Song", mFit));
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Close All Plugin Windows", mCloseWindows, windows->count() > 0));
            break;
        case 3:
            m.push_back (MenuItem::item ("Import Audio (stems)...", mImport));
            m.push_back (MenuItem::item ("Rename Selected Track...", mRename, trackSelected));
            m.push_back (MenuItem::item ("Remove Selected Track", mRemoveTrack, trackSelected));
            break;
        case 4:
        {
            m.push_back (MenuItem::headerItem ("Add to: " + engine.getStripName (selectedStrip)));
            const auto& all = pg::PluginRegistry::all();
            for (std::size_t i = 0; i < all.size(); ++i)
            {
                auto it = MenuItem::item (all[i]->name, mPluginBase + (int) i, engine.canHost (selectedStrip, all[i]->id));
                it.colour = argb (all[i]->accent);
                m.push_back (it);
            }
            m.push_back (MenuItem::sep());
            m.push_back (MenuItem::item ("Open My Presets Folder", mPresetsFolder));
            break;
        }
        case 5:
            m.push_back (MenuItem::item ("Keyboard Shortcuts", mShortcuts));
            m.push_back (MenuItem::item ("Open My Songs Folder", mSongsFolder));
            m.push_back (MenuItem::item ("About", mAbout));
            break;
        default: break;
    }
    return m;
}

void App::menuPicked (int id)
{
    openMenuIndex = -1;
    if (id >= mRecentBase && id < mRecentBase + 20)
    {
        const auto recent = recentFiles();
        const auto i = (std::size_t) (id - mRecentBase);
        if (i < recent.size()) { const fs::path f = recent[i]; checkSaveThen ([this, f] { openSession (f); }); }
        return;
    }
    if (id >= mPluginBase && id < mPluginBase + (int) pg::PluginRegistry::all().size())
    {
        addPluginToStrip (selectedStrip, pg::PluginRegistry::all()[(std::size_t) (id - mPluginBase)]->id);
        return;
    }
    switch (id)
    {
        case mNew: checkSaveThen ([this] { newSession(); }); break;
        case mOpen: checkSaveThen ([this] { openDialog(); }); break;
        case mClearRecent: settings.set ("recentSessions", std::string()); break;
        case mSave: save(); break;
        case mSaveAs: saveAs(); break;
        case mImport:
        {
            auto files = platform::openFiles (hwnd(), "Import audio (stems)", songsFolder(), { { "Audio files", pg::MediaLoader::wildcard() } }, true);
            if (! files.empty()) importFiles (std::move (files));
            break;
        }
        case mExport: showTab (tabs::master); masterPage->exportWithDialog (false); break;
        case mExportMp3: showTab (tabs::master); masterPage->exportWithDialog (true); break;
        case mAudio: showAudioSettings(); break;
        case mQuit: quitRequested = true; break;
        case mRename:
            showTab (tabs::edit);
            status ("Right-click the track name on the left, or double-click its name in the MIX tab");
            break;
        case mRemoveTrack: if (E::isTrack (selectedStrip)) removeTrack (selectedStrip); break;
        case mClearSolo: case mClearMute:
            for (int t = 0; t < E::kMaxTracks; ++t) setMixNorm (E::Ids::track (t, id == mClearSolo ? "solo" : "mute"), 0.0f);
            if (id == mClearMute) for (int b = 0; b < E::kNumBuses; ++b) setMixNorm (E::Ids::bus (b, "mute"), 0.0f);
            break;
        case mUndo: undoRedo (false); break;
        case mRedo: undoRedo (true); break;
        case mResetFader:
        {
            const auto vid = E::isTrack (selectedStrip) ? E::Ids::track (selectedStrip, "vol")
                           : (E::isBus (selectedStrip) ? E::Ids::bus (selectedStrip - E::kFirstBus, "vol") : std::string (E::Ids::masterVol));
            setMixValue (vid, 0.0f);
            break;
        }
        case mTabEdit: showTab (tabs::edit); break;
        case mTabMix: showTab (tabs::mix); break;
        case mTabForge: showTab (tabs::forge); break;
        case mTabMaster: showTab (tabs::master); break;
        case mZoomIn: showTab (tabs::edit); editPage->zoom (1.5, engine.getPositionSeconds()); break;
        case mZoomOut: showTab (tabs::edit); editPage->zoom (1.0 / 1.5, engine.getPositionSeconds()); break;
        case mFit: showTab (tabs::edit); editPage->fitToWidth(); break;
        case mCloseWindows: windows->closeAll(); break;
        case mPresetsFolder:
        {
            const fs::path f = pg::Presets::appDataFolder() / "Presets";
            std::error_code ec;
            fs::create_directories (f, ec);
            platform::reveal (f);
            break;
        }
        case mSongsFolder: platform::reveal (songsFolder()); break;
        case mShortcuts: showShortcuts(); break;
        case mAbout: showAbout(); break;
        default: break;
    }
}

//==============================================================================
void App::drawTransport()
{
    auto& ui = *mainUi;
    auto& p = *paint;
    using hc::ui::Sprite;
    const char* tips[6] { "Back to the start (Enter)", "Play / pause (Space)", "Stop and go back to the start",
                          "Loop on / off (L). Drag on the timeline's ruler to set the loop.",
                          "BEFORE / AFTER (B): hear the song with every plugin off, then back on", "Export a WAV (Ctrl+E)" };
    for (int i = 0; i < 6; ++i)
    {
        const auto& s = hc::ui::plate::kSockets[(std::size_t) i];
        const float R = 84.0f / kL;
        const float cx = s.cx / kL, cy = canvas.plateY (s.cy) / kL;
        const Rect r { cx - R, cy - R, R * 2, R * 2 };
        const auto pr = ui.behave (makeId ("transport", (std::uint64_t) i), r);
        const bool pressed = pr.held && r.contains (ui.input().x, ui.input().y);
        bool lit = false;
        Rgba col = colours::hemGreen;
        switch (i)
        {
            case 1: lit = engine.isPlaying(); break;
            case 3: lit = engine.isLooping(); col = colours::glow; break;
            case 4: lit = engine.isBeforeMode(); col = colours::amber; break;
            case 5: lit = masterPage->isExporting(); col = argb (0xffcfa63a); break;
            default: break;
        }
        const float push = pressed ? 0.8f : 0.0f;
        const float capR = 76.0f / kL, size = capR * 192.0f / 91.0f;
        p.sprite (Sprite::KnobShadow, { cx - capR * 1.1f + (pressed ? 0.8f : 2.5f), cy - capR * 1.1f + (pressed ? 1.2f : 4.0f), capR * 2.2f, capR * 2.2f },
                  { 1, 1, 1, pressed ? 0.6f : 0.9f });
        p.sprite (Sprite::CapDark, { cx - size / 2 + push, cy - size / 2 + push, size, size });
        if (lit)
        {
            const float gs = 79.0f / 0.86f * 2.0f / kL;
            p.sprite (Sprite::RingGlow, { cx - gs / 2, cy - gs / 2, gs, gs }, col, true, false);
        }
        if (pr.hover && ! lit) p.fillEllipse (centred (r, capR * 1.8f, capR * 1.8f), alpha (colours::white, 0.05f));
        const Rgba ic = lit ? col : argb (0xffd9dde3);
        const float is = 62.0f / kL, ix = cx + push, iy = cy - 5.0f / kL * 2.4f + push;
        const Rect ir { ix - is / 2, iy - is / 2, is, is };
        const char* caption = "";
        switch (i)
        {
            case 0: p.sprite (Sprite::IconPrev, ir, ic, false, lit); caption = "START"; break;
            case 1: p.sprite (engine.isPlaying() ? Sprite::IconPause : Sprite::IconPlay, ir, ic, false, lit); caption = engine.isPlaying() ? "PAUSE" : "PLAY"; break;
            case 2: p.sprite (Sprite::IconStop, ir, ic, false, lit); caption = "STOP"; break;
            case 3: p.sprite (Sprite::IconLoop, ir, ic, false, lit); caption = "LOOP"; break;
            case 4: p.text (engine.isBeforeMode() ? "BEFORE" : "AFTER", centred (r, R * 1.6f, R * 0.6f), 12.0f, true, ic, HAlign::centre, VAlign::middle,
                            lit ? TextLook::neon : TextLook::plain);
                    caption = "A / B"; break;
            case 5:
            {
                // export: an arrow down into a tray
                const float a = is * 0.42f;
                p.fill ({ ix - a * 0.14f, iy - a * 0.9f, a * 0.28f, a * 0.9f }, ic, lit);
                p.triangle (ix - a * 0.55f, iy - a * 0.05f, ix + a * 0.55f, iy - a * 0.05f, ix, iy + a * 0.55f, ic, lit);
                p.fill ({ ix - a * 0.85f, iy + a * 0.7f, a * 1.7f, a * 0.2f }, ic, lit);
                caption = "EXPORT";
                break;
            }
            default: break;
        }
        p.text (caption, { cx - R, cy + 30.0f / kL, R * 2, 14.0f }, 9.5f, true, lit ? col : argb (0xffe6eaef), HAlign::centre);
        ui.tooltip (r, tips[i]);
        if (! pr.clicked) continue;
        switch (i)
        {
            case 0: engine.setPositionSeconds (0.0); break;
            case 1: togglePlay(); break;
            case 2: engine.stop(); engine.setPositionSeconds (0.0); break;
            case 3: toggleLoop(); break;
            case 4: engine.setBeforeMode (! engine.isBeforeMode()); status (engine.isBeforeMode() ? "BEFORE: every plugin is OFF" : "AFTER: plugins back ON"); break;
            case 5: showTab (tabs::master); masterPage->startExport (false); break;
            default: break;
        }
    }
}

void App::drawCounter()
{
    auto& ui = *mainUi;
    auto& p = *paint;
    Rect glass = fp (2700.0f, 262.0f, 640.0f, 165.0f);
    const double pos = engine.getPositionSeconds();
    const double bpm = engine.getMixer (E::Ids::bpm);
    const double beats = pos * bpm / 60.0;
    p.text (timeText (pos), takeTop (glass, glass.h * 0.62f), 38.0f, true, brighter (colours::glow, 0.7f), HAlign::left, VAlign::middle, TextLook::neon);
    std::string bb = "BAR " + std::to_string ((int) (beats / 4.0) + 1) + "  BEAT " + std::to_string ((int) std::fmod (beats, 4.0) + 1);
    if (engine.isLooping()) bb += "   LOOP " + timeText (engine.getLoopStart()) + " - " + timeText (engine.getLoopEnd());
    p.text (bb, glass, 15.0f, true, colours::paperDim);

    const Rect bpmR = fp (3330.0f, 270.0f, 370.0f, 80.0f);
    char t[32];
    std::snprintf (t, sizeof t, "%.1f BPM", bpm);
    p.text (t, bpmR, 23.0f, true, brighter (colours::glow, 0.6f), HAlign::right, VAlign::middle, TextLook::neon);
    const auto pr = ui.behave (makeId ("bpm"), bpmR);
    if (pr.doubleClick) askTempo();
    ui.tooltip (bpmR, "Song tempo: double-click to type it (Elite Delay syncs to this)");
    if (engine.isBeforeMode())
        p.text ("BEFORE", fp (3330.0f, 355.0f, 370.0f, 60.0f), 15.0f, true, colours::amber, HAlign::right, VAlign::middle, TextLook::neon);
}

void App::askTempo()
{
    DialogSpec d;
    d.title = "Song tempo";
    d.message = "Beats per minute (40 to 240). Elite Delay's tempo sync follows it.";
    char t[32];
    std::snprintf (t, sizeof t, "%.1f", engine.getMixer (E::Ids::bpm));
    d.fields = { { "BPM", t } };
    d.buttons = { "OK", "CANCEL" };
    d.width = 420.0f;
    showDialog (*mainUi, d, [this] (int b, const std::vector<std::string>& v)
    {
        if (b != 0 || v.empty()) return;
        std::string digits;
        for (char c : v[0]) if ((c >= '0' && c <= '9') || c == '.') digits += c;
        const float bpmValue = digits.empty() ? 0.0f : (float) std::atof (digits.c_str());
        if (bpmValue >= 40.0f && bpmValue <= 240.0f) setMixValue (E::Ids::bpm, bpmValue);
        else status ("The tempo has to be between 40 and 240 BPM.");
    });
}

void App::drawTabs()
{
    auto& ui = *mainUi;
    for (int i = 0; i < tabs::count; ++i)
    {
        const Rect r = fp (kTabX[i], 503.0f, 873.0f, 175.0f);
        if (ui.button (makeId ("tab", (std::uint64_t) i), r, kTabNames[i], ButtonLook::tab, colours::glow, true, currentTab == i, 17.0f)) showTab (i);
    }
}

void App::drawStatusBar()
{
    auto& ui = *mainUi;
    auto& p = *paint;
    p.engraved (statusText, fp (110.0f, 1985.0f, 2950.0f, 100.0f), 16.7f);
    const Rect m = fp (3060.0f, 2000.0f, 650.0f, 70.0f);
    auto& mm = meters[(std::size_t) E::kMaster];
    if (ui.behave (makeId ("mastermeter"), m).clicked) mm.clipped = false;
    drawMeter (p, m, mm, true);
    ui.tooltip (m, "Master level (click to clear the red clip light)");
    // sound card line (driver, latency, CPU) under the meter
    const auto& st = audio.status();
    char line[200];
    if (st.running)
        std::snprintf (line, sizeof line, "%s  -  %.1f ms  -  CPU %.0f%%", st.driver.c_str(), st.latencyMs > 0.0 ? st.latencyMs : 1000.0 * st.blockFrames / std::max (1u, st.sampleRate),
                       st.loadAvg * 100.0f);
    else std::snprintf (line, sizeof line, "NO SOUND CARD  -  File > Audio Settings");
    p.text (line, fp (3060.0f, 1972.0f, 650.0f, 30.0f), 9.0f, true, st.running ? colours::ink : colours::meterRed, HAlign::right, VAlign::middle,
            TextLook::engraved);
}

//==============================================================================
void App::handleKeys()
{
    auto& ui = *mainUi;
    if (ui.menuOpen() || ui.overlayOpen()) return;
    if (ui.key (kVkSpace)) togglePlay();
    if (ui.key (kVkReturn)) engine.setPositionSeconds (0.0);
    if (ui.key ('L')) toggleLoop();
    if (ui.key ('B')) { engine.setBeforeMode (! engine.isBeforeMode()); status (engine.isBeforeMode() ? "BEFORE: every plugin is OFF" : "AFTER: plugins back ON"); }
    if (ui.key ('Z', true)) undoRedo (false);
    if (ui.key ('Y', true) || ui.key ('Z', true, true)) undoRedo (true);
    if (ui.key ('S', true)) save();
    if (ui.key ('S', true, true)) saveAs();
    if (ui.key ('O', true)) checkSaveThen ([this] { openDialog(); });
    if (ui.key ('N', true)) checkSaveThen ([this] { newSession(); });
    if (ui.key ('I', true)) menuPicked (mImport);
    if (ui.key ('E', true)) menuPicked (mExport);
    if (ui.key ('E', true, true)) menuPicked (mExportMp3);
    for (int i = 0; i < tabs::count; ++i) if (ui.key ((std::uint32_t) ('1' + i), true)) showTab (i);
    (void) kVkEscape;
}

void App::handleDrops()
{
    auto& ui = *mainUi;
    auto files = ui.takeDrop (ui.screen());
    if (files.empty()) return;
    for (auto& f : files)
        if (pg::files::toText (f.extension()) == pg::SessionFile::kExtension)
        {
            const fs::path sf = f;
            checkSaveThen ([this, sf] { openSession (sf); });
            return;
        }
    std::vector<fs::path> audioFiles;
    for (auto& f : files) if (pg::MediaLoader::isAudioFile (f)) audioFiles.push_back (f);
    std::sort (audioFiles.begin(), audioFiles.end());
    importFiles (std::move (audioFiles));
}

//==============================================================================
void App::status (const std::string& t)
{
    statusText = t;
    statusTime = steadySeconds() - startTime;
    if (statusTime == 0.0) statusTime = 1.0e-6;
}

void App::structureChanged()
{
    std::vector<const pg::SampleData*> inUse;
    for (int t = 0; t < E::kMaxTracks; ++t)
        if (auto m = engine.getTrackMedia (t)) { inUse.push_back (m.get()); waves.get (m); }
    waves.prune (inUse);
}

void App::showTab (int t)
{
    currentTab = std::clamp (t, 0, tabs::count - 1);
    if (fields) fields->unfocus();
}

void App::refreshTitle()
{
    window.setTitle (platform::wide ("HEM Rework Playground  -  " + songName() + (dirtyCached ? " *" : "")));
}

void App::togglePlay()
{
    if (engine.isPlaying()) engine.stop();
    else
    {
        if (! engine.isLooping() && engine.getPositionSeconds() >= engine.getSongLengthSeconds() - 0.05) engine.setPositionSeconds (0.0);
        engine.play();
    }
}

void App::toggleLoop()
{
    if (! engine.isLooping() && engine.getLoopEnd() - engine.getLoopStart() < 0.1)
        engine.setLoop (true, 0.0, std::max (1.0, engine.getSongLengthSeconds()));
    else
        engine.setLoop (! engine.isLooping(), engine.getLoopStart(), engine.getLoopEnd());
}

void App::startExport (bool mp3) { showTab (tabs::master); masterPage->startExport (mp3); }

//==============================================================================
pg::Plugin* App::insertPlugin (int strip, int slot, std::string_view id)
{
    if (! engine.canHost (strip, id))
    {
        const auto* info = pg::PluginRegistry::find (id);
        status (std::string (info != nullptr ? info->name : "That plugin") + " only goes on the MASTER (it has look-ahead).");
        return nullptr;
    }
    if (auto* old = engine.getPlugin (strip, slot)) windows->close (old);
    auto* p = engine.insertPlugin (strip, slot, id);
    structureChanged();
    return p;
}

void App::removePlugin (int strip, int slot)
{
    if (auto* p = engine.getPlugin (strip, slot)) windows->close (p);
    engine.removePlugin (strip, slot);
    structureChanged();
}

void App::movePlugin (int strip, int from, int to)
{
    if (to < 0 || to >= E::kSlots) return;
    engine.swapSlots (strip, from, to);
    structureChanged();
}

void App::openPlugin (int strip, int slot)
{
    if (auto* p = engine.getPlugin (strip, slot))
        windows->open (*p, engine.getStripName (strip) + "  (insert " + std::to_string (slot + 1) + ")");
}

void App::removeTrack (int track)
{
    for (int s = 0; s < E::kSlots; ++s) if (auto* p = engine.getPlugin (track, s)) windows->close (p);
    engine.removeTrack (track);
    if (selectedStrip == track) selectedStrip = E::kMaster;
    structureChanged();
}

int App::firstFreeSlot (int strip) const
{
    for (int s = 0; s < E::kSlots; ++s) if (engine.getPlugin (strip, s) == nullptr) return s;
    return -1;
}

void App::addPluginToStrip (int strip, std::string_view id)
{
    // Elite Master always goes last on the master; everything else in the first free slot
    const int slot = id == "elite-master" ? E::kSlots - 1 : firstFreeSlot (strip);
    if (slot < 0) { status (engine.getStripName (strip) + ": all 8 inserts are full"); return; }
    if (insertPlugin (strip, slot, id) != nullptr) openPlugin (strip, slot);
}

void App::showInsertMenu (int strip, int slot, float x, float y)
{
    if (slot < 0) { status (engine.getStripName (strip) + ": all 8 inserts are full"); return; }
    std::vector<MenuItem> m { MenuItem::headerItem (engine.getStripName (strip) + "  -  insert " + std::to_string (slot + 1)) };
    const auto& all = pg::PluginRegistry::all();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        auto it = MenuItem::item (std::string (all[i]->name) + "   -  " + all[i]->blurb, (int) i + 1, engine.canHost (strip, all[i]->id));
        it.colour = argb (all[i]->accent);
        m.push_back (it);
    }
    mainUi->openMenu (std::move (m), x, y, [this, strip, slot] (int r)
    {
        if (r <= 0) return;
        const auto* info = pg::PluginRegistry::all()[(std::size_t) r - 1];
        if (insertPlugin (strip, slot, info->id) != nullptr) openPlugin (strip, slot);
    });
}

//==============================================================================
void App::importFiles (std::vector<fs::path> files)
{
    if (files.empty()) return;
    const int free = E::kMaxTracks - engine.getNumUsedTracks() - loadingCount;
    if (free <= 0) { status ("All 32 tracks are in use: remove a track first."); return; }
    const int n = std::min (free, (int) files.size());
    if (n < (int) files.size()) status ("Only " + std::to_string (n) + " of " + std::to_string (files.size()) + " files fit (32 tracks max).");
    files.resize ((std::size_t) n);
    showTab (currentTab == tabs::forge ? tabs::forge : tabs::edit);
    // decode in parallel, but add the tracks in the order the files were given (01 Drums, 02 Bass...)
    struct Batch
    {
        std::vector<fs::path> files;
        std::vector<pg::SampleData::Ptr> media;
        std::vector<std::string> errors;
        std::vector<bool> done;
        std::size_t next = 0;
    };
    auto batch = std::make_shared<Batch>();
    batch->files = files;
    batch->media.resize (files.size());
    batch->errors.resize (files.size());
    batch->done.assign (files.size(), false);
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        ++loadingCount;
        auto flag = alive;
        const fs::path file = files[i];
        loader->add ([this, flag, batch, i, file]
        {
            std::string err;
            auto media = pg::MediaLoader::decode (file, err);
            Worker::toUi ([this, flag, batch, i, media, err]
            {
                if (! flag->load()) return;
                batch->media[i] = media;
                batch->errors[i] = err;
                batch->done[i] = true;
                while (batch->next < batch->files.size() && batch->done[batch->next])
                {
                    const std::size_t k = batch->next++;
                    --loadingCount;
                    auto m = batch->media[k];
                    if (m == nullptr) { status (batch->errors[k]); continue; }
                    const int t = engine.addTrack (m, pg::files::toText (batch->files[k].stem()));
                    if (t < 0) status ("All 32 tracks are in use.");
                    else
                    {
                        selectedStrip = t;
                        status ("Loaded " + pg::files::toText (batch->files[k].filename()) + "  (track " + std::to_string (t + 1) + ")");
                    }
                    batch->media[k].reset();
                }
                editPage->resetView();
                structureChanged();
            });
        });
    }
}

void App::undoRedo (bool redo)
{
    if (openingSession) return;
    history->beforeStructuralRestore = [this] { windows->closeAll(); };
    const bool done = redo ? history->redo() : history->undo();
    if (! done) { status (redo ? "Nothing to redo." : "Nothing to undo."); return; }
    if (history->lastRestoreWasStructural()) structureChanged();
    status (redo ? "Redo." : "Undo.");
}

bool App::isDirty() const { return comparable (engine.createSessionTree()) != savedState; }

void App::markSaved()
{
    savedState = comparable (engine.createSessionTree());
    dirtyCached = false;
    refreshTitle();
}

void App::checkSaveThen (std::function<void()> then)
{
    if (! isDirty()) { then(); return; }
    DialogSpec d;
    d.title = "Save changes?";
    d.message = "\"" + songName() + "\" has changes that are not saved.";
    d.buttons = { "SAVE", "DON'T SAVE", "CANCEL" };
    showDialog (*mainUi, d, [this, then] (int b, const std::vector<std::string>&)
    {
        if (b == 0) save ([then] (bool ok) { if (ok) then(); });
        else if (b == 1) then();
    });
}

void App::newSession()
{
    windows->closeAll();
    engine.stop();
    engine.newSession();
    sessionFile.clear();
    selectedStrip = E::kMaster;
    editPage->resetView();
    markSaved();
    history->reset();
    masterPage->resetFileName (songName());
    structureChanged();
    status ("New session: Elite Verb is on the Reverb bus, Elite Delay on the Delay bus, Elite Master on the master.");
}

void App::openDialog()
{
    auto files = platform::openFiles (hwnd(), "Open session", songsFolder(), { { "Rework Playground sessions", std::string ("*") + pg::SessionFile::kExtension } }, false);
    if (! files.empty()) openSession (files.front());
}

void App::openSession (const fs::path& file)
{
    std::string err;
    auto tree = pg::SessionFile::read (file, err);
    if (! tree) { status (err); return; }
    openingSession = true;
    const auto paths = E::mediaFilesIn (*tree);
    auto flag = alive;
    const pg::Tree t = *tree;
    loader->add ([this, flag, file, t, paths]
    {
        std::vector<std::string> failed;
        auto media = pg::MediaLoader::decodeAll (paths, failed, [flag] { return ! flag->load(); });
        Worker::toUi ([this, flag, file, t, media]
        {
            if (! flag->load()) return;
            openingSession = false;
            windows->closeAll();
            engine.stop();
            std::vector<std::string> missing;
            engine.applySessionTree (t, media, &missing);
            sessionFile = file;
            addRecent (file);
            selectedStrip = E::kMaster;
            editPage->resetView();
            markSaved();
            history->reset();
            masterPage->resetFileName (songName());
            structureChanged();
            const std::string name = pg::files::toText (file.filename());
            if (missing.empty()) status ("Opened " + name);
            else status ("Opened " + name + "  -  " + std::to_string (missing.size()) + " audio file(s) not found: "
                         + pg::files::toText (pg::files::toPath (missing[0]).filename()) + (missing.size() > 1 ? " ..." : ""));
        });
    });
}

bool App::saveTo (const fs::path& f)
{
    std::string err;
    fs::path target = f;
    target.replace_extension (pg::SessionFile::kExtension);
    if (! pg::SessionFile::save (engine, target, err)) { status ("Could not save: " + err); return false; }
    sessionFile = target;
    addRecent (sessionFile);
    markSaved();
    masterPage->setSongName (songName());
    status ("Saved " + pg::files::toText (sessionFile));
    return true;
}

void App::save (std::function<void (bool)> done)
{
    if (sessionFile.empty()) { saveAs (std::move (done)); return; }
    const bool ok = saveTo (sessionFile);
    if (done) done (ok);
}

void App::saveAs (std::function<void (bool)> done)
{
    const fs::path start = ! sessionFile.empty() ? sessionFile : songsFolder() / pg::files::toPath (songName() + pg::SessionFile::kExtension);
    auto f = platform::saveFile (hwnd(), "Save session as", start, { { "Rework Playground sessions", std::string ("*") + pg::SessionFile::kExtension } },
                                 std::string (pg::SessionFile::kExtension).substr (1));
    const bool ok = f.has_value() && saveTo (*f);
    if (done) done (ok);
}

std::vector<fs::path> App::recentFiles() const
{
    std::vector<fs::path> out;
    std::stringstream ss (settings.get ("recentSessions"));
    std::string line;
    while (std::getline (ss, line))
    {
        while (! line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (! line.empty()) out.push_back (pg::files::toPath (line));
    }
    return out;
}

void App::addRecent (const fs::path& f)
{
    auto list = recentFiles();
    list.erase (std::remove (list.begin(), list.end(), f), list.end());
    list.insert (list.begin(), f);
    if (list.size() > 10) list.resize (10);
    std::string s;
    for (auto& p : list) { if (! s.empty()) s += "\n"; s += pg::files::toText (p); }
    settings.set ("recentSessions", s);
}

//==============================================================================
void App::showAudioSettings()
{
    auto list = std::make_shared<std::vector<DeviceChoice>> (AudioHost::listDevices());
    showPanel (*mainUi, "Audio Settings", 620.0f, 470.0f, colours::glow, [this, list] (Ui& u, Rect body) -> bool
    {
        auto& p = u.p;
        const auto& st = audio.status();
        Rect r = body;
        p.text ("SOUND CARD", takeTop (r, 20.0f), 12.0f, true, colours::paperDim);
        Rect listR = takeTop (r, 250.0f);
        p.fillRound (listR, 4.0f, colours::well);
        p.pushClip (listR);
        for (std::size_t i = 0; i < list->size(); ++i)
        {
            const auto& d = (*list)[i];
            const Rect row { listR.x, listR.y + (float) i * 26.0f, listR.w, 26.0f };
            const bool current = d.kind == audio.current().kind && d.id == audio.current().id && st.running;
            const auto pr = u.behave (makeId ("audio.dev", i), row);
            if (current) p.fill (row, alpha (colours::glow, 0.35f));
            else if (pr.hover) p.fill (row, alpha (colours::white, 0.06f));
            p.text (d.label, reduced (row, 10.0f, 0.0f), 13.0f, current, colours::paper);
            if (pr.clicked)
            {
                if (audio.open (d))
                {
                    settings.set ("audioKind", d.kindKey());
                    settings.set ("audioId", platform::utf8 (d.id));
                    status ("Sound card: " + d.label);
                }
                else status ("Sound card: " + audio.status().problem);
            }
        }
        p.popClip();
        takeTop (r, 10.0f);
        if (st.running)
        {
            p.text ("NOW: " + st.driver + "  -  " + st.device, takeTop (r, 20.0f), 13.0f, true, colours::hemGreen);
            p.text (st.format, takeTop (r, 20.0f), 12.0f, false, colours::paper);
            char b[160];
            std::snprintf (b, sizeof b, "CPU %.1f%% average, %.1f%% peak  -  late blocks %u  -  driver dropouts %u", st.loadAvg * 100.0f, st.loadPeak * 100.0f,
                           st.overruns, st.driverDropouts);
            p.text (b, takeTop (r, 20.0f), 12.0f, false, colours::paperDim);
        }
        else p.wrapped (st.problem.empty() ? std::string ("No sound card is playing.") : st.problem, takeTop (r, 44.0f), 13.0f, false, colours::meterRed, 2);
        takeTop (r, 4.0f);
        p.wrapped ("WASAPI shared works with every output (HDMI, Bluetooth). WASAPI exclusive and ASIO give the lowest latency on an audio interface.",
                   takeTop (r, 36.0f), 11.5f, false, colours::paperDim, 2);
        const Rect buttons = takeBottom (r, 34.0f);
        const Rect refresh = withWidth (buttons, 150.0f);
        if (u.button (makeId ("audio.refresh"), refresh, "LIST AGAIN", ButtonLook::dark, colours::glow, true, false, 12.0f)) *list = AudioHost::listDevices();
        const Rect close { buttons.right() - 110.0f, buttons.y, 110.0f, buttons.h };
        return ! (u.button (makeId ("audio.close"), close, "CLOSE", ButtonLook::dark, colours::glow, true, true, 12.0f) || u.key (kVkEscape) || u.key (kVkReturn));
    });
}

void App::showShortcuts()
{
    DialogSpec d;
    d.title = "Keyboard shortcuts";
    d.message = "Space  -  play / pause\n"
                "Enter  -  back to the start\n"
                "L  -  loop on / off (drag on the ruler to set the loop)\n"
                "B  -  BEFORE / AFTER (all plugins off / on)\n"
                "Ctrl+1 / 2 / 3 / 4  -  Edit / Mix / Song Forge / Master\n"
                "Ctrl+S  -  save,  Ctrl+Shift+S  -  save as\n"
                "Ctrl+O  -  open,  Ctrl+N  -  new session\n"
                "Ctrl+I  -  import stems,  Ctrl+E  -  export WAV\n"
                "Ctrl + mouse wheel  -  zoom the timeline,  Shift + wheel  -  scroll sideways\n"
                "Double-click a knob or fader  -  back to its default";
    d.width = 600.0f;
    showDialog (*mainUi, d, {});
}

void App::showAbout()
{
    DialogSpec d;
    d.title = "HEM Rework Playground";
    d.message = std::string ("Version ") + HEM_PLAYGROUND_VERSION + "\n\nThe Hybrid Elite Mafia Rework Playground\n"
                "Stems in, 8 inserts per track, buses, sends, the Elite plugins, Song Forge and WAV export.\n\n"
                "Built on The Hybrid Core (C++23, no JUCE). Free software under the GNU GPL v3."
                + std::string (AudioHost::hasAsio() ? "\nASIO Interface Technology by Steinberg Media Technologies GmbH (ASIO SDK under its GPLv3 option)." : "");
    d.width = 560.0f;
    showDialog (*mainUi, d, {});
}
} // namespace hem::app
