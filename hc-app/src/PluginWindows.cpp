#include "PluginWindows.hpp"
#include "PluginPanels.hpp"
#include "platform/Platform.hpp"

#include "EliteEq.hpp"
#include "EliteSwirl.hpp"
#include "Presets.hpp"
#include "Waterfall.hpp"
#include "hem/pg/Files.hpp"

#include "hybrid/ui/FxPanelView.hpp"

#include <algorithm>
#include <cmath>

namespace hem::app
{
namespace
{
    constexpr float kBarH = 96.0f;                 // preset bar, panel design units
    constexpr float kPixelsPerUnit = 0.62f;        // window size at 100 % Windows scaling
    const Rgba kObsidian = argb (0xff0c0d12);

    /** PanelHost straight onto a Playground plug-in: knob turns go through the plug-in's own controls. */
    class Host final : public hc::ui::PanelHost
    {
    public:
        Host (pg::Plugin& p, const PanelMap& m) : plugin (p), map (m)
        {
            for (auto& id : map.paramIds) params.push_back (plugin.parameters().find (id));
            bypassParam = plugin.parameters().find ("bypass");
        }
        float normalized (int i) const override { return valid (i) ? params[(std::size_t) i]->getNormalised() : 0.0f; }
        void beginEdit (int) override {}
        void setNormalized (int i, float v) override { if (valid (i)) params[(std::size_t) i]->setNormalised (v); }
        void endEdit (int) override {}
        std::string text (int i) const override { return valid (i) ? params[(std::size_t) i]->spec().toText (params[(std::size_t) i]->get()) : std::string(); }
        float meter (int m) const override
        {
            if (m < 0 || m >= (int) map.meters.size()) return 0.0f;
            return meterValue (plugin, map.meters[(std::size_t) m]);
        }
        bool bypassed() const override { return bypassParam != nullptr && bypassParam->get() > 0.5f; }
        void setBypassed (bool on) override { plugin.setParameter ("bypass", on ? 1.0f : 0.0f); }

    private:
        bool valid (int i) const noexcept { return i >= 0 && i < (int) params.size() && params[(std::size_t) i] != nullptr; }
        pg::Plugin& plugin;
        const PanelMap& map;
        std::vector<pg::Param*> params;
        const pg::Param* bypassParam = nullptr;
    };
}

//==============================================================================
class PluginWindow
{
public:
    PluginWindow (App& a, pg::Plugin& p, std::string t) : plugin (p), app (a), title (std::move (t)), map (panelFor (p)), host (p, map)
    {
        layout = std::make_unique<hc::ui::PanelLayout> (hc::ui::layoutFxPanel (map.spec));
        view = std::make_unique<hc::ui::FxPanelView> (*layout, map.spec);
        controller = std::make_unique<hc::ui::FxPanelController> (*layout, map.spec, host);
        eq = dynamic_cast<pg::EliteEq*> (&plugin);
        swirl = dynamic_cast<pg::EliteSwirl*> (&plugin);
        displayH = eq != nullptr ? std::round (layout->width * 0.24f) : swirl != nullptr ? std::round (layout->width * 0.33f) : 0.0f;
        if (eq != nullptr) { plugin.setAnalyzerActive (true); shown.fill (kLevelLow); }
        if (swirl != nullptr)
            for (auto& f : history) { f.level.fill (-90.0f); f.swirl.fill (0.0f); f.spread.fill (0.0f); f.pan.fill (0.0f); }
        armDoubleClickToCurrent();
    }

    ~PluginWindow()
    {
        if (eq != nullptr) plugin.setAnalyzerActive (false);
        if (created)
        {
            window.makeCurrent();
            fields.reset();
            renderer.shutdown();
        }
        window.destroy();
    }

    bool create (int cascade, bool visible, std::string& error)
    {
        const float w = layout->width * kPixelsPerUnit, h = (kBarH + displayH + layout->height) * kPixelsPerUnit;
        // CW_USEDEFAULT placement: Windows cascades new windows, so several plug-ins never stack exactly
        if (! window.create (platform::wide (title), (int) std::lround (w), (int) std::lround (h), visible, &error)) return false;
        window.setVsync (false);   // the main window paces the loop
        if (! renderer.init (4, &error)) return false;
        if (! renderer.setParts (app.partsAtlas(), &error)) return false;
        created = true;
        fields = platform::makeTextFields (window.nativeHandle());
        paint = std::make_unique<Paint> (renderer);
        ui = std::make_unique<Ui> (*paint, fields.get());
        if (! resize (error)) return false;
        if (visible) platform::setOwner (window.nativeHandle(), app.hwnd());
        (void) cascade;
        return true;
    }

    void toFront() { platform::bringToFront (window.nativeHandle()); }
    bool frame (double seconds);
    bool snapshot (const fs::path& png);

    pg::Plugin& plugin;

private:
    static constexpr float kLevelLow = -84.0f;

    float designW() const { return layout->width; }
    float designH() const { return kBarH + displayH + layout->height; }
    float panelTop() const { return kBarH + displayH; }

    bool resize (std::string& error)
    {
        const int w = window.pixelWidth(), h = window.pixelHeight();
        hc::ui::Canvas c;
        c.scale = std::min ((float) w / designW(), (float) h / designH());
        c.offsetX = std::round (((float) w - designW() * c.scale) * 0.5f);
        c.offsetY = std::round (((float) h - designH() * c.scale) * 0.5f);
        c.pixelW = (float) w;
        c.pixelH = (float) h;
        if (haveCanvas && c == canvas) return true;
        if (! renderer.setTargetSize (w, h, &error)) return false;
        if (std::abs (c.scale - fontScale) > 0.005f * std::max (fontScale, 0.01f))
        {
            if (! font.bake (app.fontSet(), c.scale, true)) { error = "could not bake the Poppins atlas"; return false; }
            if (! renderer.setFont (font, &error)) return false;
            fontScale = c.scale;
        }
        if (plateScale <= 0.0f || std::abs (c.scale - plateScale) > 0.12f * plateScale)
        {
            const float ppu = std::min ({ c.scale, 4096.0f / layout->width, 4096.0f / layout->height });
            if (! renderer.setPanel (hc::ui::buildMips (hc::ui::premultiply (hc::ui::renderFxPlate (*layout, hc::ui::FxPalette {}, ppu))), &error)) return false;
            plateScale = c.scale;
        }
        canvas = c;
        haveCanvas = true;
        renderer.beginStatic (canvas);
        // obsidian behind the preset bar and the display, the plate itself below
        renderer.fillRound ({ -canvas.offsetX / canvas.scale, -canvas.offsetY / canvas.scale, (float) w / canvas.scale, (float) h / canvas.scale },
                            0.0f, kObsidian, hc::ui::Blend::Over);
        renderer.panel ({ 0, panelTop(), layout->width, layout->height });
        renderer.endStatic();
        return true;
    }

    void handleInput (Input& in);
    void drawPresetBar (Rect bar);
    void presetMenu (float x, float y);
    void stepPreset (int delta);
    void savePresetAs();
    void armDoubleClickToCurrent();
    void drawEq (Rect area);
    void drawSwirl (Rect area);

    App& app;
    std::string title;
    PanelMap map;
    Host host;
    std::unique_ptr<hc::ui::PanelLayout> layout;
    std::unique_ptr<hc::ui::FxPanelView> view;
    std::unique_ptr<hc::ui::FxPanelController> controller;
    hc::ui::GlWindow window;
    hc::ui::Renderer renderer;
    hc::ui::FontAtlas font;
    hc::ui::Canvas canvas;
    float fontScale = 0.0f, plateScale = 0.0f, displayH = 0.0f;
    bool haveCanvas = false, created = false, panelDrag = false;
    std::unique_ptr<TextFields> fields;
    std::unique_ptr<Paint> paint;
    std::unique_ptr<Ui> ui;
    std::string currentPreset = "Default";
    Input input;

    // Elite EQ display
    pg::EliteEq* eq = nullptr;
    pg::analysis::Waterfall analyzer;
    std::vector<float> scratch = std::vector<float> ((std::size_t) pg::Plugin::kAnalyzerFifoSize, 0.0f);
    std::array<float, pg::analysis::Waterfall::kPoints> shown {};
    double preparedRate = 0.0;
    int dragBand = -1;

    // Elite Swirl Killer display
    pg::EliteSwirl* swirl = nullptr;
    static constexpr int kHistory = 64;
    std::vector<pg::EliteSwirl::Frame> history = std::vector<pg::EliteSwirl::Frame> ((std::size_t) kHistory);
    std::array<pg::EliteSwirl::Frame, 64> incoming {};
    int newest = 0;
    float yaw = -0.62f, pitch = 0.62f, dragYaw = 0.0f, dragPitch = 0.0f;
    int heightMode = 0, camera = 0;
};

//==============================================================================
void PluginWindow::handleInput (Input& in)
{
    in.pressed = in.released = in.doubleClick = in.rightPressed = false;
    in.wheel = 0.0f;
    in.keys.clear();
    in.dropped.clear();
    const bool overlay = ui->menuOpen() || ui->overlayOpen();
    for (const auto& e : window.takeEvents())
    {
        using T = hc::ui::InputEvent::Type;
        const float dx = canvas.toDesignX (e.x), dy = canvas.toDesignY (e.y);
        const float lx = dx / kL, ly = dy / kL;
        const bool inPanel = dy >= panelTop() && ! overlay;
        switch (e.type)
        {
            case T::MouseDown:
                in.down = true; in.pressed = true; in.doubleClick = e.doubleClick; in.pressX = lx; in.pressY = ly; in.x = lx; in.y = ly;
                in.ctrl = e.ctrl; in.shift = e.shift;
                if (inPanel) { controller->mouseDown (dx, dy - panelTop(), e.doubleClick, e.fine); panelDrag = true; in.pressed = false; }
                break;
            case T::MouseUp:
                in.down = false; in.released = true; in.x = lx; in.y = ly;
                if (panelDrag) { controller->mouseUp (dx, dy - panelTop()); panelDrag = false; }
                break;
            case T::MouseMove:
                in.x = lx; in.y = ly; in.ctrl = e.ctrl; in.shift = e.shift;
                controller->mouseMove (dx, dy - panelTop(), e.fine);
                break;
            case T::Wheel:
                in.x = lx; in.y = ly; in.ctrl = e.ctrl; in.shift = e.shift;
                if (inPanel) controller->wheel (dx, dy - panelTop(), e.wheel, e.fine);
                else in.wheel += e.wheel;
                break;
            case T::RightDown:
                in.rightPressed = true; in.rightX = lx; in.rightY = ly;
                break;
            case T::Key:
                in.keys.push_back ({ e.key, e.ctrl, e.shift });
                break;
            default: break;
        }
    }
}

bool PluginWindow::frame (double seconds)
{
    if (! window.pump()) return false;
    window.makeCurrent();
    std::string err;
    if (! resize (err)) return false;
    handleInput (input);
    Input& in = input;
    if (window.minimized()) return true;

    renderer.beginFrame (canvas);
    ui->beginFrame (in, canvas, seconds);
    view->draw (renderer, host, controller->hover(), controller->active(), 0.0f, panelTop(), false);
    drawPresetBar ({ 0.0f, 0.0f, designW() / kL, kBarH / kL });
    if (displayH > 0.0f)
    {
        const Rect area = reduced ({ 0.0f, kBarH / kL, designW() / kL, displayH / kL }, 10.0f, 4.0f);
        if (eq != nullptr) drawEq (area);
        if (swirl != nullptr) drawSwirl (area);
    }
    // the plate's double-click goes to the value of the preset that is loaded (0.10.0 behaviour)
    ui->endFrame();
    renderer.endFrame (0);
    window.swap();
    return true;
}

bool PluginWindow::snapshot (const fs::path& png)
{
    window.makeCurrent();
    std::string err;
    if (! resize (err)) return false;
    Input in;
    renderer.beginFrame (canvas);
    ui->beginFrame (in, canvas, 0.0);
    view->draw (renderer, host, {}, {}, 0.0f, panelTop(), false);
    drawPresetBar ({ 0.0f, 0.0f, designW() / kL, kBarH / kL });
    if (displayH > 0.0f)
    {
        const Rect area = reduced ({ 0.0f, kBarH / kL, designW() / kL, displayH / kL }, 10.0f, 4.0f);
        if (eq != nullptr) drawEq (area);
        if (swirl != nullptr) drawSwirl (area);
    }
    ui->endFrame();
    renderer.endFrameToCapture();
    return hc::ui::writePng (png, renderer.capture());
}

//==============================================================================
void PluginWindow::armDoubleClickToCurrent()
{
    for (std::size_t i = 0; i < map.spec.params.size(); ++i) map.spec.params[i].defNormalized = host.normalized ((int) i);
}

void PluginWindow::drawPresetBar (Rect bar)
{
    auto& p = *paint;
    const Rgba acc = argb (plugin.getInfo().accent);
    Rect r = reduced (bar, 14.0f, 9.0f);
    p.fillRound (expanded (r, 6.0f, 4.0f), 8.0f, argb (0xff15171d));
    p.outlineRound (expanded (r, 6.0f, 4.0f), 8.0f, 1.0f, alpha (acc, 0.35f));
    const int nFactory = (int) pg::Presets::factory (plugin.getInfo().id).size();
    const int nUser = (int) pg::Presets::userPresetNames (plugin.getInfo().id).size();
    const Rect count = takeRight (r, 90.0f);
    p.text (std::to_string (nFactory + nUser) + " PRESETS", count, 11.0f, true, colours::paperDim, HAlign::right);
    const float bh = r.h;
    const Rect saveB = takeRight (r, bh * 1.9f);
    takeRight (r, 4.0f);
    const Rect nextB = takeRight (r, bh);
    takeRight (r, 4.0f);
    const Rect prevB = takeRight (r, bh);
    takeRight (r, 8.0f);
    const Id base = makeId ("preset", (std::uint64_t) (std::uintptr_t) &plugin);
    if (ui->button (subId (base, 1), saveB, "SAVE", ButtonLook::dark, acc, true, false, 11.0f)) savePresetAs();
    ui->tooltip (saveB, "Save these settings as your own preset");
    if (ui->button (subId (base, 2), nextB, ">", ButtonLook::dark, acc, true, false, 12.0f)) stepPreset (1);
    if (ui->button (subId (base, 3), prevB, "<", ButtonLook::dark, acc, true, false, 12.0f)) stepPreset (-1);
    // the preset name box: a click opens the list
    const auto pr = ui->behave (subId (base, 4), r);
    p.fillRound (r, 4.0f, colours::well);
    p.outlineRound (r, 4.0f, 1.0f, alpha (acc, pr.hover ? 0.85f : 0.4f));
    p.text (currentPreset, reduced (trimRight (r, r.h), 8.0f, 0.0f), 13.0f, true, colours::paper);
    const float a = r.h * 0.16f, ax = r.right() - r.h * 0.5f, ay = r.cy();
    p.triangle (ax - a, ay - a * 0.5f, ax + a, ay - a * 0.5f, ax, ay + a * 0.7f, colours::paperDim);
    if (pr.clicked) presetMenu (r.x, r.bottom() + 2.0f);
    ui->tooltip (r, "Presets: factory (Suno fixes) and your own");
}

void PluginWindow::presetMenu (float x, float y)
{
    const auto& factory = pg::Presets::factory (plugin.getInfo().id);
    const auto user = pg::Presets::userPresetNames (plugin.getInfo().id);
    constexpr int kSave = 9001, kDelete = 9002, kReset = 9003, kUser = 501;
    std::vector<MenuItem> m { MenuItem::headerItem ("FACTORY") };
    for (std::size_t i = 0; i < factory.size(); ++i) m.push_back (MenuItem::item (factory[i].name, 1 + (int) i, true, factory[i].name == currentPreset));
    if (! user.empty())
    {
        m.push_back (MenuItem::headerItem ("MY PRESETS"));
        for (std::size_t i = 0; i < user.size(); ++i) m.push_back (MenuItem::item (user[i], kUser + (int) i, true, user[i] == currentPreset));
    }
    m.push_back (MenuItem::sep());
    m.push_back (MenuItem::item ("Save as new preset...", kSave));
    m.push_back (MenuItem::item ("Delete my preset \"" + currentPreset + "\"", kDelete,
                                 std::find (user.begin(), user.end(), currentPreset) != user.end()));
    m.push_back (MenuItem::item ("Reset to default", kReset));
    ui->openMenu (std::move (m), x, y, [this, user] (int id)
    {
        const auto& f = pg::Presets::factory (plugin.getInfo().id);
        if (id >= 1 && id <= (int) f.size()) { pg::Presets::apply (plugin, f[(std::size_t) id - 1]); currentPreset = f[(std::size_t) id - 1].name; }
        else if (id >= kUser && id < kUser + (int) user.size()) { currentPreset = user[(std::size_t) (id - kUser)]; pg::Presets::loadUser (plugin, currentPreset); }
        else if (id == kSave) { savePresetAs(); return; }
        else if (id == kDelete) { pg::Presets::deleteUser (plugin.getInfo().id, currentPreset); currentPreset = "Default"; }
        else if (id == kReset) { pg::Presets::apply (plugin, pg::FactoryPreset { "Default", {} }); currentPreset = "Default"; }
        armDoubleClickToCurrent();
    }, 260.0f);
}

void PluginWindow::stepPreset (int delta)
{
    std::vector<std::string> all;
    const auto& factory = pg::Presets::factory (plugin.getInfo().id);
    for (const auto& f : factory) all.push_back (f.name);
    for (auto& u : pg::Presets::userPresetNames (plugin.getInfo().id)) all.push_back (u);
    if (all.empty()) return;
    const auto it = std::find (all.begin(), all.end(), currentPreset);
    int idx = it == all.end() ? (delta > 0 ? 0 : (int) all.size() - 1) : ((int) (it - all.begin()) + delta + (int) all.size()) % (int) all.size();
    if (idx < (int) factory.size()) pg::Presets::apply (plugin, factory[(std::size_t) idx]);
    else pg::Presets::loadUser (plugin, all[(std::size_t) idx]);
    currentPreset = all[(std::size_t) idx];
    armDoubleClickToCurrent();
}

void PluginWindow::savePresetAs()
{
    DialogSpec d;
    d.title = "Save preset";
    d.message = "Name your " + std::string (plugin.getInfo().name) + " preset:";
    d.fields = { { "NAME", currentPreset == "Default" ? std::string() : currentPreset } };
    d.buttons = { "SAVE", "CANCEL" };
    d.accent = argb (plugin.getInfo().accent);
    d.width = 420.0f;
    showDialog (*ui, d, [this] (int b, const std::vector<std::string>& v)
    {
        if (b != 0 || v.empty()) return;
        std::string name = v[0];
        while (! name.empty() && name.back() == ' ') name.pop_back();
        while (! name.empty() && name.front() == ' ') name.erase (name.begin());
        if (name.empty()) return;
        if (pg::Presets::saveUser (plugin, name))
        {
            currentPreset = pg::Presets::legalFileName (name);
            armDoubleClickToCurrent();
        }
    });
}

//==============================================================================
// Elite EQ: the spectrum of what comes OUT, the EQ curve, one numbered dot per band
// (drag: left / right = FREQ, up / down = GAIN; double-click: GAIN back to 0 dB).
void PluginWindow::drawEq (Rect area)
{
    using W = pg::analysis::Waterfall;
    auto& p = *paint;
    static constexpr float kCurveRange = 18.0f;
    const Rgba kAccent = argb (0xff2ec8e6);
    static const Rgba kBandColours[pg::EliteEq::kBands] { argb (0xffff5a5a), argb (0xffffa62e), argb (0xfff5e04a), argb (0xff4ee07a),
                                                          argb (0xff4aa8ff), argb (0xffc36bff) };
    // pull the analyzer
    const double sr = eq->getPreparedSampleRate() > 0.0 ? eq->getPreparedSampleRate() : 48000.0;
    if (sr != preparedRate) { analyzer.prepare (sr); preparedRate = sr; }
    for (int got; (got = eq->readAnalyzer (scratch.data(), (int) scratch.size())) > 0;) analyzer.add (scratch.data(), got);
    const float* row0 = analyzer.row (0);
    for (int i = 0; i < W::kPoints; ++i)
        shown[(std::size_t) i] = row0[i] > shown[(std::size_t) i] ? row0[i] : std::max (kLevelLow, shown[(std::size_t) i] - 0.9f);

    const float U = area.w / 470.0f;
    p.fillRound (area, 6.0f * U, argb (0xff0b0d12));
    const Rect r = trimBottom (reduced (area, 14.0f * U, 10.0f * U), 16.0f * U);
    auto xOf = [&r] (float hz) { return r.x + r.w * std::log (hz / 20.0f) / std::log (1000.0f); };
    auto hzOf = [&r] (float x) { return 20.0f * std::pow (1000.0f, std::clamp ((x - r.x) / r.w, 0.0f, 1.0f)); };
    auto yOfDb = [&r] (float db) { return r.cy() - r.h * 0.5f * std::clamp (db / kCurveRange, -1.2f, 1.2f); };
    auto dbOfY = [&r] (float y) { return std::clamp ((r.cy() - y) / (r.h * 0.5f) * kCurveRange, -kCurveRange, kCurveRange); };
    auto yOfLevel = [&r] (float db) { return r.bottom() - r.h * std::clamp ((db - kLevelLow) / -kLevelLow, 0.0f, 1.0f); };
    auto prm = [this] (int band, const char* what) { return eq->parameters().find (pg::EliteEq::id (band, what)); };
    auto movesGain = [&] (int band)
    {
        const int type = (int) std::lround (prm (band, "type")->get());
        if (band == 0) return type != 0;                              // Low Cut has no gain
        if (band == pg::EliteEq::kBands - 1) return type != 1;        // High Cut has no gain
        return type != 3;                                             // Notch has no gain
    };
    auto dotOf = [&] (int band)
    {
        const float hz = prm (band, "freq")->get();
        const float g = movesGain (band) ? prm (band, "gain")->get() : 0.0f;
        return std::pair<float, float> { xOf (hz), yOfDb (g) };
    };

    for (const auto& [hz, text] : { std::pair<float, const char*> { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                                    { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } })
    {
        const float x = xOf (hz);
        p.fill ({ x, r.y, 1.0f, r.h }, argb (0xff222836));
        p.text (text, { x - 20.0f * U, r.bottom() + 2.0f, 40.0f * U, 14.0f * U }, 9.0f * U, false, argb (0xff6d7380), HAlign::centre);
    }
    for (const float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
    {
        const float y = yOfDb (db);
        p.fill ({ r.x, y, r.w, 1.0f }, db == 0.0f ? argb (0xff3a4152) : argb (0xff1e2430));
        p.text ((db > 0 ? "+" : "") + std::to_string ((int) db), { r.x + 2.0f, y - 12.0f * U, 30.0f * U, 12.0f * U }, 9.0f * U, false, argb (0xff6d7380));
    }
    std::vector<float> xs, ys;
    for (int i = 0; i < W::kPoints; ++i) { xs.push_back (xOf (W::pointHz (i))); ys.push_back (yOfLevel (shown[(std::size_t) i])); }
    p.area (xs, ys, r.bottom(), alpha (kAccent, 0.35f), alpha (kAccent, 0.05f));

    xs.clear(); ys.clear();
    const int steps = std::max (64, (int) (r.w / 1.5f));
    for (int i = 0; i <= steps; ++i)
    {
        const float x = r.x + r.w * (float) i / (float) steps;
        xs.push_back (x);
        ys.push_back (yOfDb (eq->responseDb (sr, hzOf (x))));
    }
    p.pushClip (r);
    p.polyline (xs, ys, 5.0f * U, alpha (colours::white, 0.18f));
    p.polyline (xs, ys, 2.0f * U, colours::white, true);
    p.popClip();

    // dots
    int hover = -1;
    {
        float best = 16.0f * U;
        const auto& in = ui->input();
        for (int b = 0; b < pg::EliteEq::kBands; ++b)
        {
            const auto [cx, cy] = dotOf (b);
            const float d = std::hypot (cx - in.x, cy - in.y);
            if (d < best) { best = d; hover = b; }
        }
    }
    const Id did = makeId ("eq.dots", (std::uint64_t) (std::uintptr_t) eq);
    auto d = ui->drag (did, hover >= 0 || dragBand >= 0 ? area : Rect {});
    if (d.started)
    {
        dragBand = hover;
        if (dragBand >= 0 && d.doubleClick && movesGain (dragBand)) { prm (dragBand, "gain")->setValue (0.0f); dragBand = -1; }
        else if (dragBand >= 0 && prm (dragBand, "on")->get() < 0.5f) prm (dragBand, "on")->setValue (1.0f);   // grabbing a band switches it on
    }
    else if (d.dragging && dragBand >= 0)
    {
        prm (dragBand, "freq")->setValue (hzOf (d.x));
        if (movesGain (dragBand)) prm (dragBand, "gain")->setValue (dbOfY (d.y));
    }
    if (d.ended) dragBand = -1;
    for (int b = 0; b < pg::EliteEq::kBands; ++b)
    {
        const bool on = prm (b, "on")->get() > 0.5f;
        const auto [cx, cy] = dotOf (b);
        const float rad = (b == dragBand || b == hover ? 11.0f : 9.0f) * U;
        p.fillEllipse ({ cx - rad, cy - rad, rad * 2, rad * 2 }, alpha (kBandColours[b], on ? 1.0f : 0.3f), on);
        p.text (std::to_string (b + 1), { cx - rad, cy - rad, rad * 2, rad * 2 }, 10.0f * U, true, argb (0xff0b0d12), HAlign::centre);
    }
    p.panelText ("drag a dot: left / right = FREQ, up / down = GAIN  -  double-click = 0 dB", { r.x, area.y + 4.0f * U, r.w - 4.0f, 14.0f * U },
                 9.5f * U, HAlign::right, VAlign::middle, argb (0xff8a8f99));
}

//==============================================================================
// Elite Swirl Killer: X time (last ~1.5 s, newest in front), Y frequency, Z level / width / position,
// colour = how much swirl was found. Drag to turn, double-click to reset.
void PluginWindow::drawSwirl (Rect area)
{
    auto& p = *paint;
    constexpr int B = pg::EliteSwirl::kBands;
    const int got = swirl->readFrames (incoming.data(), (int) incoming.size());
    for (int i = 0; i < got; ++i) { newest = (newest + 1) % kHistory; history[(std::size_t) newest] = incoming[(std::size_t) i]; }
    const Rgba acc = argb (swirl->getInfo().accent);
    const float U = area.w / 500.0f;
    p.fillRound (area, 6.0f * U, argb (0xff0b0d12));

    auto heat = [] (float v)
    {
        v = std::clamp (v, 0.0f, 1.0f);
        static const Rgba stops[] { argb (0xff1a3cff), argb (0xff19c8ff), argb (0xff35e06a), argb (0xffffe04a), argb (0xffff3b30) };
        const float x = v * 4.0f;
        const int i = std::min (3, (int) x);
        return mix (stops[i], stops[i + 1], x - (float) i);
    };

    // buttons
    const Rect heightB { area.right() - 140.0f * U, area.y + 6.0f * U, 130.0f * U, 22.0f * U };
    const char* camNames[4] { "3D", "TOP", "SIDE", "FRONT" };
    const Id base = makeId ("swirl", (std::uint64_t) (std::uintptr_t) swirl);
    for (int i = 0; i < 4; ++i)
    {
        const float w = 42.0f * U;
        const Rect cb { area.right() - 146.0f * U - (float) (4 - i) * (w + 4.0f * U), area.y + 6.0f * U, w, 22.0f * U };
        if (ui->button (subId (base, 1 + (std::uint64_t) i), cb, camNames[i], ButtonLook::dark, acc, true, camera == i, 10.0f * U))
        {
            camera = i;
            switch (i)
            {
                case 1:  yaw = 0.0f; pitch = 1.5f; break;
                case 2:  yaw = 0.0f; pitch = 0.12f; break;
                case 3:  yaw = -1.5708f; pitch = 0.12f; heightMode = 2; break;
                default: yaw = -0.62f; pitch = 0.62f; break;
            }
        }
    }
    const char* modes[3] { "HEIGHT: LEVEL", "HEIGHT: STEREO WIDTH", "HEIGHT: LEFT / RIGHT" };
    if (ui->button (subId (base, 10), heightB, modes[heightMode], ButtonLook::dark, acc, true, false, 10.0f * U)) heightMode = (heightMode + 1) % 3;
    // title on the left (clear of the view buttons, which start 176 U from the left edge); the hint sits at the bottom
    p.text ("3D SWIRL MAP", { area.x + 12.0f * U, area.y + 6.0f * U, 150.0f * U, 22.0f * U }, 11.0f * U, true, colours::paper);
    p.text ("drag to turn  -  double-click = reset", { area.x + 12.0f * U, area.bottom() - 22.0f * U, 240.0f * U, 18.0f * U }, 9.0f * U, false, colours::paperDim);

    auto d = ui->drag (subId (base, 20), trimTop (area, 32.0f * U));
    if (d.started) { dragYaw = yaw; dragPitch = pitch; if (d.doubleClick) { camera = 0; yaw = -0.62f; pitch = 0.62f; } }
    else if (d.dragging)
    {
        yaw = dragYaw + d.dx * 0.008f;
        pitch = std::clamp (dragPitch + d.dy * 0.006f, 0.08f, 1.5f);
        camera = -1;
    }

    const Rect plot = trimRight (reduced (area, 40.0f * U, 22.0f * U), 70.0f * U);
    const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const float scale = std::min (plot.w * 0.62f, plot.h * 0.95f);
    const float ccx = plot.cx(), ccy = plot.cy() + plot.h * 0.12f;
    auto project = [&] (float x, float y, float z, float& depth)
    {
        const float rx = x * cy - y * sy, ry = x * sy + y * cy;
        const float py = ry * cp - z * sp, pz = ry * sp + z * cp;
        depth = py;
        const float persp = 1.0f / (1.0f + 0.35f * py);
        return std::pair<float, float> { ccx + rx * scale * 1.35f * persp, ccy + (py * 0.55f - pz * 1.2f) * scale * persp };
    };
    auto heightOf = [this] (const pg::EliteSwirl::Frame& f, int b)
    {
        if (heightMode == 1) return std::clamp (f.spread[(std::size_t) b], 0.0f, 1.0f) * 0.45f;
        if (heightMode == 2)
            return 0.225f + std::clamp (f.pan[(std::size_t) b], -1.0f, 1.0f) * 0.225f * std::clamp ((f.level[(std::size_t) b] + 84.0f) / 60.0f, 0.0f, 1.0f);
        return std::clamp ((f.level[(std::size_t) b] + 84.0f) / 84.0f, 0.0f, 1.0f) * 0.45f;
    };

    p.pushClip (area);
    float dd;
    for (int i = 0; i <= 8; ++i)
    {
        const float v = -0.5f + (float) i / 8.0f;
        auto a0 = project (v, -0.5f, 0.0f, dd), a1 = project (v, 0.5f, 0.0f, dd);
        p.line (a0.first, a0.second, a1.first, a1.second, 1.0f, alpha (colours::white, 0.10f));
        auto b0 = project (-0.5f, v, 0.0f, dd), b1 = project (0.5f, v, 0.0f, dd);
        p.line (b0.first, b0.second, b1.first, b1.second, 1.0f, alpha (colours::white, 0.10f));
    }
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
    {
        const float y = std::log (hz / 40.0f) / std::log (20000.0f / 40.0f) - 0.5f;
        const auto q = project (0.56f, y, 0.0f, dd);
        p.text (hz >= 1000.0f ? std::to_string ((int) (hz / 1000.0f)) + "k" : std::to_string ((int) hz), { q.first - 4.0f, q.second - 8.0f, 40.0f, 16.0f },
                10.0f * U, true, colours::paperDim);
    }

    struct V { float x, y, depth; };
    std::vector<V> verts ((std::size_t) (kHistory * B));
    for (int t = 0; t < kHistory; ++t)
    {
        const auto& f = history[(std::size_t) ((newest + 1 + t) % kHistory)];
        const float x = -0.5f + (float) t / (float) (kHistory - 1);
        for (int b = 0; b < B; ++b)
        {
            const float y = -0.5f + (float) b / (float) (B - 1);
            float depth = 0.0f;
            const auto q = project (x, y, heightOf (f, b), depth);
            verts[(std::size_t) (t * B + b)] = { q.first, q.second, depth };
        }
    }
    struct Q { float depth; int t, b; };
    std::vector<Q> quads;
    quads.reserve ((std::size_t) ((kHistory - 1) * (B - 1)));
    for (int t = 0; t < kHistory - 1; ++t)
        for (int b = 0; b < B - 1; ++b)
            quads.push_back ({ verts[(std::size_t) (t * B + b)].depth + verts[(std::size_t) ((t + 1) * B + b + 1)].depth, t, b });
    std::sort (quads.begin(), quads.end(), [] (const Q& a, const Q& b2) { return a.depth > b2.depth; });
    for (const auto& q : quads)
    {
        const auto& f0 = history[(std::size_t) ((newest + 1 + q.t) % kHistory)];
        const auto& f1 = history[(std::size_t) ((newest + 2 + q.t) % kHistory)];
        const float sw = 0.25f * (f0.swirl[(std::size_t) q.b] + f0.swirl[(std::size_t) q.b + 1] + f1.swirl[(std::size_t) q.b] + f1.swirl[(std::size_t) q.b + 1]);
        const float lv = std::clamp (0.5f * ((f0.level[(std::size_t) q.b] + 84.0f) + (f1.level[(std::size_t) q.b + 1] + 84.0f)) / 84.0f, 0.0f, 1.0f);
        Rgba c = heat (std::max (sw * 1.6f, lv * 0.38f));
        const float br = 0.55f + 0.45f * lv;
        c = { c.r * br, c.g * br, c.b * br, 0.92f };
        const V& a = verts[(std::size_t) (q.t * B + q.b)];
        const V& b2 = verts[(std::size_t) ((q.t + 1) * B + q.b)];
        const V& c2 = verts[(std::size_t) ((q.t + 1) * B + q.b + 1)];
        const V& d2 = verts[(std::size_t) (q.t * B + q.b + 1)];
        const float qx[4] { a.x, b2.x, c2.x, d2.x }, qy[4] { a.y, b2.y, c2.y, d2.y };
        p.quad (qx, qy, c);
    }
    // FROM line on the floor
    const float fromHz = swirl->getParameter ("from");
    const float fy = std::log (fromHz / 40.0f) / std::log (20000.0f / 40.0f) - 0.5f;
    const auto l0 = project (-0.5f, fy, 0.0f, dd), l1 = project (0.5f, fy, 0.0f, dd);
    p.line (l0.first, l0.second, l1.first, l1.second, 2.0f, alpha (acc, 0.9f), true);
    const auto lp = project (-0.56f, fy, 0.0f, dd);
    p.text ("FROM", { lp.first - 46.0f, lp.second - 8.0f, 44.0f, 16.0f }, 10.0f * U, true, colours::paperDim, HAlign::right);
    if (heightMode == 2)
    {
        auto c0 = project (-0.5f, -0.5f, 0.225f, dd), c1 = project (0.5f, -0.5f, 0.225f, dd);
        p.line (c0.first, c0.second, c1.first, c1.second, 1.0f, alpha (colours::white, 0.35f));
        c0 = project (-0.5f, 0.5f, 0.225f, dd); c1 = project (0.5f, 0.5f, 0.225f, dd);
        p.line (c0.first, c0.second, c1.first, c1.second, 1.0f, alpha (colours::white, 0.35f));
    }
    p.popClip();
    // legend
    const Rect legend { area.right() - 52.0f * U, area.y + 40.0f * U, 14.0f * U, area.h - 90.0f * U };
    for (int i = 0; i < 4; ++i)
    {
        const float t0 = (float) i / 4.0f, t1 = (float) (i + 1) / 4.0f;
        p.verticalGradient ({ legend.x, legend.y + legend.h * t0, legend.w, legend.h * (t1 - t0) }, heat (1.0f - t0), heat (1.0f - t1));
    }
    p.text ("SWIRL", { legend.x - 14.0f * U, legend.y - 18.0f * U, legend.w + 28.0f * U, 16.0f * U }, 9.0f * U, true, colours::paperDim, HAlign::centre);
    p.text ("CLEAN", { legend.x - 14.0f * U, legend.bottom() + 2.0f, legend.w + 28.0f * U, 16.0f * U }, 9.0f * U, true, colours::paperDim, HAlign::centre);
}

//==============================================================================
PluginWindows::PluginWindows (App& a) : app (a) {}
PluginWindows::~PluginWindows() { closeAll(); }

void PluginWindows::open (pg::Plugin& plugin, const std::string& where)
{
    if (auto it = windows.find (&plugin); it != windows.end()) { it->second->toFront(); return; }
    auto w = std::make_unique<PluginWindow> (app, plugin, std::string (plugin.getInfo().name) + "  -  " + where);
    std::string err;
    if (! w->create (cascade++ % 6, true, err)) { app.status ("Could not open the plugin window: " + err); return; }
    windows[&plugin] = std::move (w);
}

void PluginWindows::close (pg::Plugin* plugin)
{
    if (auto it = windows.find (plugin); it != windows.end())
    {
        auto w = std::move (it->second);
        windows.erase (it);
        w.reset();   // the window goes while the plug-in still exists
    }
}

void PluginWindows::closeAll()
{
    while (! windows.empty()) close (const_cast<pg::Plugin*> (windows.begin()->first));
    cascade = 0;
}

void PluginWindows::frame (double seconds)
{
    std::vector<const pg::Plugin*> closed;
    for (auto& [p, w] : windows)
        if (! w->frame (seconds)) closed.push_back (p);
    for (auto* p : closed) close (const_cast<pg::Plugin*> (p));
}

bool PluginWindows::snapshot (const fs::path& png)
{
    if (windows.empty()) return false;
    return windows.begin()->second->snapshot (png);
}
} // namespace hem::app
