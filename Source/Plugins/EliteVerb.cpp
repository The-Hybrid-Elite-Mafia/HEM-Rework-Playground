#include "ElitePlugins.h"
#include <numbers>

namespace pg
{
const PluginInfo EliteVerb::info { "elite-verb", "Elite Verb", "VERB", "Room / Plate / Hall reverb", 0xff8f6ae0 };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout verbLayout()
    {
        params::Layout l;
        params::addChoice (l, "type", "Type", EliteVerb::types(), 1);
        params::addMs (l, "predelay", "Pre-Delay", 0.1f, 250.0f, 20.0f);
        params::addPercent (l, "size", "Size", 50.0f);
        params::addFloat (l, "decay", "Decay", 0.2f, 12.0f, 1.8f, "s", 0.01f, 2.0f);
        params::addPercent (l, "damping", "Damping", 45.0f);
        params::addHz (l, "lowcut", "Low Cut", 20.0f, 1000.0f, 150.0f);
        params::addPercent (l, "width", "Width", 100.0f);
        params::addPercent (l, "mix", "Mix", 25.0f);
        return l;
    }

    // Mutually prime-ish delay sets (ms) at SIZE 50 %; SIZE scales them 0.5x .. 1.5x
    constexpr float kRoom[8]  { 11.3f, 13.7f, 17.1f, 19.3f, 23.9f, 27.1f, 31.3f, 37.7f };
    constexpr float kPlate[8] { 17.9f, 21.3f, 25.1f, 29.9f, 33.7f, 37.3f, 41.9f, 47.1f };
    constexpr float kHall[8]  { 29.7f, 37.1f, 41.1f, 43.7f, 47.3f, 53.9f, 59.3f, 67.1f };
    constexpr float kDiffuseMs[4] { 4.77f, 3.59f, 12.73f, 9.30f };
    constexpr float kMaxLineMs = 67.1f * 1.5f + 2.0f;   // + modulation depth
    constexpr float kModMs = 0.25f;
    constexpr int kModulated[3] { 0, 3, 5 };
}

EliteVerb::EliteVerb() : ElitePlugin (info, verbLayout())
{
    pType = raw ("type"); pPre = raw ("predelay"); pSize = raw ("size"); pDecay = raw ("decay");
    pDamp = raw ("damping"); pLowCut = raw ("lowcut"); pWidth = raw ("width"); pMix = raw ("mix");
}

void EliteVerb::prepareDsp (double fs, int)
{
    const int lineCap = (int) std::ceil (kMaxLineMs * 0.001 * fs) + 4;
    for (auto& l : lines) { l.buf.assign ((size_t) lineCap, 0.0f); l.write = 0; }
    for (int d = 0; d < kDiffusers; ++d)
    {
        diffusers[(size_t) d].length = juce::jmax (1, (int) std::lround (kDiffuseMs[d] * 0.001 * fs));
        diffusers[(size_t) d].buf.assign ((size_t) diffusers[(size_t) d].length, 0.0f);
        diffusers[(size_t) d].pos = 0;
    }
    pre.assign ((size_t) std::ceil (0.26 * fs) + 2, 0.0f);
    preWrite = 0;
    lfoInc = (float) (2.0 * std::numbers::pi * 0.6 / fs);
    mix.reset (fs, 0.03); width.reset (fs, 0.03);
    mix.setCurrentAndTargetValue (param (pMix) * 0.01f);
    width.setCurrentAndTargetValue (param (pWidth) * 0.01f);
    cache.fill (-1.0e9f);
    updateTargets();
    for (auto& l : lines) l.length = l.target;
}

void EliteVerb::resetDsp() noexcept
{
    for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.lp = 0.0f; }
    for (auto& d : diffusers) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
    std::fill (pre.begin(), pre.end(), 0.0f);
    for (auto& s : lowCutSt) s.reset();
}

void EliteVerb::updateTargets() noexcept
{
    const std::array<float, 8> now { param (pType), param (pSize), param (pDecay), param (pDamp), param (pLowCut), 0, 0, 0 };
    if (now == cache) return;
    cache = now;
    const int type = juce::jlimit (0, 2, (int) now[0]);
    const float* set = type == 0 ? kRoom : type == 1 ? kPlate : kHall;
    diffusion = type == 0 ? 0.62f : type == 1 ? 0.75f : 0.7f;
    const float scale = 0.5f + now[1] * 0.01f;
    const float rt60 = juce::jmax (0.1f, now[2]);
    for (int i = 0; i < kLines; ++i)
    {
        auto& l = lines[(size_t) i];
        l.target = juce::jmin ((float) l.buf.size() - 3.0f, set[i] * scale * 0.001f * (float) sampleRateHz);
        l.gain = std::pow (10.0f, -3.0f * l.target / (rt60 * (float) sampleRateHz));
    }
    dampCoeff = now[3] * 0.01f * 0.7f;
    lowCut = BiquadCoeffs::highPass (sampleRateHz, now[4], 0.7071068);
}

float EliteVerb::readFrac (const Line& l, float delay) const noexcept
{
    const int size = (int) l.buf.size();
    float rp = (float) l.write - delay;
    while (rp < 0.0f) rp += (float) size;
    const int i0 = (int) rp;
    const float frac = rp - (float) i0;
    const int i1 = (i0 + 1) % size;
    return l.buf[(size_t) (i0 % size)] * (1.0f - frac) + l.buf[(size_t) i1] * frac;
}

void EliteVerb::processDsp (float* left, float* right, int n) noexcept
{
    updateTargets();
    mix.setTargetValue (param (pMix) * 0.01f);
    width.setTargetValue (param (pWidth) * 0.01f);
    const int preSamples = juce::jlimit (0, (int) pre.size() - 2, (int) std::lround (param (pPre) * 0.001 * sampleRateHz));
    const float modDepth = kModMs * 0.001f * (float) sampleRateHz;
    constexpr float inGain = 0.35f;
    static constexpr float inSign[8] { 1, -1, 1, -1, -1, 1, -1, 1 };

    for (int i = 0; i < n; ++i)
    {
        const float dryL = left[i], dryR = right[i];
        // pre-delay
        pre[(size_t) preWrite] = 0.5f * (dryL + dryR);
        int rp = preWrite - preSamples; if (rp < 0) rp += (int) pre.size();
        float x = pre[(size_t) rp];
        if (++preWrite >= (int) pre.size()) preWrite = 0;

        // input diffusion (series all-pass)
        for (auto& d : diffusers)
        {
            const float delayed = d.buf[(size_t) d.pos];
            const float v = x + diffusion * delayed;
            d.buf[(size_t) d.pos] = v;
            x = delayed - diffusion * v;
            if (++d.pos >= d.length) d.pos = 0;
        }

        // FDN: read lines (a few slowly modulated), damp, decay
        lfoPhase += lfoInc; if (lfoPhase > 2.0f * std::numbers::pi_v<float>) lfoPhase -= 2.0f * std::numbers::pi_v<float>;
        float y[kLines]; float sum = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            auto& l = lines[(size_t) k];
            l.length += (l.target - l.length) * 0.0005f;
            float d = l.length;
            for (int m = 0; m < 3; ++m)
                if (kModulated[m] == k) d += modDepth * std::sin (lfoPhase * (1.0f + 0.37f * (float) m) + (float) m);
            const float v = readFrac (l, juce::jmax (1.0f, d));
            l.lp = v + dampCoeff * (l.lp - v);
            y[k] = l.lp * l.gain;
            sum += y[k];
        }
        // Householder feedback matrix + input, write back
        const float h = sum * (2.0f / (float) kLines);
        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            auto& l = lines[(size_t) k];
            l.buf[(size_t) l.write] = y[k] - h + inGain * inSign[k] * x;
            if (++l.write >= (int) l.buf.size()) l.write = 0;
            if (k % 2 == 0) outL += (k % 4 == 0 ? 1.0f : -1.0f) * y[k];
            else            outR += (k % 4 == 1 ? 1.0f : -1.0f) * y[k];
        }
        float wl = lowCutSt[0].process (lowCut, outL * 0.6f);
        float wr = lowCutSt[1].process (lowCut, outR * 0.6f);
        const float w = width.getNextValue();
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
        wl = mid + side; wr = mid - side;
        const float m = mix.getNextValue();
        left[i] = dryL + m * (wl - dryL);
        right[i] = dryR + m * (wr - dryR);
    }
}
} // namespace pg
