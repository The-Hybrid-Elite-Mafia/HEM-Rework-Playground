#include "Paint.hpp"

#include <cmath>

namespace hem::app
{
using hc::ui::Blend;
using hc::ui::TextStyle;

//==============================================================================
Rgba argb (std::uint32_t c) noexcept
{
    return { (float) ((c >> 16) & 0xff) / 255.0f, (float) ((c >> 8) & 0xff) / 255.0f, (float) (c & 0xff) / 255.0f,
             (float) ((c >> 24) & 0xff) / 255.0f };
}

Rgba brighter (Rgba c, float amount) noexcept
{
    const float a = 1.0f / (1.0f + amount);   // the same curve 0.10.0's colours used
    return { 1.0f - a * (1.0f - c.r), 1.0f - a * (1.0f - c.g), 1.0f - a * (1.0f - c.b), c.a };
}

Rgba darker (Rgba c, float amount) noexcept
{
    const float a = 1.0f / (1.0f + amount);
    return { c.r * a, c.g * a, c.b * a, c.a };
}

Rgba mix (Rgba a, Rgba b, float t) noexcept
{
    t = std::clamp (t, 0.0f, 1.0f);
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

//==============================================================================
Rect takeTop (Rect& r, float h) noexcept
{
    h = std::clamp (h, 0.0f, std::max (0.0f, r.h));
    const Rect t { r.x, r.y, r.w, h };
    r.y += h; r.h -= h;
    return t;
}

Rect takeBottom (Rect& r, float h) noexcept
{
    h = std::clamp (h, 0.0f, std::max (0.0f, r.h));
    r.h -= h;
    return { r.x, r.y + r.h, r.w, h };
}

Rect takeLeft (Rect& r, float w) noexcept
{
    w = std::clamp (w, 0.0f, std::max (0.0f, r.w));
    const Rect t { r.x, r.y, w, r.h };
    r.x += w; r.w -= w;
    return t;
}

Rect takeRight (Rect& r, float w) noexcept
{
    w = std::clamp (w, 0.0f, std::max (0.0f, r.w));
    r.w -= w;
    return { r.x + r.w, r.y, w, r.h };
}

Rect reduced (Rect r, float dx, float dy) noexcept { return { r.x + dx, r.y + dy, std::max (0.0f, r.w - 2 * dx), std::max (0.0f, r.h - 2 * dy) }; }
Rect expanded (Rect r, float dx, float dy) noexcept { return { r.x - dx, r.y - dy, r.w + 2 * dx, r.h + 2 * dy }; }
Rect centred (Rect r, float w, float h) noexcept { return { r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h }; }
Rect withHeight (Rect r, float h) noexcept { return { r.x, r.y, r.w, h }; }
Rect withWidth (Rect r, float w) noexcept { return { r.x, r.y, w, r.h }; }
Rect trimTop (Rect r, float d) noexcept { takeTop (r, d); return r; }
Rect trimBottom (Rect r, float d) noexcept { takeBottom (r, d); return r; }
Rect trimLeft (Rect r, float d) noexcept { takeLeft (r, d); return r; }
Rect trimRight (Rect r, float d) noexcept { takeRight (r, d); return r; }

Rect unite (Rect a, Rect b) noexcept
{
    const float x0 = std::min (a.x, b.x), y0 = std::min (a.y, b.y);
    const float x1 = std::max (a.right(), b.right()), y1 = std::max (a.bottom(), b.bottom());
    return { x0, y0, x1 - x0, y1 - y0 };
}

Rect fromDesign (Rect d) noexcept { return { d.x / kL, d.y / kL, d.w / kL, d.h / kL }; }
Rect toDesign (Rect l) noexcept { return { l.x * kL, l.y * kL, l.w * kL, l.h * kL }; }

//==============================================================================
TextStyle styleFor (float heightL, bool bold) noexcept
{
    // 0.10.0 gave Poppins' full height (ascent + descent = 1.4 em) in window pixels of a 1600-wide window.
    const float em = heightL / 1.4f * kL;
    if (bold)
    {
        if (em <= 23.5f) return TextStyle::SmallBold;   // 22
        if (em <= 30.0f) return TextStyle::BodyBold;    // 26
        if (em <= 43.0f) return TextStyle::Button;      // 34
        if (em <= 54.0f) return TextStyle::Panel;       // 52
        if (em <= 76.0f) return TextStyle::Heading;     // 56 (ASCII)
        return TextStyle::Display;                      // 96 (ASCII)
    }
    if (em <= 21.0f) return TextStyle::Micro;          // 18
    if (em <= 26.0f) return TextStyle::Body;           // 24
    if (em <= 30.0f) return TextStyle::Sub;            // 28
    if (em <= 42.0f) return TextStyle::Status;         // 32
    return TextStyle::Panel;
}

//==============================================================================
void Paint::fill (Rect r, Rgba c, bool emissive)
{
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float xs[4] { r.x * kL, r.right() * kL, r.right() * kL, r.x * kL };
    const float ys[4] { r.y * kL, r.y * kL, r.bottom() * kL, r.bottom() * kL };
    const Rgba cs[4] { c, c, c, c };
    rr.colorQuad (xs, ys, cs, Blend::Over, emissive);
}

void Paint::fillRound (Rect r, float radius, Rgba c, bool emissive)
{
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    rr.fillRound (toDesign (r), radius * kL, c, Blend::Over, emissive);
}

void Paint::fillEllipse (Rect r, Rgba c, bool emissive)
{
    fillRound (r, std::min (r.w, r.h) * 0.5f, c, emissive);
}

void Paint::outlineRound (Rect r, float radius, float width, Rgba c, bool emissive)
{
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    radius = std::clamp (radius, 0.0f, std::min (r.w, r.h) * 0.5f);
    const float hw = width * 0.5f;
    const Rect o = reduced (r, hw, hw);
    const float rad = std::max (0.0f, radius - hw);
    std::vector<float> xs, ys;
    constexpr int kSeg = 6;
    auto corner = [&] (float cx, float cy, float a0)
    {
        for (int i = 0; i <= kSeg; ++i)
        {
            const float a = a0 + (float) i / (float) kSeg * 1.5707963f;
            xs.push_back (cx + std::cos (a) * rad);
            ys.push_back (cy + std::sin (a) * rad);
        }
    };
    corner (o.right() - rad, o.y + rad, -1.5707963f);
    corner (o.right() - rad, o.bottom() - rad, 0.0f);
    corner (o.x + rad, o.bottom() - rad, 1.5707963f);
    corner (o.x + rad, o.y + rad, 3.1415927f);
    xs.push_back (xs.front());
    ys.push_back (ys.front());
    polyline (xs, ys, width, c, emissive);
}

void Paint::outline (Rect r, float width, Rgba c)
{
    fill ({ r.x, r.y, r.w, width }, c);
    fill ({ r.x, r.bottom() - width, r.w, width }, c);
    fill ({ r.x, r.y + width, width, r.h - 2 * width }, c);
    fill ({ r.right() - width, r.y + width, width, r.h - 2 * width }, c);
}

void Paint::line (float x0, float y0, float x1, float y1, float width, Rgba c, bool emissive)
{
    const float xs[2] { x0 * kL, x1 * kL }, ys[2] { y0 * kL, y1 * kL };
    rr.polyline (xs, ys, 2, width * kL, c, Blend::Over, emissive);
}

void Paint::polyline (const std::vector<float>& xs, const std::vector<float>& ys, float width, Rgba c, bool emissive)
{
    const std::size_t n = std::min (xs.size(), ys.size());
    if (n < 2) return;
    std::vector<float> dx (n), dy (n);
    for (std::size_t i = 0; i < n; ++i) { dx[i] = xs[i] * kL; dy[i] = ys[i] * kL; }
    rr.polyline (dx.data(), dy.data(), (int) n, width * kL, c, Blend::Over, emissive);
}

void Paint::verticalGradient (Rect r, Rgba top, Rgba bottom, bool emissive)
{
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float xs[4] { r.x * kL, r.right() * kL, r.right() * kL, r.x * kL };
    const float ys[4] { r.y * kL, r.y * kL, r.bottom() * kL, r.bottom() * kL };
    const Rgba cs[4] { top, top, bottom, bottom };
    rr.colorQuad (xs, ys, cs, Blend::Over, emissive);
}

void Paint::horizontalGradient (Rect r, Rgba left, Rgba right, bool emissive)
{
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float xs[4] { r.x * kL, r.right() * kL, r.right() * kL, r.x * kL };
    const float ys[4] { r.y * kL, r.y * kL, r.bottom() * kL, r.bottom() * kL };
    const Rgba cs[4] { left, right, right, left };
    rr.colorQuad (xs, ys, cs, Blend::Over, emissive);
}

void Paint::quad (const float xs[4], const float ys[4], Rgba c, bool emissive)
{
    const Rgba cs[4] { c, c, c, c };
    quad4 (xs, ys, cs, emissive);
}

void Paint::quad4 (const float xs[4], const float ys[4], const Rgba cs[4], bool emissive)
{
    const float dx[4] { xs[0] * kL, xs[1] * kL, xs[2] * kL, xs[3] * kL };
    const float dy[4] { ys[0] * kL, ys[1] * kL, ys[2] * kL, ys[3] * kL };
    rr.colorQuad (dx, dy, cs, Blend::Over, emissive);
}

void Paint::triangle (float x0, float y0, float x1, float y1, float x2, float y2, Rgba c, bool emissive)
{
    const float xs[4] { x0, x1, x2, x2 }, ys[4] { y0, y1, y2, y2 };
    quad (xs, ys, c, emissive);
}

void Paint::arc (float cx, float cy, float radius, float width, float a0, float a1, Rgba c, bool emissive)
{
    rr.arc (cx * kL, cy * kL, radius * kL, width * kL, a0, a1, c, emissive);
}

void Paint::glow (Rect core, float radius, float spread, Rgba c)
{
    rr.glow (toDesign (core), radius * kL, spread * kL, c, true);
}

void Paint::sprite (hc::ui::Sprite s, Rect r, Rgba tint, bool add, bool emissive)
{
    rr.sprite (s, toDesign (r), tint, add ? Blend::Add : Blend::Over, emissive);
}

void Paint::spriteRotated (hc::ui::Sprite s, float cx, float cy, float w, float h, float angle, Rgba tint)
{
    rr.spriteRotated (s, cx * kL, cy * kL, w * kL, h * kL, angle, tint);
}

void Paint::nineSlice (hc::ui::Sprite s, Rect r, float cornerScale, Rgba tint, bool add)
{
    rr.nineSlice (s, toDesign (r), cornerScale, tint, add ? Blend::Add : Blend::Over);
}

void Paint::chassis9 (Rect src, float inset, Rect dst, float scale)
{
    const Rect d = toDesign (dst);
    const float c = std::min ({ inset * scale, d.w * 0.5f, d.h * 0.5f });
    const float si = c / std::max (1.0e-6f, scale);   // source pixels actually used for the corners
    const float sx[4] { src.x, src.x + si, src.right() - si, src.right() };
    const float sy[4] { src.y, src.y + si, src.bottom() - si, src.bottom() };
    const float dx[4] { d.x, d.x + c, d.right() - c, d.right() };
    const float dy[4] { d.y, d.y + c, d.bottom() - c, d.bottom() };
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
        {
            const Rect s2 { sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j] };
            const Rect d2 { dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j] };
            if (s2.w > 0.0f && s2.h > 0.0f && d2.w > 0.0f && d2.h > 0.0f) rr.chassis (s2, d2);
        }
}

void Paint::area (const std::vector<float>& xs, const std::vector<float>& ys, float baseY, Rgba top, Rgba bottom)
{
    const std::size_t n = std::min (xs.size(), ys.size());
    for (std::size_t i = 0; i + 1 < n; ++i)
    {
        const float qx[4] { xs[i], xs[i + 1], xs[i + 1], xs[i] };
        const float qy[4] { ys[i], ys[i + 1], baseY, baseY };
        const Rgba cs[4] { top, top, bottom, bottom };
        quad4 (qx, qy, cs);
    }
}

//==============================================================================
float Paint::textWidth (std::string_view s, float heightL, bool bold) const
{
    return rr.textWidth (styleFor (heightL, bold), s) / kL;
}

std::string Paint::fitText (std::string_view s, float width, float heightL, bool bold) const
{
    const auto st = styleFor (heightL, bold);
    const float w = width * kL;
    if (rr.textWidth (st, s) <= w) return std::string (s);
    std::string t (s);
    while (! t.empty())
    {
        // drop whole UTF-8 characters
        do t.pop_back(); while (! t.empty() && ((unsigned char) t.back() & 0xC0) == 0x80);
        if (! t.empty() && ((unsigned char) t.back() & 0xC0) == 0xC0) t.pop_back();
        if (rr.textWidth (st, t + "..") <= w) return t + "..";
    }
    return {};
}

void Paint::text (std::string_view s, Rect area, float heightL, bool bold, Rgba c, HAlign h, VAlign v, TextLook look)
{
    if (s.empty() || c.a <= 0.0f) return;
    const auto st = styleFor (heightL, bold);
    const std::string shown = fitText (s, area.w, heightL, bold);
    if (shown.empty()) return;
    const float cap = rr.capHeight (st);
    const Rect d = toDesign (area);
    float baseline = d.cy() + cap * 0.5f;
    if (v == VAlign::top) baseline = d.y + cap + (lineHeight (heightL) * kL - cap) * 0.5f;
    else if (v == VAlign::bottom) baseline = d.bottom() - (lineHeight (heightL) * kL - cap) * 0.5f;
    float x = d.x;
    auto align = hc::ui::Align::Left;
    if (h == HAlign::centre) { x = d.cx(); align = hc::ui::Align::Center; }
    else if (h == HAlign::right) { x = d.right(); align = hc::ui::Align::Right; }
    hc::ui::TextFx fx = hc::ui::TextFx::Plain;
    if (look == TextLook::engraved) fx = hc::ui::TextFx::Engraved;
    else if (look == TextLook::shadowed) fx = hc::ui::TextFx::Shadowed;
    else if (look == TextLook::neon) fx = hc::ui::TextFx::NeonCrisp;
    rr.text (st, shown, x, baseline, c, align, fx);
}

void Paint::engraved (std::string_view s, Rect area, float heightL, HAlign h, VAlign v)
{
    text (s, area, heightL, true, colours::ink, h, v, TextLook::engraved);
}

void Paint::panelText (std::string_view s, Rect area, float heightL, HAlign h, VAlign v, Rgba c)
{
    text (s, area, heightL, true, c, h, v, TextLook::shadowed);
}

std::vector<std::string> Paint::wrapLines (std::string_view s, float width, float heightL, bool bold) const
{
    std::vector<std::string> lines;
    const auto st = styleFor (heightL, bold);
    const float w = width * kL;
    std::size_t start = 0;
    while (start <= s.size())
    {
        const std::size_t nl = s.find ('\n', start);
        const std::string_view para = s.substr (start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        std::string line;
        std::size_t i = 0;
        while (i < para.size())
        {
            const std::size_t sp = para.find (' ', i);
            const std::string_view word = para.substr (i, sp == std::string_view::npos ? std::string_view::npos : sp - i);
            const std::string tryLine = line.empty() ? std::string (word) : line + " " + std::string (word);
            if (! line.empty() && rr.textWidth (st, tryLine) > w)
            {
                lines.push_back (line);
                line = std::string (word);
            }
            else
                line = tryLine;
            if (sp == std::string_view::npos) break;
            i = sp + 1;
        }
        lines.push_back (line);
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return lines;
}

void Paint::wrapped (std::string_view s, Rect area, float heightL, bool bold, Rgba c, int maxLines, HAlign h, VAlign v)
{
    auto lines = wrapLines (s, area.w, heightL, bold);
    if ((int) lines.size() > maxLines && maxLines > 0)
    {
        lines.resize ((std::size_t) maxLines);
        const std::string cut = fitText (lines.back(), area.w - textWidth ("..", heightL, bold), heightL, bold);
        lines.back() = (cut.size() >= 2 && cut.ends_with ("..") ? cut.substr (0, cut.size() - 2) : cut) + "..";
    }
    const float lh = lineHeight (heightL);
    const float total = lh * (float) lines.size();
    float y = area.y;
    if (v == VAlign::middle) y = area.y + (area.h - total) * 0.5f;
    else if (v == VAlign::bottom) y = area.bottom() - total;
    for (const auto& l : lines)
    {
        if (y + lh > area.bottom() + lh * 0.25f) break;
        text (l, { area.x, y, area.w, lh }, heightL, bold, c, h, VAlign::middle);
        y += lh;
    }
}

void Paint::pushClip (Rect r) { rr.pushClip (toDesign (r)); }
void Paint::popClip() { rr.popClip(); }
} // namespace hem::app
