#pragma once
// Small per-sample helpers used by many plug-in engines (same arithmetic as 0.10.0).
#include <algorithm>
#include <cmath>

namespace hem::pg::dsp
{
/** One-pole smoothing coefficient for a time constant in ms. */
inline float timeCoeff (double ms, double fs) noexcept
{
    const double samples = std::max (1.0e-3, ms * 0.001 * fs);
    return (float) std::exp (-1.0 / samples);
}

inline float linToDb (float x) noexcept { return x > 1.0e-9f ? 20.0f * std::log10 (x) : -180.0f; }
inline float dbToLin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
} // namespace hem::pg::dsp
