#include "EditPage.hpp"
#include "../platform/Platform.hpp"
#include "media/MediaLoader.hpp"

#include <cmath>

namespace hem::app
{
namespace
{
    struct PaletteEntry { const char* name; std::uint32_t argb; };
    constexpr PaletteEntry kPalette[] { { "Gold", 0xffe0a526 }, { "Cyan", 0xff2ec8e6 }, { "Red", 0xffe0533c }, { "Green", 0xff5bc94a },
                                        { "Purple", 0xffa45be0 }, { "Teal", 0xff1fb59a }, { "Pink", 0xffe05ba8 }, { "Blue", 0xff3a7be0 } };
    constexpr float kHeader = 40.0f, kRuler = 40.0f, kScroll = 12.0f;
}

std::vector<int> EditPage::usedTracks() const
{
    std::vector<int> t;
    for (int i = 0; i < E::kMaxTracks; ++i) if (app.engine.isTrackUsed (i)) t.push_back (i);
    return t;
}

void EditPage::fitToWidth()
{
    const double len = std::max (10.0, app.engine.getSongLengthSeconds() * 1.03);
    pxPerSecond = std::max (0.05, (double) std::max (100.0f, lanesW) / len);
    startSeconds = 0.0;
    autoFit = true;
}

void EditPage::zoom (double factor, double around)
{
    const float ax = secondsToX (around) - lanesX;
    pxPerSecond = std::clamp (pxPerSecond * factor, 0.5, 4000.0);
    startSeconds = std::max (0.0, around - (double) ax / pxPerSecond);
    autoFit = false;
}

//==============================================================================
void EditPage::drawLeft (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    auto& eng = app.engine;
    const auto tracks = usedTracks();
    Rect head = withHeight (panel, kHeader);
    p.panelText ("TRACKS  " + std::to_string (tracks.size()) + "/" + std::to_string (E::kMaxTracks), trimRight (trimLeft (head, 10), 120), 15.0f);
    const Rect importB { panel.right() - 104, panel.y + 6, 96, kHeader - 12 };
    if (ui.button (makeId ("edit.import"), importB, "+ IMPORT"))
    {
        auto files = platform::openFiles (app.hwnd(), "Import audio (stems)", App::songsFolder(),
                                          { { "Audio files", pg::MediaLoader::wildcard() } }, true);
        if (! files.empty()) app.importFiles (std::move (files));
    }
    ui.tooltip (importB, "Load WAV / MP3 / FLAC stems as tracks (you can also drag files onto the window)");

    const Rect list = trimTop (panel, kHeader);
    const float content = (float) tracks.size() * rowHeight;
    scrollY = std::clamp (scrollY, 0.0f, std::max (0.0f, content - list.h));
    p.pushClip (list);
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const int t = tracks[i];
        const Rect row { panel.x, list.y + (float) i * rowHeight - scrollY, panel.w, rowHeight };
        if (row.bottom() < list.y || row.y > list.bottom()) continue;
        const Rect r = reduced (row, 4.0f, 2.0f);
        const bool selected = app.selectedStrip == t;
        const Rgba col = argb (eng.getTrackColour (t));
        p.fillRound (r, 5.0f, alpha (colours::panel, selected ? 0.95f : 0.78f));
        p.fillRound (withWidth (r, 6.0f), 2.0f, col);
        if (selected) p.outlineRound (r, 5.0f, 1.5f, alpha (col, 0.85f));
        Rect text = trimRight (trimLeft (r, 14.0f), 4.0f);
        p.text (std::to_string (t + 1) + "  " + eng.getTrackName (t), takeTop (text, r.h * 0.5f), 14.0f, true, colours::paper, HAlign::left, VAlign::bottom);
        if (eng.getTrackMedia (t) == nullptr)
            p.text ("audio file missing", text, 10.5f, false, brighter (colours::meterRed, 0.4f), HAlign::right, VAlign::top);

        Rect b = trimLeft (reduced (row, 6.0f, 4.0f), 12.0f);
        Rect bottom = takeBottom (b, std::min (22.0f, b.h * 0.5f));
        const Id base = makeId ("edit.row", (std::uint64_t) t);
        const auto muteId = E::Ids::track (t, "mute"), soloId = E::Ids::track (t, "solo");
        const bool muted = app.mixNorm (muteId) > 0.5f, soloed = app.mixNorm (soloId) > 0.5f;
        const Rect mb = takeLeft (bottom, 26.0f); takeLeft (bottom, 4.0f);
        const Rect sb = takeLeft (bottom, 26.0f); takeLeft (bottom, 4.0f);
        const Rect fb = takeLeft (bottom, 52.0f);
        if (ui.capButton (subId (base, 1), mb, "M", muted, colours::amber)) app.setMixNorm (muteId, muted ? 0.0f : 1.0f);
        if (ui.capButton (subId (base, 2), sb, "S", soloed, colours::hemGreen)) app.setMixNorm (soloId, soloed ? 0.0f : 1.0f);
        ui.tooltip (mb, "Mute");
        ui.tooltip (sb, "Solo");
        int n = 0;
        for (int s = 0; s < E::kSlots; ++s) n += eng.getPlugin (t, s) != nullptr ? 1 : 0;
        if (ui.button (subId (base, 3), fb, n > 0 ? "FX " + std::to_string (n) : "+ FX", ButtonLook::dark, col))
        {
            app.selectedStrip = t;
            app.showInsertMenu (t, app.firstFreeSlot (t), fb.x, fb.bottom());
        }
        ui.tooltip (fb, "Plugins on this track: click to add one");
        if (ui.unclaimedPress (r)) app.selectedStrip = t;
        if (ui.unclaimedRight (r)) { app.selectedStrip = t; trackMenu (t, ui.input().rightX, ui.input().rightY); }
    }
    p.popClip();
    if (const float w = ui.wheel (list); w != 0.0f) scrollY = std::clamp (scrollY - w * 100.0f, 0.0f, std::max (0.0f, content - list.h));
}

void EditPage::trackMenu (int t, float x, float y)
{
    auto& eng = app.engine;
    std::vector<MenuItem> colourMenu;
    for (int i = 0; i < 8; ++i)
    {
        auto m = MenuItem::item (kPalette[i].name, 100 + i);
        m.colour = argb (kPalette[i].argb);
        colourMenu.push_back (m);
    }
    std::vector<MenuItem> m { MenuItem::headerItem (eng.getTrackName (t)), MenuItem::item ("Rename...", 1),
                              MenuItem::submenu ("Colour", colourMenu), MenuItem::item ("Add plugin...", 2), MenuItem::sep(),
                              MenuItem::item ("Remove track", 3) };
    app.ui().openMenu (std::move (m), x, y, [this, t, x, y] (int r)
    {
        auto& e = app.engine;
        if (r >= 100) { e.setTrackColour (t, kPalette[r - 100].argb); app.structureChanged(); return; }
        if (r == 1)
        {
            DialogSpec d;
            d.title = "Rename track";
            d.fields = { { "NAME", e.getTrackName (t) } };
            d.buttons = { "OK", "CANCEL" };
            showDialog (app.ui(), d, [this, t] (int b, const std::vector<std::string>& v)
            {
                if (b != 0 || v.empty()) return;
                std::string n = v[0];
                while (! n.empty() && n.back() == ' ') n.pop_back();
                while (! n.empty() && n.front() == ' ') n.erase (n.begin());
                if (n.empty()) return;
                if (n.size() > 24) n.resize (24);
                app.engine.setTrackName (t, n);
                app.structureChanged();
            });
        }
        else if (r == 2) app.showInsertMenu (t, app.firstFreeSlot (t), x, y);
        else if (r == 3) app.removeTrack (t);
    });
}

//==============================================================================
void EditPage::drawMain (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    auto& eng = app.engine;
    const Rect bounds = trimBottom (panel, kScroll);
    lanesX = bounds.x;
    lanesW = bounds.w;
    rowHeight = std::clamp (std::floor (panel.h / 8.0f), 48.0f, 90.0f);
    if (autoFit) fitToWidth();

    const double bpm = std::max (20.0f, eng.getMixer (E::Ids::bpm));
    const double barSec = 4.0 * 60.0 / bpm;
    const auto tracks = usedTracks();

    // follow the play-head: page forward when it leaves the view
    if (eng.isPlaying() && ! autoFit)
    {
        const float x = secondsToX (eng.getPositionSeconds());
        if (x > bounds.right() - 20.0f || x < bounds.x) startSeconds = std::max (0.0, eng.getPositionSeconds() - 0.05 * bounds.w / pxPerSecond);
    }

    //---------------------------------------------------------------- ruler
    const Rect ruler = withHeight (bounds, kRuler);
    p.fillRound (ruler, 4.0f, alpha (argb (0xff101317), 0.9f));
    if (eng.isLooping() || eng.getLoopEnd() > eng.getLoopStart())
    {
        const float lx0 = std::max (ruler.x, secondsToX (eng.getLoopStart())), lx1 = std::min (ruler.right(), secondsToX (eng.getLoopEnd()));
        const Rgba lc = eng.isLooping() ? colours::glow : colours::paperDim;
        if (lx1 > lx0)
        {
            p.fill ({ lx0, ruler.y, lx1 - lx0, ruler.h }, alpha (lc, 0.35f), eng.isLooping());
            p.fill ({ lx0, ruler.y, 2.0f, ruler.h }, lc);
            p.fill ({ lx1 - 2.0f, ruler.y, 2.0f, ruler.h }, lc);
        }
    }
    int barStep = 1;
    while (barStep * barSec * pxPerSecond < 60.0 && barStep < 1024) barStep *= 2;
    const int firstBar = std::max (0, (int) std::floor (startSeconds / barSec / barStep) * barStep);
    p.pushClip (ruler);
    for (int bar = firstBar;; bar += barStep)
    {
        const float x = secondsToX (bar * barSec);
        if (x > bounds.right()) break;
        p.fill ({ x, ruler.y + ruler.h * 0.5f, 1.0f, ruler.h * 0.5f }, alpha (colours::white, 0.5f));
        p.text (std::to_string (bar + 1), { x + 3.0f, ruler.y + 2.0f, 60.0f, 16.0f }, 11.5f, true, colours::paper);
        const int secs = (int) (bar * barSec);
        char t[16];
        std::snprintf (t, sizeof t, "%d:%02d", secs / 60, secs % 60);
        p.text (t, { x + 3.0f, ruler.y + 18.0f, 60.0f, 14.0f }, 9.5f, false, colours::paperDim);
    }
    p.popClip();

    //---------------------------------------------------------------- lanes
    const Rect lanes = trimTop (bounds, kRuler + 2.0f);
    const float content = (float) tracks.size() * rowHeight;
    scrollY = std::clamp (scrollY, 0.0f, std::max (0.0f, content - lanes.h));
    p.pushClip (lanes);
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        const float y = lanes.y + (float) i * rowHeight - scrollY;
        if (y + rowHeight < lanes.y || y > lanes.bottom()) continue;
        const int t = tracks[i];
        const Rect lane { bounds.x, y + 2.0f, bounds.w, rowHeight - 4.0f };
        const Rgba col = argb (eng.getTrackColour (t));
        p.fillRound (lane, 4.0f, alpha (colours::panel, app.selectedStrip == t ? 0.85f : 0.62f));
        for (int bar = firstBar;; bar += barStep)
        {
            const float x = secondsToX (bar * barSec);
            if (x > bounds.right()) break;
            p.fill ({ x, lane.y, 1.0f, lane.h }, alpha (colours::white, 0.05f));
        }
        if (auto media = eng.getTrackMedia (t))
        {
            const double len = (double) media->getNumFrames() / media->getSourceRate();
            const float x0 = std::max (bounds.x, secondsToX (0.0)), x1 = std::min (bounds.right(), secondsToX (len));
            if (x1 > x0) p.fillRound ({ x0, lane.y, x1 - x0, lane.h }, 4.0f, alpha (col, 0.18f));
            if (auto ov = app.waves.get (media))
            {
                const float mid = lane.cy(), half = lane.h * 0.46f;
                const double bucketsPerSecond = ov->sourceRate / WaveOverview::kSamplesPerBucket;
                const Rgba wc = brighter (col, 0.3f);
                const float step = std::max (1.0f, p.pixel());
                for (float px = std::floor (x0); px < x1; px += step)
                {
                    const auto b0 = (std::size_t) std::max (0.0, xToSeconds (px) * bucketsPerSecond);
                    const auto b1 = (std::size_t) std::max ((double) b0 + 1.0, xToSeconds (px + step) * bucketsPerSecond);
                    float lo = 0.0f, hi = 0.0f;
                    for (std::size_t b = b0; b < b1 && b < ov->mins.size(); ++b) { lo = std::min (lo, ov->mins[b]); hi = std::max (hi, ov->maxs[b]); }
                    const float top = mid - std::min (1.0f, hi) * half, bot = mid - std::max (-1.0f, lo) * half;
                    p.fill ({ px, top, step, std::max (step, bot - top) }, wc);
                }
            }
            else
                p.text ("drawing waveform...", reduced (lane, 10.0f, 0.0f), 11.0f, false, colours::paperDim);
        }
        p.text (eng.getTrackName (t), reduced (lane, 8.0f, 4.0f), 11.5f, true, alpha (colours::paper, 0.9f), HAlign::left, VAlign::top);
    }
    p.popClip();

    if (tracks.empty())
    {
        Rect top = lanes;
        Rect upper = takeTop (top, lanes.h * 0.5f);
        p.panelText ("DRAG YOUR STEMS HERE", upper, 30.0f, HAlign::centre, VAlign::bottom);
        p.panelText ("WAV / MP3 / FLAC - or File > Import Audio, or make a song in SONG FORGE", trimTop (top, 8.0f), 15.0f, HAlign::centre,
                     VAlign::top, colours::paperDim);
    }

    //---------------------------------------------------------------- mouse
    auto rd = ui.drag (makeId ("edit.ruler"), ruler);
    if (rd.started) dragStartSeconds = std::max (0.0, xToSeconds (rd.startX));
    if (rd.dragging && rd.moved)
    {
        const double now = std::max (0.0, xToSeconds (rd.x));
        eng.setLoop (true, std::min (dragStartSeconds, now), std::max (dragStartSeconds, now));
    }
    if (rd.ended && ! rd.moved) eng.setPositionSeconds (dragStartSeconds);
    ui.tooltip (ruler, "Click to move the play-head. Drag to set the loop (L turns it on / off).");

    auto ld = ui.drag (makeId ("edit.lanes"), lanes);
    if (ld.started)
    {
        const int idx = (int) std::floor ((ld.startY - lanes.y + scrollY) / rowHeight);
        if (idx >= 0 && idx < (int) tracks.size()) app.selectedStrip = tracks[(std::size_t) idx];
        eng.setPositionSeconds (std::max (0.0, xToSeconds (ld.startX)));
    }

    if (const float w = ui.wheel (bounds); w != 0.0f)
    {
        if (ui.input().ctrl) zoom (w > 0 ? 1.25 : 0.8, xToSeconds (ui.input().x));
        else if (ui.input().shift)
        {
            startSeconds = std::max (0.0, startSeconds - w * 200.0 / pxPerSecond);
            autoFit = false;
        }
        else scrollY = std::clamp (scrollY - w * 100.0f, 0.0f, std::max (0.0f, content - lanes.h));
    }

    //---------------------------------------------------------------- scroll bar
    const Rect bar { panel.x, panel.bottom() - kScroll + 2.0f, panel.w, kScroll - 3.0f };
    const double total = std::max (10.0, eng.getSongLengthSeconds() * 1.05);
    const double visible = (double) bounds.w / pxPerSecond;
    const double limit = std::max (total, startSeconds + visible);
    p.fillRound (bar, 4.0f, alpha (colours::black, 0.7f));
    const float tx = bar.x + (float) (startSeconds / limit) * bar.w, tw = std::max (20.0f, (float) (visible / limit) * bar.w);
    const Rect thumb { std::min (tx, bar.right() - tw), bar.y, tw, bar.h };
    auto sd = ui.drag (makeId ("edit.hscroll"), bar);
    p.fillRound (thumb, 4.0f, alpha (colours::paperDim, sd.hover || sd.dragging ? 0.6f : 0.35f));
    if (sd.dragging && ! sd.started)
    {
        startSeconds = std::clamp (startSeconds + (double) sd.stepX / bar.w * limit, 0.0, std::max (0.0, limit - visible));
        autoFit = false;
    }

    //---------------------------------------------------------------- play-head
    const float px = secondsToX (eng.getPositionSeconds());
    if (px >= bounds.x - 2.0f && px <= bounds.right() + 2.0f)
    {
        const float h = bounds.h;
        p.fill ({ px - 1.5f, bounds.y, 4.0f, h }, alpha (colours::white, 0.25f));
        const Rgba hc = eng.isPlaying() ? colours::hemGreen : colours::white;
        p.fill ({ px, bounds.y, 1.5f, h }, hc, eng.isPlaying());
        p.triangle (px - 6.0f, bounds.y, px + 7.5f, bounds.y, px + 0.75f, bounds.y + 9.0f, hc);
    }
}
} // namespace hem::app
