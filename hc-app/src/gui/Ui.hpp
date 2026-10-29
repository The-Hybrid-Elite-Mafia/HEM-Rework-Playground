#pragma once
// =============================================================================
//  Ui — the Playground's controls, drawn every frame on The Hybrid Core renderer
//  (immediate mode: a page calls ui.knob (...) each frame; the call draws the knob and
//  reports what the mouse did to it). Everything is in L units (see Paint.hpp).
//
//  Overlays (pop-up menus, dialogs, tooltips) are drawn after the page; while one is open the
//  page's controls get no input. Text boxes are real Windows edit boxes placed over the GL
//  window (typing, selection, copy / paste, IME all work as in any Windows app).
//  UI thread only.
// =============================================================================
#include "Paint.hpp"
#include "hybrid/ui/Faceplate.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hem::app
{
using Id = std::uint64_t;

/** Stable id from text (FNV-1a), optionally mixed with an index. */
Id makeId (std::string_view text, std::uint64_t salt = 0) noexcept;
inline Id subId (Id base, std::uint64_t index) noexcept { return (base ^ (index + 0x9E3779B97F4A7C15ull)) * 0x100000001B3ull; }

/** One frame of input, in L units. */
struct Input
{
    float x = -1.0e6f, y = -1.0e6f;
    bool down = false;
    bool pressed = false, released = false, doubleClick = false, rightPressed = false;
    float pressX = -1.0e6f, pressY = -1.0e6f;
    float rightX = -1.0e6f, rightY = -1.0e6f;
    float wheel = 0.0f;
    bool ctrl = false, shift = false;
    struct Key { std::uint32_t vk; bool ctrl, shift; };
    std::vector<Key> keys;
    std::vector<std::filesystem::path> dropped;
    float dropX = 0.0f, dropY = 0.0f;
};

/** Native text boxes (Windows edit controls over the GL window). */
struct FieldStyle
{
    bool multiline = false;
    bool password = false;
    float fontHeightL = 15.0f;
    std::string placeholder;   // single-line only (Windows cue banner)
    Rgba accent = colours::glow;
    bool enabled = true;
    float pixelsPerL = 1.0f;   // set by Ui: window pixels per L unit (font size)
};

class TextFields
{
public:
    virtual ~TextFields() = default;
    /** Shows the box at `pixels` (window pixels) this frame. Returns true when the user changed the text
        (then `text` holds the new text). When the app changes `text` itself the box follows. */
    virtual bool field (Id id, Rect pixels, std::string& text, const FieldStyle& style, int layer) = 0;
    /** Hides every box not shown this frame, and every box under something drawn on a higher layer
        (`occluders`: layer + window pixels; a native box would otherwise show through a GL menu). */
    virtual void endFrame (const std::vector<std::pair<int, Rect>>& occluders) = 0;
    virtual bool anyFocused() const = 0;
    virtual void unfocus() = 0;
    virtual void focus (Id id) = 0;
};

struct MenuItem
{
    std::string text;
    int id = 0;
    bool enabled = true, checked = false, header = false, separator = false;
    Rgba colour { 0, 0, 0, 0 };   // a colour swatch on the left (plug-in accents, track colours)
    std::vector<MenuItem> sub;

    static MenuItem item (std::string t, int i, bool en = true, bool chk = false) { MenuItem m; m.text = std::move (t); m.id = i; m.enabled = en; m.checked = chk; return m; }
    static MenuItem headerItem (std::string t) { MenuItem m; m.text = std::move (t); m.header = true; m.enabled = false; return m; }
    static MenuItem sep() { MenuItem m; m.separator = true; m.enabled = false; return m; }
    static MenuItem submenu (std::string t, std::vector<MenuItem> s, bool en = true) { MenuItem m; m.text = std::move (t); m.sub = std::move (s); m.enabled = en; return m; }
};

enum class ButtonLook : std::uint8_t { plate, dark, tab, chrome, flat };

class Ui
{
public:
    Ui (Paint& paint, TextFields* fields) noexcept : p (paint), textFields (fields) {}

    Paint& p;

    /** Starts a frame: `canvas` maps design units to window pixels; `seconds` is a steady clock. */
    void beginFrame (const Input& input, const hc::ui::Canvas& canvas, double seconds);
    /** Draws the overlays (menus, dialogs, tooltip) and finishes the text boxes. */
    void endFrame();

    const Input& input() const noexcept { return in; }
    double now() const noexcept { return time; }
    const hc::ui::Canvas& canvas() const noexcept { return cv; }
    /** The window area in L units (the part of the canvas that is on screen). */
    Rect screen() const noexcept;

    //============================================================================== input helpers
    bool enabled() const noexcept { return ! blocked; }
    bool hovering (Rect r) const noexcept { return enabled() && r.contains (in.x, in.y); }
    bool isActive (Id id) const noexcept { return active == id; }
    bool isHot (Id id) const noexcept { return hot == id; }
    bool anyActive() const noexcept { return active != 0; }

    struct Press { bool clicked = false, doubleClick = false, right = false, hover = false, held = false; float rx = 0, ry = 0; };
    /** Button behaviour: clicked on release inside after a press inside. */
    Press behave (Id id, Rect r, bool allowed = true);

    struct Drag
    {
        bool started = false, dragging = false, ended = false, hover = false, doubleClick = false, moved = false, right = false;
        float x = 0, y = 0, startX = 0, startY = 0, dx = 0, dy = 0;   // dx / dy: since the press
        float stepX = 0, stepY = 0;                                     // since the last frame
    };
    Drag drag (Id id, Rect r, bool allowed = true);

    /** A press inside `r` that no control took this frame (strip / row selection). */
    bool unclaimedPress (Rect r) noexcept;
    bool unclaimedRight (Rect r) noexcept;
    /** Wheel notches over `r` (0 if none, or a control already used them). */
    float wheel (Rect r) noexcept;
    /** True once for a key pressed this frame (and not typed into a text box). */
    bool key (std::uint32_t vk, bool ctrl = false, bool shift = false) noexcept;
    /** Files dropped on `r` this frame (taken: empty for the next caller). */
    std::vector<std::filesystem::path> takeDrop (Rect r) noexcept;
    bool dropHovering() const noexcept { return false; }

    //============================================================================== controls
    bool button (Id id, Rect r, std::string_view text, ButtonLook look = ButtonLook::plate, Rgba accent = colours::glow,
                 bool enabled = true, bool lit = false, float textHeight = 0.0f);
    /** Round socket with an LED and a label (0.10.0 "led" toggle). Returns true when clicked (toggles `on`). */
    bool ledToggle (Id id, Rect r, bool& on, std::string_view label, Rgba accent, bool onChrome = false, bool enabled = true);
    /** Chrome cap button that lights up (M / S / BYPASS). Returns true when clicked. */
    bool capButton (Id id, Rect r, std::string_view text, bool on, Rgba lit, bool enabled = true);
    /** Rotary knob. `norm` 0..1. Drag up / down (Shift / Ctrl: fine), wheel, double-click = `resetTo`.
        Returns true while the value changes. `gesture` gets +1 when a drag starts and -1 when it ends. */
    bool knob (Id id, Rect r, float& norm, float resetTo, Rgba accent, bool bipolar = false, float zeroNorm = 0.0f,
               int* gesture = nullptr, bool socket = true, bool enabled = true);
    /** Vertical fader with dB ticks (`tickNorms` 0..1, `zeroTick` the 0 dB one). */
    bool fader (Id id, Rect r, float& norm, float resetTo, Rgba accent, const std::vector<float>& tickNorms = {},
                float zeroTick = -1.0f, int* gesture = nullptr);
    bool hslider (Id id, Rect r, float& norm, Rgba accent, int* gesture = nullptr, bool enabled = true);
    /** Drop-down: a box showing items[index]; a pop-up menu picks another. Returns true when it changed. */
    bool combo (Id id, Rect r, const std::vector<std::string>& items, int& index, Rgba accent, bool plate = false,
                bool enabled = true, std::string_view emptyText = {}, float textHeight = 0.0f);
    /** Native text box. Returns true when the user changed it. */
    bool textField (Id id, Rect r, std::string& text, const FieldStyle& style = {});
    void progress (Rect r, float fraction, Rgba colour);
    void tooltip (Rect r, std::string_view text);
    /** Small LED dome. */
    void led (float cx, float cy, float radius, Rgba colour, bool on);

    //============================================================================== overlays
    using MenuResult = std::function<void (int id)>;
    void openMenu (std::vector<MenuItem> items, float x, float y, MenuResult onPick, float minWidth = 0.0f);
    bool menuOpen() const noexcept { return menu != nullptr; }
    void closeMenu() noexcept;

    /** A modal overlay: drawn every frame on top of everything until it returns false. */
    using Overlay = std::function<bool (Ui&)>;
    void pushOverlay (Overlay o);
    bool overlayOpen() const noexcept { return ! overlays.empty(); }

    TextFields* fields() const noexcept { return textFields; }
    Rect toPixels (Rect l) const noexcept;

    /** Lets a control keep a small value between frames (combo picks, hover timers...). */
    std::map<Id, int> pickedIndex;

private:
    struct MenuState
    {
        std::vector<MenuItem> items;
        float x = 0, y = 0, minWidth = 0;
        MenuResult onPick;
        int openSub = -1;
        double openedAt = 0;
    };
    void drawMenu();
    bool menuLevel (const std::vector<MenuItem>& items, float x, float y, float minW, int depth, int& pickedId, bool& inside);
    void drawTooltip();
    void claim (Id id) noexcept { pressClaimed = true; active = id; }

    TextFields* textFields = nullptr;
    Input in;
    hc::ui::Canvas cv;
    double time = 0.0;
    bool blocked = false, pressClaimed = false, rightClaimed = false, wheelUsed = false;
    Id hot = 0, active = 0, lastHot = 0;
    float lastX = 0, lastY = 0, dragStartX = 0, dragStartY = 0;
    bool dragMoved = false;
    std::unique_ptr<MenuState> menu;
    std::vector<Overlay> overlays;
    std::vector<Overlay> pendingOverlays;
    // tooltip
    std::string tipText, tipCandidate;
    Rect tipRect {};
    double tipSince = 0.0;
    std::vector<bool> keyUsed;
    int layer = 0;                                     // 0 = page, 1.. = overlays (text-box stacking)
    std::vector<std::pair<int, Rect>> occluders;       // this frame's menus / dialogs, window pixels
    bool tipSeen = false;
};
} // namespace hem::app
