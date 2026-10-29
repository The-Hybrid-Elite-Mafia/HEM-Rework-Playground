#include "Spectrum3D.hpp"

#include <cmath>
#include <cstdio>

namespace hem::app
{
namespace
{
    using W = pg::analysis::Waterfall;
    constexpr float kXW = 1.6f, kZD = 2.4f, kH = 0.95f, kDbLow = -84.0f, kCamDist = 4.2f;
    const Rgba kBg = argb (0xff0b0d12);
    const Rgba kOrange = argb (0xffff8a2a);

    float height (float db) noexcept { return kH * std::clamp ((db - kDbLow) / -kDbLow, 0.0f, 1.0f); }
    float xOf (float hz) noexcept { return -kXW + 2.0f * kXW * std::log (hz / 20.0f) / std::log (1000.0f); }

    // deep blue -> violet -> HEM orange -> hot yellow-white
    Rgba heat (float t) noexcept
    {
        t = std::clamp (t, 0.0f, 1.0f);
        static const Rgba stops[] { argb (0xff0a1a3a), argb (0xff3a2a8c), argb (0xffb0368c), argb (0xffff8a2a), argb (0xffffe08a) };
        const float x = t * 4.0f;
        const int i = std::min (3, (int) x);
        return mix (stops[i], stops[i + 1], x - (float) i);
    }
}

void Spectrum3D::setShowing (bool on)
{
    if (on == tapping) return;
    tapping = on;
    engine.setMasterTapActive (on);
    if (on) while (engine.readMasterTap (scratch.data(), (int) scratch.size()) > 0) {}   // drop stale audio
}

void Spectrum3D::pull()
{
    const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
    if (sr != preparedRate) { waterfall.prepare (sr); preparedRate = sr; }
    for (int got; (got = engine.readMasterTap (scratch.data(), (int) scratch.size())) > 0;)
        waterfall.add (scratch.data(), got);
}

Spectrum3D::P Spectrum3D::project (float x, float y, float z) const noexcept
{
    const float zc = z - 0.5f * kZD;
    const float cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const float x1 = x * cy - zc * sy;
    const float z1 = x * sy + zc * cy;
    const float up = y * cp + z1 * sp;
    const float depth = z1 * cp - y * sp;
    const float s = kCamDist / (kCamDist + depth);
    return { x1 * s, -up * s };
}

void Spectrum3D::draw (Ui& ui, Rect area)
{
    setShowing (true);
    pull();
    auto& p = ui.p;
    p.fillRound (area, 6.0f, kBg);
    p.outlineRound (reduced (area, 0.5f), 6.0f, 1.0f, alpha (kOrange, 0.25f));
    Rect a = area;
    const Rect head = reduced (takeTop (a, 24.0f), 10.0f, 0.0f);
    p.panelText ("3D SPECTRUM", head, 14.0f, HAlign::left, VAlign::middle, brighter (kOrange, 0.3f));
    p.panelText ("drag to turn  -  double-click to reset", head, 11.0f, HAlign::right, VAlign::middle, argb (0xff8a8f99));
    const Rect box = trimBottom (reduced (a, 14.0f, 10.0f), 14.0f);

    auto d = ui.drag (makeId ("spectrum3d"), area);
    if (d.started) { dragYaw = yaw; dragPitch = pitch; if (d.doubleClick) { yaw = kDefaultYaw; pitch = kDefaultPitch; } }
    else if (d.dragging)
    {
        yaw = std::clamp (dragYaw + d.dx * 0.006f, -0.9f, 0.9f);
        pitch = std::clamp (dragPitch + d.dy * 0.006f, 0.15f, 1.1f);
    }
    ui.tooltip (area, "3D spectrum of the master output: newest sound in front, lows left, highs right. Drag to turn it, double-click to reset.");
    if (box.w < 40.0f || box.h < 40.0f) return;

    // fit the whole scene (its 8 corners) into the box
    float x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f;
    for (float cx : { -kXW, kXW })
        for (float cyy : { 0.0f, kH })
            for (float cz : { 0.0f, kZD })
            {
                const auto q = project (cx, cyy, cz);
                x0 = std::min (x0, q.x); x1 = std::max (x1, q.x); y0 = std::min (y0, q.y); y1 = std::max (y1, q.y);
            }
    const float scale = std::min (box.w / (x1 - x0), box.h / (y1 - y0));
    const float ox = box.cx() - 0.5f * (x0 + x1) * scale, oy = box.cy() - 0.5f * (y0 + y1) * scale;
    auto P = [&] (float x, float y, float z) { const auto q = project (x, y, z); return std::pair<float, float> { ox + q.x * scale, oy + q.y * scale }; };
    auto line = [&] (std::pair<float, float> a0, std::pair<float, float> a1, Rgba c, float w) { p.line (a0.first, a0.second, a1.first, a1.second, w, c); };

    p.pushClip (area);
    const Rgba grid = argb (0xff2a3140);
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        line (P (xOf (hz), 0.0f, 0.0f), P (xOf (hz), 0.0f, kZD), grid, 1.0f);
    for (int k = 0; k <= 4; ++k)
    {
        const float z = kZD * (float) k / 4.0f;
        line (P (-kXW, 0.0f, z), P (kXW, 0.0f, z), grid, 1.0f);
    }
    for (float db : { -24.0f, -48.0f, -72.0f })
        line (P (-kXW, height (db), kZD), P (kXW, height (db), kZD), argb (0xff222836), 1.0f);

    // the landscape, back to front so near rows cover far ones
    constexpr int N = W::kPoints;
    std::vector<float> rx ((std::size_t) N), ry ((std::size_t) N), bx ((std::size_t) N), by ((std::size_t) N), hs ((std::size_t) N);
    for (int age = W::kRows - 1; age >= 0; --age)
    {
        const float z = kZD * (float) age / (float) (W::kRows - 1);
        const float* row = waterfall.row (age);
        float peak = 0.0f;
        for (int i = 0; i < N; ++i)
        {
            const float x = -kXW + 2.0f * kXW * (float) i / (float) (N - 1);
            const float h = height (row[i]);
            peak = std::max (peak, h);
            const auto top = P (x, h, z), base = P (x, 0.0f, z);
            rx[(std::size_t) i] = top.first; ry[(std::size_t) i] = top.second;
            bx[(std::size_t) i] = base.first; by[(std::size_t) i] = base.second;
            hs[(std::size_t) i] = h;
        }
        const float fog = (float) age / (float) (W::kRows - 1);
        const Rgba baseC = alpha (heat (0.05f), 0.92f);
        for (int i = 0; i + 1 < N; ++i)
        {
            const std::size_t a0 = (std::size_t) i, a1 = (std::size_t) i + 1;
            const float xs[4] { rx[a0], rx[a1], bx[a1], bx[a0] };
            const float ys[4] { ry[a0], ry[a1], by[a1], by[a0] };
            auto topColour = [&] (float h) { return alpha (mix (heat (0.05f + 0.95f * h / kH), kBg, 0.6f * fog), 0.92f); };
            const Rgba cs[4] { topColour (hs[a0]), topColour (hs[a1]), baseC, baseC };
            p.quad4 (xs, ys, cs);
        }
        if (age == 0)
        {
            p.polyline (rx, ry, 5.0f, alpha (kOrange, 0.25f), true);
            p.polyline (rx, ry, 1.8f, argb (0xffffe08a), true);
        }
        else
        {
            const Rgba lc = mix (heat (0.35f + 0.65f * peak / kH), kBg, 0.7f * fog);
            p.polyline (rx, ry, 1.0f, alpha (lc, 0.55f * (1.0f - fog) + 0.1f));
        }
    }

    // labels at the front edge
    const Rgba lab = argb (0xff8a8f99);
    for (const auto& [hz, text] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
    {
        const auto q = P (xOf (hz), 0.0f, 0.0f);
        p.text (text, { q.first - 20.0f, q.second + 2.0f, 40.0f, 14.0f }, 11.0f, false, lab, HAlign::centre);
    }
    const auto backLeft = P (-kXW, 0.0f, kZD);
    char ago[32];
    std::snprintf (ago, sizeof ago, "%.1f s ago", W::kRows * waterfall.secondsPerRow());
    p.text (ago, { backLeft.first - 70.0f, backLeft.second - 16.0f, 66.0f, 14.0f }, 11.0f, false, lab, HAlign::right);
    p.popClip();
}
} // namespace hem::app
