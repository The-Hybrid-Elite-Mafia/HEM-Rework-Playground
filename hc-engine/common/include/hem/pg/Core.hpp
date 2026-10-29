#pragma once
// =============================================================================
//  HEM Rework Playground engine — small building blocks shared by every plug-in engine.
//  Plain C++23, no JUCE. Where the Playground 0.10.0 code relied on JUCE helpers, these
//  reproduce the same arithmetic (same float types, same order of operations) so a ported
//  engine renders the same samples.
// =============================================================================
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
 #include <xmmintrin.h>
 #define HEM_PG_X86 1
#endif

namespace hem::pg
{
using uint32 = std::uint32_t;
using int64 = std::int64_t;
using String = std::string;
using StringArray = std::vector<std::string>;

template <typename T> constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }
template <typename T> constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }
template <typename T> constexpr T jmin (T a, T b, T c) noexcept { return jmin (jmin (a, b), c); }
template <typename T> constexpr T jmax (T a, T b, T c) noexcept { return jmax (jmax (a, b), c); }
/** Same argument order as the old code: lower, upper, value. */
template <typename T> constexpr T jlimit (T lower, T upper, T v) noexcept { return v < lower ? lower : (upper < v ? upper : v); }

/** Round to nearest, ties to even (the old code's rounding, not std::lround's ties-away). */
inline int roundToInt (double v) noexcept { return (int) std::nearbyint (v); }
inline int roundToInt (float v) noexcept { return (int) std::nearbyint ((double) v); }

template <typename T> struct MathConstants { static constexpr T pi = std::numbers::pi_v<T>; static constexpr T twoPi = 2 * std::numbers::pi_v<T>; static constexpr T halfPi = std::numbers::pi_v<T> / 2; };

/** True when a and b are equal within one float step (relative) — the rule the old code's
    parameter and smoothing logic used to decide "nothing changed". */
template <typename T>
constexpr bool approximatelyEqual (T a, T b) noexcept
{
    if (! (std::isfinite (a) && std::isfinite (b))) return a == b;
    const T diff = std::abs (a - b);
    return diff <= std::numeric_limits<T>::min() || diff <= std::numeric_limits<T>::epsilon() * std::max (std::abs (a), std::abs (b));
}

namespace params
{
    /** dB -> linear gain (-120 dB and below = silence). */
    inline float dbToGain (float db) noexcept { return db <= -120.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g) noexcept { return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f; }
}

//==============================================================================
/** Linear ramp to a target over a fixed number of samples (audio thread; no allocation). */
template <typename FloatType>
class SmoothedValue
{
public:
    SmoothedValue() noexcept = default;
    explicit SmoothedValue (FloatType initial) noexcept : current (initial), target (initial) {}

    void reset (double sampleRate, double rampSeconds) noexcept { reset ((int) std::floor (rampSeconds * sampleRate)); }
    void reset (int numSteps) noexcept { stepsToTarget = numSteps; setCurrentAndTargetValue (target); }

    void setCurrentAndTargetValue (FloatType v) noexcept { target = current = v; countdown = 0; }

    void setTargetValue (FloatType v) noexcept
    {
        if (approximatelyEqual (v, target)) return;
        if (stepsToTarget <= 0) { setCurrentAndTargetValue (v); return; }
        target = v;
        countdown = stepsToTarget;
        step = (target - current) / (FloatType) countdown;
    }

    FloatType getNextValue() noexcept
    {
        if (countdown <= 0) return target;
        --countdown;
        if (countdown > 0) current += step;
        else current = target;
        return current;
    }

    FloatType skip (int numSamples) noexcept
    {
        if (numSamples >= countdown) { setCurrentAndTargetValue (target); return target; }
        current += step * (FloatType) numSamples;
        countdown -= numSamples;
        return current;
    }

    bool isSmoothing() const noexcept { return countdown > 0; }
    FloatType getCurrentValue() const noexcept { return current; }
    FloatType getTargetValue() const noexcept { return target; }

private:
    FloatType current = 0, target = 0, step = 0;
    int countdown = 0, stepsToTarget = 0;
};

//==============================================================================
/** Single-producer / single-consumer ring index bookkeeping (one slot is always kept free). */
class AbstractFifo
{
public:
    explicit AbstractFifo (int capacity) noexcept : bufferSize (capacity) {}

    int getTotalSize() const noexcept { return bufferSize; }
    int getNumReady() const noexcept
    {
        const int vs = validStart.load(), ve = validEnd.load();
        return ve >= vs ? (ve - vs) : (bufferSize - (vs - ve));
    }
    void reset() noexcept { validStart = 0; validEnd = 0; }

    void prepareToWrite (int numToWrite, int& start1, int& size1, int& start2, int& size2) const noexcept
    {
        const int vs = validStart.load(), ve = validEnd.load();
        const int freeSpace = ve >= vs ? (bufferSize - (ve - vs)) : (vs - ve);
        numToWrite = jmin (numToWrite, freeSpace - 1);
        if (numToWrite <= 0) { start1 = start2 = size1 = size2 = 0; return; }
        start1 = ve; start2 = 0;
        size1 = jmin (bufferSize - ve, numToWrite);
        numToWrite -= size1;
        size2 = numToWrite <= 0 ? 0 : jmin (numToWrite, vs);
    }
    void finishedWrite (int n) noexcept
    {
        int e = validEnd.load() + n;
        if (e >= bufferSize) e -= bufferSize;
        validEnd = e;
    }
    void prepareToRead (int numWanted, int& start1, int& size1, int& start2, int& size2) const noexcept
    {
        const int vs = validStart.load(), ve = validEnd.load();
        const int ready = ve >= vs ? (ve - vs) : (bufferSize - (vs - ve));
        numWanted = jmin (numWanted, ready);
        if (numWanted <= 0) { start1 = start2 = size1 = size2 = 0; return; }
        start1 = vs; start2 = 0;
        size1 = jmin (bufferSize - vs, numWanted);
        numWanted -= size1;
        size2 = numWanted <= 0 ? 0 : jmin (numWanted, ve);
    }
    void finishedRead (int n) noexcept
    {
        int s = validStart.load() + n;
        if (s >= bufferSize) s -= bufferSize;
        validStart = s;
    }

private:
    int bufferSize;
    std::atomic<int> validStart { 0 }, validEnd { 0 };
};

//==============================================================================
/** Flush-to-zero + denormals-are-zero for the life of the object (x86 MXCSR bits 15 and 6). */
class ScopedNoDenormals
{
public:
#if HEM_PG_X86
    ScopedNoDenormals() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved | 0x8040u); }
    ~ScopedNoDenormals() { _mm_setcsr (saved); }
private:
    unsigned int saved;
#else
    ScopedNoDenormals() noexcept = default;
#endif
};
} // namespace hem::pg
