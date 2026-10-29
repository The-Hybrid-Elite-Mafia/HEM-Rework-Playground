#pragma once
// =============================================================================
//  AudioBuffer — a block of float channels for OFFLINE work (Band Forge renders, file analysis,
//  mixes written to disk). Never used on the audio thread (it allocates).
//
//  Same behaviour as the buffer 0.10.0 used, including its "cleared" flag: a buffer marked cleared
//  skips gain changes, and the first add into a cleared buffer is a copy. That keeps renders
//  identical down to the sign of zero samples.
// =============================================================================
#include "hem/pg/Core.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hem::pg
{
class AudioBuffer
{
public:
    AudioBuffer() = default;
    /** New buffers hold zeros (not marked cleared, as in 0.10.0). */
    AudioBuffer (int numChannels, int numSamples) { setSize (numChannels, numSamples); }

    int getNumChannels() const noexcept { return chans; }
    int getNumSamples() const noexcept { return len; }

    /** keepExisting: old samples stay (new space is zero); otherwise the whole buffer is zeroed. */
    void setSize (int numChannels, int numSamples, bool keepExisting = false, bool /*clearExtraSpace*/ = false, bool /*avoidReallocating*/ = false)
    {
        if (numChannels == chans && numSamples == len && keepExisting) return;
        if (numChannels == chans && numSamples == len)
        {
            if (cleared) std::fill (data.begin(), data.end(), 0.0f);
            return;
        }
        std::vector<float> fresh ((size_t) numChannels * (size_t) numSamples, 0.0f);
        if (keepExisting && ! cleared)
            for (int c = 0; c < std::min (chans, numChannels); ++c)
                std::copy (data.begin() + (std::ptrdiff_t) c * len, data.begin() + (std::ptrdiff_t) c * len + std::min (len, numSamples),
                           fresh.begin() + (std::ptrdiff_t) c * numSamples);
        data = std::move (fresh);
        chans = numChannels;
        len = numSamples;
    }

    const float* getReadPointer (int channel, int start = 0) const noexcept { return data.data() + (size_t) channel * (size_t) len + (size_t) start; }
    float* getWritePointer (int channel, int start = 0) noexcept { cleared = false; return data.data() + (size_t) channel * (size_t) len + (size_t) start; }
    /** Write pointers for every channel (for code that wants float**). */
    std::vector<float*> getArrayOfWritePointers() noexcept
    {
        cleared = false;
        std::vector<float*> p ((size_t) chans);
        for (int c = 0; c < chans; ++c) p[(size_t) c] = data.data() + (size_t) c * (size_t) len;
        return p;
    }

    float getSample (int channel, int i) const noexcept { return getReadPointer (channel)[i]; }
    void setSample (int channel, int i, float v) noexcept { cleared = false; data[(size_t) channel * (size_t) len + (size_t) i] = v; }
    void addSample (int channel, int i, float v) noexcept { cleared = false; data[(size_t) channel * (size_t) len + (size_t) i] += v; }

    void clear() noexcept
    {
        if (cleared) return;
        std::fill (data.begin(), data.end(), 0.0f);
        cleared = true;
    }
    void clear (int channel, int start, int n) noexcept
    {
        if (! cleared) std::fill_n (ptr (channel, start), n, 0.0f);
    }
    bool hasBeenCleared() const noexcept { return cleared; }

    void applyGain (int channel, int start, int n, float gain) noexcept
    {
        if (cleared) return;
        float* d = ptr (channel, start);
        for (int i = 0; i < n; ++i) d[i] *= gain;
    }
    void applyGain (int start, int n, float gain) noexcept { for (int c = 0; c < chans; ++c) applyGain (c, start, n, gain); }
    void applyGain (float gain) noexcept { applyGain (0, len, gain); }

    void applyGainRamp (int channel, int start, int n, float startGain, float endGain) noexcept
    {
        if (cleared) return;
        const float increment = (endGain - startGain) / (float) n;
        float* d = ptr (channel, start);
        while (--n >= 0)
        {
            *d++ *= startGain;
            startGain += increment;
        }
    }

    void addFrom (int destChannel, int destStart, const AudioBuffer& src, int srcChannel, int srcStart, int n, float gain = 1.0f) noexcept
    {
        if (n <= 0 || src.cleared) return;
        float* d = ptr (destChannel, destStart);
        const float* s = src.getReadPointer (srcChannel, srcStart);
        if (cleared)
        {
            cleared = false;
            for (int i = 0; i < n; ++i) d[i] = s[i] * gain;
        }
        else
            for (int i = 0; i < n; ++i) d[i] = d[i] + s[i] * gain;
    }

    void copyFrom (int destChannel, int destStart, const AudioBuffer& src, int srcChannel, int srcStart, int n) noexcept
    {
        if (n <= 0) return;
        if (src.cleared)
        {
            if (! cleared) std::fill_n (ptr (destChannel, destStart), n, 0.0f);
            return;
        }
        cleared = false;
        const float* s = src.getReadPointer (srcChannel, srcStart);
        std::copy (s, s + n, ptr (destChannel, destStart));
    }
    void copyFrom (int destChannel, int destStart, const float* src, int n) noexcept
    {
        if (n <= 0) return;
        cleared = false;
        std::copy (src, src + n, ptr (destChannel, destStart));
    }

    float getMagnitude (int channel, int start, int n) const noexcept
    {
        if (cleared || n <= 0) return 0.0f;
        const float* d = getReadPointer (channel, start);
        float lo = d[0], hi = d[0];
        for (int i = 1; i < n; ++i) { lo = std::min (lo, d[i]); hi = std::max (hi, d[i]); }
        return jmax (jmax (lo, -lo), jmax (hi, -hi));
    }
    float getMagnitude (int start, int n) const noexcept
    {
        float m = 0.0f;
        for (int c = 0; c < chans; ++c) m = jmax (m, getMagnitude (c, start, n));
        return m;
    }
    float getRMSLevel (int channel, int start, int n) const noexcept
    {
        if (n <= 0 || cleared) return 0.0f;
        const float* d = getReadPointer (channel, start);
        double sum = 0.0;
        for (int i = 0; i < n; ++i) { const float s = d[i]; sum += s * s; }
        return (float) std::sqrt (sum / n);
    }

private:
    float* ptr (int channel, int start) noexcept { return data.data() + (size_t) channel * (size_t) len + (size_t) start; }

    std::vector<float> data;
    int chans = 0, len = 0;
    bool cleared = false;
};
} // namespace hem::pg
