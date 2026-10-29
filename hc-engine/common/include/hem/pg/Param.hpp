#pragma once
// =============================================================================
//  Plug-in parameters without JUCE.
//
//  Every control is a ParamSpec (id, name, range, default, display) plus one atomic "raw" value
//  in real units (dB, Hz, %, index...) that the audio thread reads. Moving a control goes through
//  the same steps the 0.10.0 app used (0..1 position -> real value snapped to its step), so a
//  saved session or a knob position lands on exactly the value it did before.
//
//  Threads: setters on the message thread (or an offline render thread on its own copy);
//  the audio thread only loads raw values (relaxed atomics).
// =============================================================================
#include "hem/pg/Core.hpp"

#include <memory>
#include <span>

namespace hem::pg
{
/** Value <-> 0..1 mapping. Replicates the float maths of the old parameter ranges. */
struct Range
{
    enum class Kind : std::uint8_t { linear, logarithmic, choice };
    float start = 0.0f, end = 1.0f, interval = 0.0f, skew = 1.0f;
    Kind kind = Kind::linear;

    static Range linear (float a, float b, float step) noexcept { Range r; r.start = a; r.end = b; r.interval = step; return r; }
    static Range logarithmic (float a, float b) noexcept { Range r; r.start = a; r.end = b; r.kind = Kind::logarithmic; return r; }
    static Range choiceIndex (int count) noexcept { Range r; r.start = 0.0f; r.end = (float) count - 1.0f; r.interval = 1.0f; r.kind = Kind::choice; return r; }
    void setSkewForCentre (float centre) noexcept { skew = std::log (0.5f) / std::log ((centre - start) / (end - start)); }

    static float clamp01 (float v) noexcept { return jlimit (0.0f, 1.0f, v); }

    /** Plain mapping (no snapping). */
    float to01 (float v) const noexcept
    {
        if (kind == Kind::logarithmic) return clamp01 (std::log (v / start) / std::log (end / start));
        if (kind == Kind::choice) return clamp01 (jlimit (0.0f, 1.0f, v / end));
        const float p = clamp01 ((v - start) / (end - start));
        if (skew == 1.0f) return p;
        return std::pow (p, skew);
    }
    float from01 (float p) const noexcept
    {
        p = clamp01 (p);
        if (kind == Kind::logarithmic) return start * std::pow (end / start, p);
        if (kind == Kind::choice) return jlimit (0.0f, end, p * end);
        if (skew != 1.0f && p > 0.0f) p = std::exp (std::log (p) / skew);
        return start + (end - start) * p;
    }
    float snap (float v) const noexcept
    {
        if (kind == Kind::logarithmic) return jlimit (start, end, v);
        if (kind == Kind::choice) return (float) roundToInt (jlimit (start, end, v));
        if (interval > 0.0f) v = start + interval * std::floor ((v - start) / interval + 0.5f);
        return (v <= start || end <= start) ? start : (v >= end ? end : v);
    }
    /** What a control does with a real value / a position: snap first, then map. */
    float valueTo01 (float v) const noexcept { return to01 (snap (v)); }
    float valueFrom01 (float p) const noexcept { return snap (from01 (clamp01 (p))); }
};

struct ParamSpec
{
    enum class Type : std::uint8_t { floating, toggle, choice };
    enum class Unit : std::uint8_t { db, hz, ms, percent, plain, none };
    std::string id, name;
    Type type = Type::floating;
    Unit unit = Unit::plain;
    std::string unitLabel;          // shown after a plain number ("dB/oct", ":1", "s"...)
    Range range;
    float defaultValue = 0.0f;      // real units (index for a choice, 0/1 for a toggle)
    StringArray choices;

    /** The text the control shows for a real value. */
    std::string toText (float v) const;
};

/** One control's live state. */
class Param
{
public:
    explicit Param (ParamSpec s);

    const ParamSpec& spec() const noexcept { return sp; }
    const std::string& id() const noexcept { return sp.id; }

    /** Audio thread: the real value. */
    float get() const noexcept { return raw.load (std::memory_order_relaxed); }
    std::atomic<float>* rawPointer() noexcept { return &raw; }

    /** Message thread: move the control to a 0..1 position (a knob turn). */
    void setNormalised (float p) noexcept;
    /** Message thread: set a real value (sessions, presets, typed numbers). */
    void setValue (float v) noexcept { setNormalised (sp.range.valueTo01 (v)); }
    /** Position of the control, 0..1. */
    float getNormalised() const noexcept;
    float defaultNormalised() const noexcept;
    void resetToDefault() noexcept { setNormalised (defaultNormalised()); }

private:
    ParamSpec sp;
    float position = 0.0f;           // the control's own stored value (real units, or 0..1 for a toggle)
    std::atomic<float> raw { 0.0f };
};

/** The ordered list of a plug-in's controls. */
class ParamSet
{
public:
    void add (ParamSpec spec);
    Param* find (std::string_view id) noexcept;
    const Param* find (std::string_view id) const noexcept;
    std::span<const std::unique_ptr<Param>> all() const noexcept { return params; }
    std::size_t size() const noexcept { return params.size(); }

private:
    std::vector<std::unique_ptr<Param>> params;
};

/** The helpers every plug-in declares its controls with (same names and arguments as 0.10.0). */
namespace params
{
    using Layout = std::vector<ParamSpec>;
    void addDb (Layout&, const std::string& id, const std::string& name, float min, float max, float def, float step = 0.1f);
    void addHz (Layout&, const std::string& id, const std::string& name, float min, float max, float def);
    void addMs (Layout&, const std::string& id, const std::string& name, float min, float max, float def);
    void addFloat (Layout&, const std::string& id, const std::string& name, float min, float max, float def, const char* unit, float step = 0.01f, float centre = 0.0f);
    void addPercent (Layout&, const std::string& id, const std::string& name, float def);
    void addBool (Layout&, const std::string& id, const std::string& name, bool def);
    void addChoice (Layout&, const std::string& id, const std::string& name, const StringArray& choices, int def);
}
} // namespace hem::pg
