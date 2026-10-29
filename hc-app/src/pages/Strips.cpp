#include "Strips.hpp"
#include "../PluginWindows.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace hem::app
{
std::string dbText (float db)
{
    if (db <= E::kFaderOffDb + 0.05f) return "-inf";
    char b[16];
    std::snprintf (b, sizeof b, "%s%.1f", db > 0.0f ? "+" : "", db);
    return b;
}

Rgba stripAccent (App& app, int strip)
{
    if (E::isTrack (strip)) return argb (app.engine.getTrackColour (strip));
    if (E::isBus (strip)) return colours::glow;
    return colours::gold;
}

namespace
{
    bool bypassed (pg::Plugin& p)
    {
        const auto* b = p.parameters().find ("bypass");
        return b != nullptr && b->get() > 0.5f;
    }

    void slotMenu (App& app, int strip, int slot, float x, float y)
    {
        auto* p = app.engine.getPlugin (strip, slot);
        if (p == nullptr) return;
        const bool off = bypassed (*p);
        std::vector<MenuItem> m { MenuItem::headerItem (std::string (p->getInfo().name) + "  (slot " + std::to_string (slot + 1) + ")"),
                                  MenuItem::item ("Open window", 1), MenuItem::item (off ? "Turn on (un-bypass)" : "Bypass", 2),
                                  MenuItem::item ("Replace with...", 3), MenuItem::item ("Move up", 4, slot > 0),
                                  MenuItem::item ("Move down", 5, slot < E::kSlots - 1), MenuItem::sep(), MenuItem::item ("Remove", 6) };
        app.ui().openMenu (std::move (m), x, y, [&app, strip, slot, off, x, y] (int r)
        {
            switch (r)
            {
                case 1: app.openPlugin (strip, slot); break;
                case 2: if (auto* pl = app.engine.getPlugin (strip, slot)) pl->setParameter ("bypass", off ? 0.0f : 1.0f); break;
                case 3: app.showInsertMenu (strip, slot, x, y); break;
                case 4: app.movePlugin (strip, slot, slot - 1); break;
                case 5: app.movePlugin (strip, slot, slot + 1); break;
                case 6: app.removePlugin (strip, slot); break;
                default: break;
            }
        });
    }

    /** Value bubble over a knob while it is touched (0.10.0's pop-up display). */
    void bubble (Ui& ui, Rect knob, const std::string& text, Rgba accent)
    {
        auto& p = ui.p;
        const float w = p.textWidth (text, 12.0f) + 14.0f;
        const Rect b { knob.cx() - w * 0.5f, knob.y - 22.0f, w, 19.0f };
        p.fillRound (b, 5.0f, alpha (colours::black, 0.92f));
        p.outlineRound (b, 5.0f, 1.0f, alpha (accent, 0.7f));
        p.text (text, b, 12.0f, true, brighter (accent, 0.5f), HAlign::centre);
    }

    std::vector<float> faderTicks (const pg::Param& prm)
    {
        std::vector<float> t;
        for (const float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f })
            if (db >= prm.spec().range.start && db <= prm.spec().range.end) t.push_back (prm.spec().range.valueTo01 (db));
        return t;
    }
}

void drawInsertSlot (App& app, Ui& ui, Rect area, int strip, int slot)
{
    auto& p = ui.p;
    const Rect r = reduced (area, 1.0f, 1.0f);
    const Id id = makeId ("slot", (std::uint64_t) (strip * 16 + slot));
    const auto pr = ui.behave (id, r);
    auto* pl = app.engine.getPlugin (strip, slot);
    if (pl == nullptr)
    {
        p.fillRound (r, 3.0f, alpha (argb (0xff15181d), pr.hover ? 0.95f : 0.75f));
        p.text ("+", r, r.h * 0.62f / 0.55f, true, alpha (colours::white, pr.hover ? 0.5f : 0.18f), HAlign::centre);
    }
    else
    {
        const Rgba acc = argb (pl->getInfo().accent);
        const bool off = bypassed (*pl);
        const float fade = off ? 0.35f : 1.0f;
        p.verticalGradient (r, alpha (brighter (acc, 0.15f), fade), alpha (darker (acc, 0.55f), fade));
        if (app.windows && app.windows->isOpen (pl)) p.outlineRound (r, 3.0f, 1.5f, colours::white);
        else if (pr.hover) p.outlineRound (r, 3.0f, 1.0f, alpha (colours::white, 0.5f));
        std::string name = r.w > 160.0f ? std::string (pl->getInfo().name) : std::string (pl->getInfo().shortName);
        if (r.w > 160.0f) for (auto& c : name) c = (char) std::toupper ((unsigned char) c);
        p.text (name, reduced (r, 4.0f, 0.0f), std::min (13.0f, r.h * 0.62f), true, off ? colours::paperDim : colours::white, HAlign::centre);
        if (off) p.line (r.x + 4.0f, r.cy(), r.right() - 4.0f, r.cy(), 1.2f, alpha (colours::meterRed, 0.8f));
    }
    if (pr.right && pl != nullptr) { app.selectedStrip = strip; slotMenu (app, strip, slot, pr.rx, pr.ry); }
    if (pr.clicked)
    {
        app.selectedStrip = strip;
        if (pl == nullptr) app.showInsertMenu (strip, slot, r.x, r.bottom());
        else app.openPlugin (strip, slot);
    }
    ui.tooltip (r, "Insert " + std::to_string (slot + 1) + ": click to add / open, right-click for more");
}

void drawChannelStrip (App& app, Ui& ui, Rect bounds, int strip)
{
    auto& p = ui.p;
    auto& eng = app.engine;
    const bool track = E::isTrack (strip), bus = E::isBus (strip), master = strip == E::kMaster;
    const Rgba acc = stripAccent (app, strip);
    const Id base = makeId ("strip", (std::uint64_t) strip);

    const Rect bg = reduced (bounds, 2.0f, 0.0f);
    const bool selected = app.selectedStrip == strip;
    p.fillRound (bg, 5.0f, alpha (colours::panel, 0.82f));
    p.fillRound (withHeight (bg, 5.0f), 2.0f, acc, true);
    p.outlineRound (reduced (bg, 0.5f), 5.0f, selected ? 1.6f : 1.0f, selected ? alpha (acc, 0.9f) : alpha (colours::white, 0.08f));

    const bool compact = bounds.h < 640.0f;
    const float cap = compact ? 10.0f : 12.0f;
    Rect r = reduced (bounds, 6.0f, compact ? 4.0f : 6.0f);
    takeTop (r, 3.0f);

    // name: double-click renames (not the master)
    const Rect nameR = takeTop (r, compact ? 18.0f : 20.0f);
    p.text (eng.getStripName (strip), nameR, 13.0f, true, colours::paper, HAlign::centre);
    if (! master)
    {
        const auto pr = ui.behave (subId (base, 1), nameR);
        ui.tooltip (nameR, "Double-click to rename");
        if (pr.doubleClick)
        {
            DialogSpec d;
            d.title = track ? "Rename track" : "Rename bus";
            d.fields = { { "NAME", eng.getStripName (strip) } };
            d.buttons = { "OK", "CANCEL" };
            showDialog (ui, d, [&app, strip] (int b, const std::vector<std::string>& v)
            {
                if (b != 0 || v.empty()) return;
                std::string n = v[0];
                while (! n.empty() && n.back() == ' ') n.pop_back();
                while (! n.empty() && n.front() == ' ') n.erase (n.begin());
                if (n.size() > 24) n.resize (24);
                if (E::isTrack (strip)) app.engine.setTrackName (strip, n);
                else if (E::isBus (strip)) app.engine.setBusName (strip - E::kFirstBus, n);
                app.structureChanged();
            });
        }
    }
    takeTop (r, 2.0f);

    const float slotH = compact ? 15.0f : std::clamp (std::floor (bounds.h / 30.0f), 17.0f, 24.0f);
    for (int i = 0; i < E::kSlots; ++i) drawInsertSlot (app, ui, takeTop (r, slotH), strip, i);
    takeTop (r, compact ? 3.0f : 6.0f);

    std::vector<std::string> busNames;
    for (int b = 0; b < E::kNumBuses; ++b) busNames.push_back (eng.getBusName (b));

    auto knob = [&] (Rect k, const std::string& id, std::uint64_t n, const std::string& tip)
    {
        auto* prm = eng.mixer().find (id);
        if (prm == nullptr) return;
        float v = prm->getNormalised();
        const bool bipolar = prm->spec().range.start < 0.0f && prm->spec().range.end > 0.0f;
        const float zero = bipolar ? prm->spec().range.valueTo01 (0.0f) : 0.0f;
        const Id kid = subId (base, n);
        if (ui.knob (kid, k, v, bipolar ? zero : prm->defaultNormalised(), acc, bipolar, zero)) prm->setNormalised (v);
        if (ui.isHot (kid) || ui.isActive (kid)) bubble (ui, k, prm->spec().toText (prm->get()), acc);
        ui.tooltip (k, tip);
    };
    auto combo = [&] (Rect c, const std::string& id, std::uint64_t n, const std::vector<std::string>& items, const std::string& tip)
    {
        auto* prm = eng.mixer().find (id);
        if (prm == nullptr) return;
        int idx = (int) std::lround (prm->get());
        if (ui.combo (subId (base, n), c, items, idx, acc, false, true, {}, std::min (12.0f, c.h * 0.6f))) prm->setValue ((float) idx);
        ui.tooltip (c, tip);
    };

    if (track)
    {
        const float rowH = compact ? 18.0f : std::clamp (std::floor (bounds.h / 30.0f), 18.0f, 24.0f);
        auto sendRow = [&] (const char* caption, const char* busId, const char* levelId, std::uint64_t n, const std::string& tipBus, const std::string& tipLevel)
        {
            p.text (caption, takeTop (r, cap), cap == 10.0f ? 8.5f : 9.5f, true, colours::paperDim);
            Rect row = takeTop (r, rowH + (compact ? 2.0f : 6.0f));
            const Rect k = takeRight (row, rowH + (compact ? 2.0f : 6.0f));
            combo (centred (row, row.w - 2.0f, rowH), E::Ids::track (strip, busId), n, busNames, tipBus);
            knob (k, E::Ids::track (strip, levelId), n + 1, tipLevel);
        };
        sendRow ("SEND A", "sa_bus", "sa", 10, "Send A goes to this bus", "Send A level (after the fader)");
        sendRow ("SEND B", "sb_bus", "sb", 20, "Send B goes to this bus", "Send B level (after the fader)");
        p.text ("OUTPUT", takeTop (r, cap), cap == 10.0f ? 8.5f : 9.5f, true, colours::paperDim);
        auto outs = busNames;
        outs.insert (outs.begin(), "Master");
        combo (takeTop (r, rowH), E::Ids::track (strip, "out"), 30, outs, "Where this track's fader goes (Master, or a bus like the Drum Bus)");
        takeTop (r, compact ? 2.0f : 4.0f);
    }

    Rect bottom = takeBottom (r, master ? 24.0f : 22.0f);
    if (track)
    {
        const auto muteId = E::Ids::track (strip, "mute"), soloId = E::Ids::track (strip, "solo");
        const bool muted = app.mixNorm (muteId) > 0.5f, soloed = app.mixNorm (soloId) > 0.5f;
        const Rect mb = reduced (takeLeft (bottom, bottom.w * 0.5f), 2.0f, 0.0f), sb = reduced (bottom, 2.0f, 0.0f);
        if (ui.capButton (subId (base, 40), mb, "M", muted, colours::amber)) app.setMixNorm (muteId, muted ? 0.0f : 1.0f);
        if (ui.capButton (subId (base, 41), sb, "S", soloed, colours::hemGreen)) app.setMixNorm (soloId, soloed ? 0.0f : 1.0f);
        ui.tooltip (mb, "Mute");
        ui.tooltip (sb, "Solo (only soloed tracks play)");
    }
    else if (bus)
    {
        const auto muteId = E::Ids::bus (strip - E::kFirstBus, "mute");
        const bool muted = app.mixNorm (muteId) > 0.5f;
        const Rect mb = centred (bottom, bottom.w * 0.5f, bottom.h);
        if (ui.capButton (subId (base, 40), mb, "M", muted, colours::amber)) app.setMixNorm (muteId, muted ? 0.0f : 1.0f);
        ui.tooltip (mb, "Mute bus");
    }
    else
        p.text ("MASTER", bottom, 9.5f, true, colours::paperDim, HAlign::centre);
    takeBottom (r, 4.0f);

    const std::string volId = track ? E::Ids::track (strip, "vol") : (bus ? E::Ids::bus (strip - E::kFirstBus, "vol") : std::string (E::Ids::masterVol));
    const std::string panId = track ? E::Ids::track (strip, "pan") : E::Ids::bus (strip - E::kFirstBus, "pan");
    auto* vol = eng.mixer().find (volId);
    Rect valueBox {};
    if (compact && ! master)
    {
        Rect row = takeBottom (r, 28.0f);
        knob (takeLeft (row, 28.0f), panId, 50, "Pan (double-click = centre)");
        valueBox = reduced (row, 4.0f, 5.0f);
    }
    else
    {
        valueBox = reduced (takeBottom (r, 18.0f), 6.0f, 0.0f);
        takeBottom (r, 4.0f);
        if (! master)
        {
            const float pk = std::min (r.w * 0.5f, 40.0f);
            knob (centred (takeTop (r, pk), pk, pk), panId, 50, "Pan (double-click = centre)");
            takeTop (r, 4.0f);
        }
    }
    if (vol != nullptr)
    {
        p.fillRound (valueBox, 3.0f, colours::well);
        p.text (dbText (vol->get()), valueBox, 12.0f, true, brighter (acc, 0.5f), HAlign::centre, VAlign::middle, TextLook::neon);
    }
    takeBottom (r, 2.0f);
    const Rect meterR = takeRight (r, 14.0f);
    takeRight (r, 4.0f);
    auto& m = app.stripMeter (strip);
    if (ui.behave (subId (base, 60), meterR).clicked) m.clipped = false;
    drawMeter (p, meterR, m, false);
    if (vol != nullptr)
    {
        float v = vol->getNormalised();
        const auto ticks = faderTicks (*vol);
        if (ui.fader (subId (base, 70), r, v, vol->spec().range.valueTo01 (0.0f), acc, ticks, vol->spec().range.valueTo01 (0.0f)))
            vol->setNormalised (v);
        ui.tooltip (r, "Volume (double-click = 0 dB)");
    }

    if (ui.unclaimedPress (bounds)) app.selectedStrip = strip;
}
} // namespace hem::app
