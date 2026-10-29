#pragma once
// =============================================================================
//  Paint — drawing for the Playground pages on The Hybrid Core renderer.
//
//  Pages are laid out in "L" units: the pixels of the old 1600 x 900 window. One L unit is
//  2.4 design units of the 3840 x 2160 faceplate, so the numbers of the 0.10.0 layout carry over
//  and the whole screen still scales with the window (the renderer maps design units to pixels).
//  Text sizes are given the way the old app gave them (font height in L units) and land on the
//  nearest Poppins style the renderer has baked.
// =============================================================================
#include "hybrid/ui/Renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hem::app
{
using hc::ui::Rect;
using hc::ui::Rgba;

inline constexpr float kL = 2.4f;   // design units per L unit

//==============================================================================
// colours (ARGB like 0.10.0)
Rgba argb (std::uint32_t c) noexcept;
Rgba brighter (Rgba c, float amount) noexcept;
Rgba darker (Rgba c, float amount) noexcept;
Rgba mix (Rgba a, Rgba b, float t) noexcept;
inline Rgba alpha (Rgba c, float a) noexcept { return { c.r, c.g, c.b, a }; }

namespace colours
{
    inline const Rgba ink      = argb (0xff15181d);
    inline const Rgba inkSoft  = argb (0xff3b4048);
    inline const Rgba paper    = argb (0xfff4f6f8);
    inline const Rgba paperDim = argb (0xffc4c9d0);
    inline const Rgba navy     = argb (0xff05070d);
    inline const Rgba glow     = argb (0xff2f8bff);   // HEM blue
    inline const Rgba hemGreen = argb (0xff35e06a);
    inline const Rgba meterRed = argb (0xffff4a3d);
    inline const Rgba amber    = argb (0xffffb02e);
    inline const Rgba gold     = argb (0xffcfa63a);
    inline const Rgba panel    = argb (0xff12151a);
    inline const Rgba well     = argb (0xff0d1015);
    inline const Rgba black    = argb (0xff0b0d10);
    inline const Rgba white    = argb (0xffffffff);
}

//==============================================================================
// rectangles (L units)
Rect takeTop (Rect& r, float h) noexcept;
Rect takeBottom (Rect& r, float h) noexcept;
Rect takeLeft (Rect& r, float w) noexcept;
Rect takeRight (Rect& r, float w) noexcept;
Rect reduced (Rect r, float dx, float dy) noexcept;
inline Rect reduced (Rect r, float d) noexcept { return reduced (r, d, d); }
Rect expanded (Rect r, float dx, float dy) noexcept;
Rect centred (Rect r, float w, float h) noexcept;
Rect withHeight (Rect r, float h) noexcept;
Rect withWidth (Rect r, float w) noexcept;
Rect trimTop (Rect r, float d) noexcept;
Rect trimBottom (Rect r, float d) noexcept;
Rect trimLeft (Rect r, float d) noexcept;
Rect trimRight (Rect r, float d) noexcept;
Rect unite (Rect a, Rect b) noexcept;
Rect fromDesign (Rect d) noexcept;   // design units -> L
Rect toDesign (Rect l) noexcept;     // L -> design units

enum class HAlign : std::uint8_t { left, centre, right };
enum class VAlign : std::uint8_t { top, middle, bottom };
enum class TextLook : std::uint8_t { plain, engraved, shadowed, neon };

/** Picks the baked text style closest to an 0.10.0 font height (L units). */
hc::ui::TextStyle styleFor (float heightL, bool bold) noexcept;

class Paint
{
public:
    explicit Paint (hc::ui::Renderer& r) noexcept : rr (r) {}
    hc::ui::Renderer& renderer() noexcept { return rr; }

    // shapes
    void fill (Rect r, Rgba c, bool emissive = false);
    void fillRound (Rect r, float radius, Rgba c, bool emissive = false);
    void fillEllipse (Rect r, Rgba c, bool emissive = false);
    void outlineRound (Rect r, float radius, float width, Rgba c, bool emissive = false);
    void outline (Rect r, float width, Rgba c);
    void line (float x0, float y0, float x1, float y1, float width, Rgba c, bool emissive = false);
    void polyline (const std::vector<float>& xs, const std::vector<float>& ys, float width, Rgba c, bool emissive = false);
    void verticalGradient (Rect r, Rgba top, Rgba bottom, bool emissive = false);
    void horizontalGradient (Rect r, Rgba left, Rgba right, bool emissive = false);
    void quad (const float xs[4], const float ys[4], Rgba c, bool emissive = false);
    void quad4 (const float xs[4], const float ys[4], const Rgba cs[4], bool emissive = false);
    void triangle (float x0, float y0, float x1, float y1, float x2, float y2, Rgba c, bool emissive = false);
    void arc (float cx, float cy, float radius, float width, float a0, float a1, Rgba c, bool emissive = false);
    void glow (Rect core, float radius, float spread, Rgba c);
    void sprite (hc::ui::Sprite s, Rect r, Rgba tint = {}, bool add = false, bool emissive = false);
    void spriteRotated (hc::ui::Sprite s, float cx, float cy, float w, float h, float angle, Rgba tint = {});
    void nineSlice (hc::ui::Sprite s, Rect r, float cornerScale, Rgba tint = {}, bool add = false);
    /** A piece of the main faceplate drawn as a 9-slice: `src` in faceplate pixels, corners keep
        `inset` source pixels x `scale` (design units per source pixel). */
    void chassis9 (Rect src, float inset, Rect dst, float scale);
    /** Filled area under a curve down to `baseY` (vertical strips, one per point pair). */
    void area (const std::vector<float>& xs, const std::vector<float>& ys, float baseY, Rgba top, Rgba bottom);

    // text
    float textWidth (std::string_view s, float heightL, bool bold = true) const;
    void text (std::string_view s, Rect area, float heightL, bool bold, Rgba c, HAlign h = HAlign::left, VAlign v = VAlign::middle,
               TextLook look = TextLook::plain);
    /** Engraved text on chrome (dark with a light lower edge). */
    void engraved (std::string_view s, Rect area, float heightL, HAlign h = HAlign::left, VAlign v = VAlign::middle);
    /** Light text with a soft shadow, for the grey panels. */
    void panelText (std::string_view s, Rect area, float heightL, HAlign h = HAlign::left, VAlign v = VAlign::middle,
                    Rgba c = colours::paper);
    /** Word-wrapped text, at most `maxLines` lines (the last one shortened with ".." if it does not fit). */
    void wrapped (std::string_view s, Rect area, float heightL, bool bold, Rgba c, int maxLines, HAlign h = HAlign::left,
                  VAlign v = VAlign::top);
    std::vector<std::string> wrapLines (std::string_view s, float width, float heightL, bool bold) const;
    /** `s` cut with ".." so it fits `width`. */
    std::string fitText (std::string_view s, float width, float heightL, bool bold) const;
    float lineHeight (float heightL) const noexcept { return heightL * 1.2f; }

    // clipping
    void pushClip (Rect r);
    void popClip();

    /** One window pixel in L units. */
    float pixel() const noexcept { return rr.pixelSize() / kL; }

private:
    hc::ui::Renderer& rr;
};
} // namespace hem::app
