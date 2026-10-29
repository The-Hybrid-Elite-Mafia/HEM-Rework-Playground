#include "Ui.hpp"

#include <cmath>

namespace hem::app
{
using hc::ui::Sprite;

namespace
{
    constexpr float kPi = 3.14159265358979f;
    constexpr float kKnobMin = -0.75f * kPi, kKnobMax = 0.75f * kPi;   // -135 .. +135 degrees, 0 = up
    constexpr float kDragForFullRange = 250.0f;                          // L units of mouse travel
    constexpr float kFine = 0.15f;
    const Rgba kMenuBg = argb (0xff1b1f26);

    // 0.10.0's plate piece of the main faceplate (brushed plate in a bezel)
    constexpr Rect kPlateSrc { 95, 240, 1117, 209 };
    constexpr float kPlateInset = 60.0f;
}

Id makeId (std::string_view text, std::uint64_t salt) noexcept
{
    std::uint64_t h = 0xcbf29ce484222325ull ^ (salt * 0x9E3779B97F4A7C15ull);
    for (const char c : text) { h ^= (unsigned char) c; h *= 0x100000001B3ull; }
    return h == 0 ? 1 : h;
}

//==============================================================================
void Ui::beginFrame (const Input& input, const hc::ui::Canvas& canvas, double seconds)
{
    in = input;
    cv = canvas;
    time = seconds;
    pressClaimed = rightClaimed = wheelUsed = false;
    keyUsed.assign (in.keys.size(), false);
    for (auto& o : pendingOverlays) overlays.push_back (std::move (o));
    pendingOverlays.clear();
    blocked = menu != nullptr || ! overlays.empty();
    lastHot = hot;
    hot = 0;
    if (active != 0 && ! in.down && ! in.released) active = 0;   // the control that had the mouse is gone
    layer = 0;
    occluders.clear();
    tipSeen = false;
    tipText.clear();
}

Rect Ui::screen() const noexcept
{
    const float top = -cv.offsetY / cv.scale;
    return { cv.visibleLeft() / kL, top / kL, (cv.visibleRight() - cv.visibleLeft()) / kL, cv.pixelH / cv.scale / kL };
}

Rect Ui::toPixels (Rect l) const noexcept
{
    const float x0 = cv.toPixelX (l.x * kL), y0 = cv.toPixelY (l.y * kL);
    const float x1 = cv.toPixelX (l.right() * kL), y1 = cv.toPixelY (l.bottom() * kL);
    return { std::round (x0), std::round (y0), std::round (x1 - x0), std::round (y1 - y0) };
}

void Ui::endFrame()
{
    // modal overlays: only the top one is live
    for (std::size_t i = 0; i < overlays.size();)
    {
        const bool top = i + 1 == overlays.size();
        blocked = ! top || menu != nullptr;
        layer = (int) i + 1;
        occluders.push_back ({ layer, toPixels (screen()) });
        auto keep = overlays[i] (*this);
        if (! keep) overlays.erase (overlays.begin() + (std::ptrdiff_t) i);
        else ++i;
    }
    blocked = false;
    layer = (int) overlays.size() + 1;
    if (menu != nullptr) drawMenu();
    if (! tipSeen) tipCandidate.clear();
    drawTooltip();
    if (textFields != nullptr) textFields->endFrame (occluders);
    blocked = menu != nullptr || ! overlays.empty() || ! pendingOverlays.empty();
}

//==============================================================================
Ui::Press Ui::behave (Id id, Rect r, bool allowed)
{
    Press res;
    const bool live = enabled() && allowed;
    if (live && r.contains (in.x, in.y) && (active == 0 || active == id)) { hot = id; res.hover = true; }
    if (live && in.pressed && ! pressClaimed && r.contains (in.pressX, in.pressY))
    {
        claim (id);
        res.doubleClick = in.doubleClick;
        res.rx = in.pressX; res.ry = in.pressY;
    }
    if (active == id)
    {
        res.held = in.down;
        if (in.released || ! in.down)
        {
            if (live && r.contains (in.x, in.y)) { res.clicked = true; res.rx = in.x; res.ry = in.y; }
            active = 0;
        }
    }
    if (live && in.rightPressed && ! rightClaimed && r.contains (in.rightX, in.rightY))
    {
        rightClaimed = true;
        res.right = true;
        res.rx = in.rightX; res.ry = in.rightY;
    }
    return res;
}

Ui::Drag Ui::drag (Id id, Rect r, bool allowed)
{
    Drag d;
    const bool live = enabled() && allowed;
    if (live && r.contains (in.x, in.y) && (active == 0 || active == id)) { hot = id; d.hover = true; }
    if (live && in.pressed && ! pressClaimed && r.contains (in.pressX, in.pressY))
    {
        claim (id);
        dragStartX = lastX = in.pressX;
        dragStartY = lastY = in.pressY;
        dragMoved = false;
        d.started = true;
        d.doubleClick = in.doubleClick;
    }
    if (active == id)
    {
        hot = id;
        d.dragging = true;
        d.x = in.x; d.y = in.y;
        d.startX = dragStartX; d.startY = dragStartY;
        d.dx = in.x - dragStartX; d.dy = in.y - dragStartY;
        d.stepX = in.x - lastX; d.stepY = in.y - lastY;
        lastX = in.x; lastY = in.y;
        if (std::abs (d.dx) > 3.0f || std::abs (d.dy) > 3.0f) dragMoved = true;
        d.moved = dragMoved;
        if (in.released || ! in.down) { d.ended = true; active = 0; }
    }
    if (live && in.rightPressed && ! rightClaimed && r.contains (in.rightX, in.rightY)) { rightClaimed = true; d.right = true; }
    return d;
}

bool Ui::unclaimedPress (Rect r) noexcept
{
    if (! enabled() || ! in.pressed || pressClaimed || ! r.contains (in.pressX, in.pressY)) return false;
    pressClaimed = true;
    return true;
}

bool Ui::unclaimedRight (Rect r) noexcept
{
    if (! enabled() || ! in.rightPressed || rightClaimed || ! r.contains (in.rightX, in.rightY)) return false;
    rightClaimed = true;
    return true;
}

float Ui::wheel (Rect r) noexcept
{
    if (! enabled() || wheelUsed || in.wheel == 0.0f || ! r.contains (in.x, in.y)) return 0.0f;
    wheelUsed = true;
    return in.wheel;
}

bool Ui::key (std::uint32_t vk, bool ctrl, bool shift) noexcept
{
    if (! enabled()) return false;
    for (std::size_t i = 0; i < in.keys.size(); ++i)
        if (! keyUsed[i] && in.keys[i].vk == vk && in.keys[i].ctrl == ctrl && in.keys[i].shift == shift)
        {
            keyUsed[i] = true;
            return true;
        }
    return false;
}

std::vector<std::filesystem::path> Ui::takeDrop (Rect r) noexcept
{
    if (in.dropped.empty() || ! enabled() || ! r.contains (in.dropX, in.dropY)) return {};
    auto files = std::move (in.dropped);
    in.dropped.clear();
    return files;
}

//==============================================================================
bool Ui::button (Id id, Rect r, std::string_view text, ButtonLook look, Rgba accent, bool en, bool lit, float textHeight)
{
    const auto pr = behave (id, r, en);
    const bool down = pr.held && r.contains (in.x, in.y);
    const float th = textHeight > 0.0f ? textHeight : std::min (17.0f, r.h * 0.42f);
    const float fade = en ? 1.0f : 0.4f;
    switch (look)
    {
        case ButtonLook::plate:
        {
            p.chassis9 (kPlateSrc, kPlateInset, r, r.h * kL / kPlateSrc.h);
            if (lit) p.fillRound (reduced (r, r.h * 0.16f), r.h * 0.12f, alpha (accent, 0.28f), true);
            if (pr.hover || down) p.fillRound (reduced (r, r.h * 0.16f), r.h * 0.12f, down ? alpha (accent, 0.25f) : alpha (colours::white, 0.15f));
            if (text == "<" || text == ">")
            {
                const float a = r.h * 0.13f, dir = text == "<" ? -1.0f : 1.0f, cx = r.cx(), cy = r.cy();
                p.triangle (cx + dir * a, cy + 1, cx - dir * a * 0.8f, cy - a + 1, cx - dir * a * 0.8f, cy + a + 1, alpha (colours::white, 0.6f * fade));
                p.triangle (cx + dir * a, cy, cx - dir * a * 0.8f, cy - a, cx - dir * a * 0.8f, cy + a, alpha (colours::ink, fade));
            }
            else
                p.text (text, reduced (r, r.h * 0.25f, 0), th, true, alpha (colours::ink, fade), HAlign::centre, VAlign::middle, TextLook::engraved);
            break;
        }
        case ButtonLook::dark:
        {
            p.fillRound (reduced (r, 0.5f), 3.0f, lit ? alpha (accent, fade) : alpha (kMenuBg, fade));
            p.outlineRound (reduced (r, 0.5f), 3.0f, 1.0f, lit ? brighter (accent, 0.4f) : alpha (colours::white, pr.hover ? 0.35f : 0.15f));
            if (lit) p.glow (r, 3.0f, 4.0f, alpha (accent, 0.35f));
            if (down) p.fillRound (r, 3.0f, alpha (colours::black, 0.2f));
            p.text (text, reduced (r, 2, 0), textHeight > 0.0f ? textHeight : std::min (14.0f, r.h * 0.6f), true,
                    alpha (lit ? colours::ink : colours::paper, fade), HAlign::centre);
            break;
        }
        case ButtonLook::tab:
        {
            const Rect inner = reduced (r, r.h * 0.2f, r.h * 0.2f);
            if (lit)
            {
                p.fillRound (inner, inner.h * 0.3f, alpha (accent, 0.22f), true);
                p.outlineRound (inner, inner.h * 0.3f, std::max (1.5f, r.h * 0.04f), alpha (accent, 0.9f), true);
            }
            else if (pr.hover || down)
                p.fillRound (inner, inner.h * 0.3f, alpha (colours::white, down ? 0.12f : 0.07f));
            p.panelText (text, r, th, HAlign::centre, VAlign::middle, lit ? colours::white : colours::paper);
            break;
        }
        case ButtonLook::chrome:
            return capButton (id, r, text, lit, accent, en) || pr.clicked;
        case ButtonLook::flat:
            if (pr.hover) p.fillRound (r, 4.0f, alpha (colours::white, 0.08f));
            p.text (text, reduced (r, 4, 0), th, true, alpha (lit ? accent : colours::paper, fade), HAlign::centre);
            break;
    }
    return pr.clicked && en;
}

bool Ui::capButton (Id id, Rect b, std::string_view caption, bool on, Rgba lit, bool en)
{
    const auto pr = behave (id, b, en);
    const bool pressed = pr.held && b.contains (in.x, in.y);
    const bool down = on || pressed;
    const float push = down ? 0.6f : 0.0f;
    const float sx = down ? 0.02f : 0.07f, sy = down ? 0.03f : 0.12f, grow = down ? 1.04f : 1.16f;
    p.sprite (Sprite::ButtonShadow, { b.cx() - b.w * grow * 0.5f * 96.0f / 68.0f + b.w * sx, b.cy() - b.h * grow * 0.5f * 72.0f / 44.0f + b.h * sy,
                                      b.w * grow * 96.0f / 68.0f, b.h * grow * 72.0f / 44.0f }, { 1, 1, 1, down ? 0.55f : 0.9f });
    const Rect cap { b.x - b.w * (128.0f / 116.0f - 1.0f) * 0.5f + push, b.y - b.h * (96.0f / 84.0f - 1.0f) * 0.5f + push,
                     b.w * 128.0f / 116.0f, b.h * 96.0f / 84.0f };
    p.sprite (Sprite::ButtonCap, cap, down ? Rgba { 0.86f, 0.86f, 0.86f, 1 } : Rgba { 1, 1, 1, en ? 1.0f : 0.6f });
    const float th = std::min (caption.size() <= 2 ? b.h * 0.55f : b.h * 0.42f, 18.0f);
    const Rect tr = b.offset (push, push);
    if (on)
    {
        p.sprite (Sprite::ButtonLens, cap, alpha (lit, 0.95f), false, true);
        p.sprite (Sprite::ButtonHalo, { b.cx() - b.w * 0.69f + push, b.cy() - b.h * 0.76f + push, b.w * 1.38f, b.h * 1.52f }, alpha (lit, 0.7f), true, true);
        p.text (caption, tr, th, true, colours::white, HAlign::centre);
    }
    else
        p.text (caption, tr, th, true, alpha (argb (0xff14161a), en ? 1.0f : 0.5f), HAlign::centre, VAlign::middle, TextLook::engraved);
    if (pr.hover && ! on) p.fillRound (reduced (b, 1.0f), 3.0f, alpha (colours::white, 0.06f));
    return pr.clicked && en;
}

bool Ui::ledToggle (Id id, Rect r, bool& on, std::string_view label, Rgba accent, bool onChrome, bool en)
{
    const auto pr = behave (id, r, en);
    const float d = r.h;
    const Rect sock { r.x, r.y, d, d };
    p.sprite (Sprite::Socket, sock, { 1, 1, 1, en ? 1.0f : 0.6f });
    led (sock.cx(), sock.cy(), d * 0.2f, accent, on);
    if (pr.hover) p.fillEllipse (reduced (sock, d * 0.12f), alpha (colours::white, 0.08f));
    if (! label.empty())
    {
        const Rect tr { r.x + d * 1.15f, r.y, r.w - d * 1.15f, r.h };
        const float th = std::min (16.0f, d * 0.45f);
        if (onChrome) p.text (label, tr, th, true, alpha (colours::ink, en ? 1.0f : 0.5f), HAlign::left, VAlign::middle, TextLook::engraved);
        else p.panelText (label, tr, th, HAlign::left, VAlign::middle, alpha (colours::paper, en ? 1.0f : 0.45f));
    }
    if (pr.clicked && en) { on = ! on; return true; }
    return false;
}

void Ui::led (float cx, float cy, float radius, Rgba colour, bool on)
{
    if (on) p.sprite (Sprite::GlowDot, { cx - radius * 3.2f, cy - radius * 3.2f, radius * 6.4f, radius * 6.4f }, alpha (colour, 0.8f), true, true);
    p.sprite (Sprite::Lamp, { cx - radius, cy - radius, radius * 2, radius * 2 }, on ? colour : mix (darker (colour, 2.5f), argb (0xff101214), 0.6f),
              false, on);
}

bool Ui::knob (Id id, Rect r, float& norm, float resetTo, Rgba accent, bool bipolar, float zeroNorm, int* gesture, bool socket, bool en)
{
    const float D = std::min (r.w, r.h), R = D * 0.5f, cx = r.cx(), cy = r.cy();
    const Rect sq { cx - R, cy - R, D, D };
    auto d = drag (id, sq, en);
    bool changed = false;
    const float before = norm;
    if (d.started && gesture) *gesture = 1;
    if (d.started && d.doubleClick) norm = resetTo;
    else if (d.dragging && ! d.started)
        norm = std::clamp (norm - d.stepY / kDragForFullRange * (in.ctrl || in.shift ? kFine : 1.0f), 0.0f, 1.0f);
    if (d.ended && gesture) *gesture = -1;
    if (const float w = en && active == 0 ? wheel (sq) : 0.0f; w != 0.0f)
        norm = std::clamp (norm + w * (in.ctrl || in.shift ? 0.005f : 0.03f), 0.0f, 1.0f);
    changed = norm != before;

    const bool hotNow = d.hover || d.dragging;
    if (socket) p.sprite (Sprite::Socket, sq, { 1, 1, 1, en ? 1.0f : 0.6f });
    const float ang = kKnobMin + (kKnobMax - kKnobMin) * norm;
    const float a0 = kKnobMin + (kKnobMax - kKnobMin) * (bipolar ? zeroNorm : 0.0f);
    const float ar = 0.745f * R, aw = 0.085f * R;
    p.arc (cx, cy, ar, aw, kKnobMin, kKnobMax, alpha (accent, 0.30f), false);
    if (std::abs (ang - a0) > 0.004f)
    {
        p.arc (cx, cy, ar, aw, std::min (a0, ang), std::max (a0, ang), alpha (accent, en ? 1.0f : 0.5f), en);
        const float tx = cx + std::sin (ang) * ar, ty = cy - std::cos (ang) * ar;
        p.sprite (Sprite::GlowDot, { tx - aw * 2.2f, ty - aw * 2.2f, aw * 4.4f, aw * 4.4f }, alpha (accent, hotNow ? 1.0f : 0.8f), true, en);
    }
    const float bodyR = 0.665f * R;
    p.sprite (Sprite::KnobShadow, { cx - bodyR * 1.12f + 0.05f * bodyR, cy - bodyR * 1.12f + 0.09f * bodyR, bodyR * 2.24f, bodyR * 2.24f });
    const float bd = bodyR * 2.0f * 256.0f / 254.0f;
    p.spriteRotated (Sprite::KnobBody, cx, cy, bd, bd, ang);
    const float len = (0.62f - 0.20f) * bodyR, mid = 0.5f * (0.62f + 0.20f) * bodyR;
    const float ih = len * 96.0f / 72.0f, iw = ih * 24.0f / 96.0f;
    const Rgba ind { 0.5f + 0.5f * accent.r, 0.5f + 0.5f * accent.g, 0.5f + 0.5f * accent.b, 1.0f };
    p.spriteRotated (Sprite::KnobIndicator, cx + std::sin (ang) * mid, cy - std::cos (ang) * mid, iw, ih, ang, ind);
    p.sprite (Sprite::KnobLight, { cx - bd / 2, cy - bd / 2, bd, bd });
    if (hotNow) p.fillEllipse (reduced (sq, R * 0.3f), alpha (colours::white, 0.04f));
    return changed;
}

bool Ui::fader (Id id, Rect b, float& norm, float resetTo, Rgba accent, const std::vector<float>& ticks, float zeroTick, int* gesture)
{
    const float capH = std::clamp (b.w * 0.62f, 16.0f, 34.0f), capW = b.w * 0.86f;
    const float travel = std::max (1.0f, b.h - capH);
    auto posOf = [&] (float n) { return b.y + capH * 0.5f + (1.0f - n) * travel; };
    auto d = drag (subId (id, 1), b);
    const float before = norm;
    if (d.started && gesture) *gesture = 1;
    if (d.started && d.doubleClick) norm = resetTo;
    else if (d.started)
    {
        // click on the slot (not the cap): jump there, like 0.10.0's linear sliders
        const float cy = posOf (norm);
        if (std::abs (d.startY - cy) > capH * 0.6f) norm = std::clamp (1.0f - (d.startY - b.y - capH * 0.5f) / travel, 0.0f, 1.0f);
    }
    else if (d.dragging)
        norm = std::clamp (norm - d.stepY / travel * (in.ctrl || in.shift ? kFine : 1.0f), 0.0f, 1.0f);
    if (d.ended && gesture) *gesture = -1;
    if (const float w = active == 0 ? wheel (b) : 0.0f; w != 0.0f)
        norm = std::clamp (norm + w * (in.ctrl || in.shift ? 0.004f : 0.02f), 0.0f, 1.0f);

    const float pos = posOf (norm);
    const float slotW = std::max (4.0f, b.w * 0.12f);
    const Rect slot { b.cx() - slotW * 0.5f, b.y, slotW, b.h };
    p.fillRound (slot, slotW * 0.5f, colours::black);
    p.fillRound ({ slot.x, pos, slot.w, slot.bottom() - pos }, slotW * 0.5f, alpha (accent, 0.55f), true);
    for (const float t : ticks)
    {
        const float ty = posOf (t);
        const bool z = std::abs (t - zeroTick) < 1.0e-4f;
        const float tw = z ? b.w * 0.42f : b.w * 0.26f;
        p.fill ({ b.cx() - tw, ty, tw * 2.0f, z ? 2.0f : 1.0f }, alpha (colours::black, 0.45f));
    }
    const float cw = capW * 256.0f / 248.0f, ch = capH * 136.0f / 124.0f;
    p.sprite (Sprite::FaderShadow, { b.cx() - cw * 0.62f + cw * 0.06f, pos - ch * 0.75f + ch * 0.16f, cw * 1.24f, ch * 1.5f }, { 1, 1, 1, 0.95f });
    p.sprite (Sprite::FaderCap, { b.cx() - cw / 2, pos - ch / 2, cw, ch });
    p.sprite (Sprite::FaderLine, { b.cx() - capW * 0.43f, pos - capH * 0.085f, capW * 0.86f, capH * 0.17f }, accent, false, true);
    if (d.hover || d.dragging) p.fillRound ({ b.cx() - cw / 2, pos - ch / 2, cw, ch }, 3.0f, alpha (colours::white, 0.06f));
    return norm != before;
}

bool Ui::hslider (Id id, Rect b, float& norm, Rgba accent, int* gesture, bool en)
{
    auto d = drag (id, b, en);
    const float before = norm;
    const float r = std::min (b.h * 0.42f, 9.0f);
    const float x0 = b.x + r, w = std::max (1.0f, b.w - 2 * r);
    if (d.started && gesture) *gesture = 1;
    if (d.dragging) norm = std::clamp ((d.x - x0) / w, 0.0f, 1.0f);
    if (d.ended && gesture) *gesture = -1;
    const float h = std::max (3.0f, b.h * 0.18f);
    const Rect track { b.x, b.cy() - h * 0.5f, b.w, h };
    p.fillRound (track, h * 0.5f, colours::black);
    const float px = x0 + w * norm;
    p.fillRound ({ track.x, track.y, px - track.x, track.h }, h * 0.5f, alpha (accent, en ? 1.0f : 0.4f), en);
    p.fillEllipse ({ px - r, b.cy() - r, r * 2, r * 2 }, en ? colours::paper : colours::paperDim);
    p.outlineRound ({ px - r, b.cy() - r, r * 2, r * 2 }, r, 1.0f, colours::ink);
    return norm != before;
}

bool Ui::combo (Id id, Rect r, const std::vector<std::string>& items, int& index, Rgba accent, bool plate, bool en,
                std::string_view emptyText, float textHeight)
{
    bool changed = false;
    if (auto it = pickedIndex.find (id); it != pickedIndex.end())
    {
        if (it->second >= 0 && it->second < (int) items.size() && it->second != index) { index = it->second; changed = true; }
        pickedIndex.erase (it);
    }
    const auto pr = behave (id, r, en);
    const float fade = en ? 1.0f : 0.4f;
    if (plate) p.chassis9 (kPlateSrc, kPlateInset, r, r.h * kL / kPlateSrc.h);
    else
    {
        p.fillRound (reduced (r, 0.5f), 3.0f, colours::well);
        p.outlineRound (reduced (r, 0.5f), 3.0f, 1.0f, alpha (accent, pr.hover || isActive (id) ? 0.8f : 0.35f));
    }
    const std::string shown = index >= 0 && index < (int) items.size() ? items[(std::size_t) index] : std::string (emptyText);
    const float th = textHeight > 0.0f ? textHeight : std::min (plate ? 18.0f : 14.0f, r.h * 0.5f);
    const float inset = plate ? r.h * 0.3f : 5.0f;
    const Rect tr { r.x + inset, r.y, r.w - inset - r.h, r.h };
    if (plate) p.text (shown, tr, th, true, alpha (colours::ink, fade), HAlign::left, VAlign::middle, TextLook::engraved);
    else p.text (shown, tr, th, true, alpha (colours::paper, fade));
    const float a = r.h * 0.16f, ax = r.right() - r.h * 0.5f, ay = r.cy();
    p.triangle (ax - a, ay - a * 0.5f, ax + a, ay - a * 0.5f, ax, ay + a * 0.7f, alpha (plate ? colours::ink : colours::paperDim, 0.85f * fade));
    if (pr.clicked && en)
    {
        std::vector<MenuItem> m;
        for (int i = 0; i < (int) items.size(); ++i) m.push_back (MenuItem::item (items[(std::size_t) i], i + 1, true, i == index));
        openMenu (std::move (m), r.x, r.bottom() + 2.0f, [this, id] (int picked) { pickedIndex[id] = picked - 1; }, r.w);
    }
    return changed;
}

bool Ui::textField (Id id, Rect r, std::string& text, const FieldStyle& style)
{
    if (textFields == nullptr) return false;
    FieldStyle s = style;
    s.enabled = style.enabled && ! blocked;
    s.pixelsPerL = cv.scale * kL;
    // the box is a real window: give it its own dark well and accent outline underneath
    p.fillRound (expanded (r, 1.5f, 1.5f), 3.0f, alpha (style.accent, 0.45f));
    p.fill (r, colours::well);
    return textFields->field (id, toPixels (r), text, s, layer);
}

void Ui::progress (Rect r, float fraction, Rgba colour)
{
    p.fillRound (r, std::min (4.0f, r.h * 0.5f), colours::black);
    if (fraction > 0.0f)
        p.fillRound ({ r.x, r.y, r.w * std::clamp (fraction, 0.0f, 1.0f), r.h }, std::min (4.0f, r.h * 0.5f), colour, true);
}

void Ui::tooltip (Rect r, std::string_view text)
{
    if (text.empty() || ! hovering (r) || active != 0 || in.down) return;
    tipSeen = true;
    if (tipCandidate != text) { tipCandidate = std::string (text); tipSince = time; return; }
    if (time - tipSince >= 0.6) { tipText = tipCandidate; tipRect = r; }
}

void Ui::drawTooltip()
{
    if (tipText.empty()) return;
    const Rect s = screen();
    const float th = 13.0f, maxW = 360.0f;
    auto lines = p.wrapLines (tipText, maxW, th, false);
    float w = 0.0f;
    for (auto& l : lines) w = std::max (w, p.textWidth (l, th, false));
    w += 16.0f;
    const float h = p.lineHeight (th) * (float) lines.size() + 10.0f;
    float x = in.x + 12.0f, y = in.y + 20.0f;
    if (x + w > s.right() - 4) x = s.right() - 4 - w;
    if (y + h > s.bottom() - 4) y = in.y - h - 8.0f;
    const Rect box { x, y, w, h };
    p.fillRound (expanded (box, 1, 1), 5.0f, alpha (colours::glow, 0.5f));
    p.fillRound (box, 4.0f, kMenuBg);
    p.wrapped (tipText, reduced (box, 8, 5), th, false, colours::paper, 20);
    occluders.push_back ({ layer, toPixels (box) });
}

//==============================================================================
void Ui::openMenu (std::vector<MenuItem> items, float x, float y, MenuResult onPick, float minWidth)
{
    menu = std::make_unique<MenuState>();
    menu->items = std::move (items);
    menu->x = x; menu->y = y; menu->minWidth = minWidth;
    menu->onPick = std::move (onPick);
    menu->openedAt = time;
    pressClaimed = true;   // the click that opened it is used up
}

void Ui::closeMenu() noexcept { menu.reset(); }

void Ui::pushOverlay (Overlay o) { pendingOverlays.push_back (std::move (o)); }

namespace
{
    constexpr float kItemH = 26.0f, kHeaderH = 22.0f, kSepH = 9.0f, kMenuText = 15.0f;
    float itemHeight (const MenuItem& m) noexcept { return m.separator ? kSepH : (m.header ? kHeaderH : kItemH); }
}

bool Ui::menuLevel (const std::vector<MenuItem>& items, float x, float y, float minW, int depth, int& pickedId, bool& inside)
{
    float w = minW;
    for (const auto& m : items) w = std::max (w, p.textWidth (m.text, kMenuText, false) + 64.0f);
    float h = 8.0f;
    for (const auto& m : items) h += itemHeight (m);
    const Rect s = screen();
    if (x + w > s.right() - 4) x = std::max (s.x + 4, s.right() - 4 - w);
    if (y + h > s.bottom() - 4) y = std::max (s.y + 4, s.bottom() - 4 - h);
    const Rect box { x, y, w, h };
    p.fillRound (expanded (box, 6, 6), 10.0f, alpha (colours::black, 0.35f));
    p.fillRound (box, 5.0f, kMenuBg);
    p.outlineRound (box, 5.0f, 1.0f, alpha (colours::glow, 0.35f));
    occluders.push_back ({ layer, toPixels (box) });
    if (box.contains (in.x, in.y)) inside = true;

    float iy = y + 4.0f;
    int subIndex = -1;
    Rect subRow {};
    for (int i = 0; i < (int) items.size(); ++i)
    {
        const auto& m = items[(std::size_t) i];
        const Rect row { x + 4, iy, w - 8, itemHeight (m) };
        iy += row.h;
        if (m.separator) { p.fill ({ row.x + 6, row.cy(), row.w - 12, 1.0f }, alpha (colours::white, 0.12f)); continue; }
        if (m.header) { p.text (m.text, reduced (row, 10, 0), 13.0f, true, colours::paperDim); continue; }
        const bool hov = row.contains (in.x, in.y) && m.enabled;
        if (hov) p.fillRound (row, 3.0f, alpha (colours::glow, 0.35f));
        float tx = row.x + 26.0f;
        if (m.colour.a > 0.0f)
            p.fillRound ({ row.x + 8, row.cy() - 5, 10, 10 }, 2.0f, m.colour);
        if (m.checked)
        {
            const float cx = row.x + 13, cy = row.cy();
            p.line (cx - 5, cy, cx - 1.5f, cy + 4, 2.0f, colours::paper);
            p.line (cx - 1.5f, cy + 4, cx + 5, cy - 5, 2.0f, colours::paper);
        }
        p.text (m.text, { tx, row.y, row.right() - tx - 16, row.h }, kMenuText, false,
                m.enabled ? (hov ? colours::white : colours::paper) : alpha (colours::paperDim, 0.45f));
        if (! m.sub.empty())
        {
            const float ax = row.right() - 10, ay = row.cy();
            p.triangle (ax - 3, ay - 5, ax - 3, ay + 5, ax + 3, ay, colours::paperDim);
            if (hov) { subIndex = i; subRow = row; }
        }
        if (hov && m.sub.empty() && in.released && time - menu->openedAt > 0.15) pickedId = m.id;
    }
    // a submenu stays open while the mouse is on its row or inside it
    if (depth == 0)
    {
        if (subIndex >= 0) menu->openSub = subIndex;
        if (menu->openSub >= 0 && menu->openSub < (int) items.size() && ! items[(std::size_t) menu->openSub].sub.empty())
        {
            float sy = y + 4.0f;
            for (int i = 0; i < menu->openSub; ++i) sy += itemHeight (items[(std::size_t) i]);
            bool subInside = false;
            menuLevel (items[(std::size_t) menu->openSub].sub, x + w - 4, sy - 4, 0.0f, 1, pickedId, subInside);
            if (subInside) inside = true;
            else if (subIndex < 0 && box.contains (in.x, in.y)) menu->openSub = -1;
        }
    }
    (void) subRow;
    return inside;
}

void Ui::drawMenu()
{
    int picked = 0;
    bool inside = false;
    menuLevel (menu->items, menu->x, menu->y, menu->minWidth, 0, picked, inside);
    if (picked != 0)
    {
        auto cb = std::move (menu->onPick);
        menu.reset();
        if (cb) cb (picked);
        return;
    }
    const bool escape = [this] { for (auto& k : in.keys) if (k.vk == 0x1B) return true; return false; }();
    if (escape || ((in.pressed || in.rightPressed) && ! inside && time - menu->openedAt > 0.05))
        menu.reset();
}
} // namespace hem::app
