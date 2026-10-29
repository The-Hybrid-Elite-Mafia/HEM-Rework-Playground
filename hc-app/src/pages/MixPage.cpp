#include "MixPage.hpp"
#include "Strips.hpp"
#include "Registry.hpp"

namespace hem::app
{
void MixPage::drawMain (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    Rect r = panel;
    const Rect masterR = trimBottom (takeRight (r, kStripWidth + 10.0f), 10.0f);
    takeRight (r, 8.0f);
    drawChannelStrip (app, ui, masterR, E::kMaster);

    const Rect bar = takeBottom (r, 10.0f);
    std::vector<int> strips;
    for (int t = 0; t < E::kMaxTracks; ++t) if (app.engine.isTrackUsed (t)) strips.push_back (t);
    for (int b = 0; b < E::kNumBuses; ++b) strips.push_back (E::busStrip (b));
    const float content = (float) strips.size() * kStripWidth + 10.0f;
    scrollX = std::clamp (scrollX, 0.0f, std::max (0.0f, content - r.w));

    p.pushClip (r);
    float x = r.x - scrollX;
    for (const int s : strips)
    {
        if (s == E::kFirstBus) x += 10.0f;   // gap before the buses
        const Rect sr { x, r.y, kStripWidth, r.h };
        if (sr.right() >= r.x && sr.x <= r.right()) drawChannelStrip (app, ui, sr, s);
        x += kStripWidth;
    }
    p.popClip();

    // scroll bar (only when the strips do not fit)
    if (content > r.w)
    {
        const Rect track = reduced (bar, 0.0f, 1.0f);
        p.fillRound (track, 4.0f, alpha (colours::black, 0.7f));
        const float tw = std::max (30.0f, track.w * r.w / content);
        const float tx = track.x + (track.w - tw) * (scrollX / std::max (1.0f, content - r.w));
        auto d = ui.drag (makeId ("mix.scroll"), track);
        p.fillRound ({ tx, track.y, tw, track.h }, 4.0f, alpha (colours::paperDim, d.hover || d.dragging ? 0.6f : 0.35f));
        if (d.dragging && ! d.started) scrollX = std::clamp (scrollX + d.stepX * content / track.w, 0.0f, content - r.w);
        if (const float w = ui.wheel (r); w != 0.0f) scrollX = std::clamp (scrollX - w * 120.0f, 0.0f, content - r.w);
    }
}

void MixPage::drawLeft (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    p.panelText ("ELITE PLUGINS", reduced (withHeight (panel, 34.0f), 10.0f, 0.0f), 15.0f);
    const auto& all = pg::PluginRegistry::all();
    const float top = 40.0f;
    const float h = std::min (64.0f, std::floor ((panel.h - top - 70.0f) / (float) all.size()));
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        const auto* info = all[i];
        const Rect row { panel.x + 8.0f, panel.y + top + (float) i * h, panel.w - 16.0f, h - 6.0f };
        const Rgba acc = argb (info->accent);
        const auto pr = ui.behave (makeId ("lib.plugin", i), row);
        p.fillRound (row, 5.0f, alpha (colours::panel, pr.hover ? 0.95f : 0.75f));
        p.fillRound (withWidth (row, 5.0f), 2.0f, acc, true);
        p.outlineRound (row, 5.0f, 1.0f, pr.hover ? acc : alpha (colours::white, 0.08f));
        const bool compact = row.h < 40.0f;
        Rect t = reduced (row, 12.0f, compact ? 2.0f : 4.0f);
        p.text (info->name, takeTop (t, t.h * (compact ? 0.52f : 0.48f)), compact ? 12.5f : 14.0f, true, colours::paper, HAlign::left, VAlign::bottom);
        p.wrapped (info->blurb, t, compact ? 9.5f : 10.5f, false, colours::paperDim, compact ? 1 : 2);
        if (pr.clicked) app.addPluginToStrip (app.selectedStrip, info->id);
        ui.tooltip (row, info->blurb);
    }
    const Rect foot = reduced ({ panel.x, panel.bottom() - 60.0f, panel.w, 60.0f }, 10.0f, 4.0f);
    p.wrapped ("Click a plugin to add it to the selected strip:  " + app.engine.getStripName (app.selectedStrip), foot, 11.0f, false,
               colours::paperDim, 3, HAlign::left, VAlign::middle);
}
} // namespace hem::app
