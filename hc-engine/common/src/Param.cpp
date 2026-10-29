#include "hem/pg/Param.hpp"

#include <cstdio>

namespace hem::pg
{
namespace
{
    std::string fmt (float v, int decimals)
    {
        char buf[48];
        std::snprintf (buf, sizeof buf, "%.*f", decimals, (double) v);
        return buf;
    }
}

std::string ParamSpec::toText (float v) const
{
    switch (type)
    {
        case Type::toggle: return v >= 0.5f ? "On" : "Off";
        case Type::choice:
        {
            const int i = jlimit (0, (int) choices.size() - 1, (int) v);
            return choices.empty() ? std::string() : choices[(std::size_t) i];
        }
        case Type::floating: break;
    }
    switch (unit)
    {
        case Unit::db: return fmt (v, 1) + " dB";
        case Unit::hz: return v >= 1000.0f ? fmt (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : std::to_string ((int) std::round (v)) + " Hz";
        case Unit::ms: return v >= 1000.0f ? fmt (v / 1000.0f, 2) + " s" : (v < 10.0f ? fmt (v, 1) : std::to_string (roundToInt (v))) + " ms";
        case Unit::percent: return std::to_string ((int) std::round (v)) + " %";
        case Unit::plain: return fmt (v, std::abs (v) < 10.0f ? 2 : 1) + (unitLabel.empty() ? "" : " " + unitLabel);
        case Unit::none: break;
    }
    return fmt (v, 2);
}

//==============================================================================
Param::Param (ParamSpec s) : sp (std::move (s))
{
    position = sp.type == ParamSpec::Type::toggle ? (sp.defaultValue >= 0.5f ? 1.0f : 0.0f) : sp.defaultValue;
    // The first live value is the default's position mapped back without snapping (what 0.10.0 did).
    raw.store (sp.type == ParamSpec::Type::toggle ? position : sp.range.from01 (defaultNormalised()));
}

float Param::defaultNormalised() const noexcept
{
    if (sp.type == ParamSpec::Type::toggle) return sp.defaultValue >= 0.5f ? 1.0f : 0.0f;
    return sp.range.valueTo01 (sp.defaultValue);
}

float Param::getNormalised() const noexcept
{
    return sp.type == ParamSpec::Type::toggle ? position : sp.range.valueTo01 (position);
}

void Param::setNormalised (float p) noexcept
{
    position = sp.type == ParamSpec::Type::toggle ? p : sp.range.valueFrom01 (p);
    const float next = sp.range.valueFrom01 (getNormalised());
    if (! approximatelyEqual (raw.load (std::memory_order_relaxed), next))
        raw.store (next, std::memory_order_relaxed);
}

//==============================================================================
void ParamSet::add (ParamSpec spec) { params.push_back (std::make_unique<Param> (std::move (spec))); }

Param* ParamSet::find (std::string_view id) noexcept
{
    for (auto& p : params) if (p->id() == id) return p.get();
    return nullptr;
}

const Param* ParamSet::find (std::string_view id) const noexcept
{
    for (auto& p : params) if (p->id() == id) return p.get();
    return nullptr;
}

//==============================================================================
namespace params
{
    void addDb (Layout& l, const std::string& id, const std::string& name, float min, float max, float def, float step)
    {
        ParamSpec s { id, name, ParamSpec::Type::floating, ParamSpec::Unit::db, "dB", Range::linear (min, max, step), def, {} };
        if (min < 0.0f && max > 0.0f && std::abs (min + max) > 1.0e-3f)
            s.range.setSkewForCentre (0.0f);   // boost / cut knobs: 0 dB always sits straight up
        l.push_back (std::move (s));
    }

    void addHz (Layout& l, const std::string& id, const std::string& name, float min, float max, float def)
    {
        l.push_back ({ id, name, ParamSpec::Type::floating, ParamSpec::Unit::hz, "Hz", Range::logarithmic (min, max), def, {} });
    }

    void addMs (Layout& l, const std::string& id, const std::string& name, float min, float max, float def)
    {
        l.push_back ({ id, name, ParamSpec::Type::floating, ParamSpec::Unit::ms, "ms", Range::logarithmic (min, max), def, {} });
    }

    void addFloat (Layout& l, const std::string& id, const std::string& name, float min, float max, float def, const char* unit, float step, float centre)
    {
        ParamSpec s { id, name, ParamSpec::Type::floating, ParamSpec::Unit::plain, unit != nullptr ? unit : "", Range::linear (min, max, step), def, {} };
        if (centre > min && centre < max) s.range.setSkewForCentre (centre);
        l.push_back (std::move (s));
    }

    void addPercent (Layout& l, const std::string& id, const std::string& name, float def)
    {
        l.push_back ({ id, name, ParamSpec::Type::floating, ParamSpec::Unit::percent, "%", Range::linear (0.0f, 100.0f, 0.1f), def, {} });
    }

    void addBool (Layout& l, const std::string& id, const std::string& name, bool def)
    {
        l.push_back ({ id, name, ParamSpec::Type::toggle, ParamSpec::Unit::none, "", Range::linear (0.0f, 1.0f, 1.0f), def ? 1.0f : 0.0f, {} });
    }

    void addChoice (Layout& l, const std::string& id, const std::string& name, const StringArray& choices, int def)
    {
        l.push_back ({ id, name, ParamSpec::Type::choice, ParamSpec::Unit::none, "", Range::choiceIndex ((int) choices.size()), (float) def, choices });
    }
}
} // namespace hem::pg
