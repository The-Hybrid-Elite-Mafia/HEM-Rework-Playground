#include "EliteWidth.hpp"

namespace hem::pg
{
const PluginInfo EliteWidth::info { "elite-width", "Elite Width", "WIDE",
                                    "Stereo width with mono bass: wider (or narrower) highs, a tight mono low end", 0xff38e8b0 };

namespace
{
    params::Layout widthLayout()
    {
        params::Layout l;
        params::addFloat (l, "width", "Width", 0.0f, 200.0f, 100.0f, "%", 1.0f, 100.0f);
        params::addHz (l, "mono_below", "Mono Below", 20.0f, 500.0f, 20.0f);   // 20 Hz = off
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }
    constexpr double kButterQ = 0.7071067811865476;
}

EliteWidth::EliteWidth() : Plugin (info, widthLayout())
{
    pWidth = raw ("width"); pMonoBelow = raw ("mono_below"); pOutput = raw ("output");
}

void EliteWidth::prepareDsp (double fs, int)
{
    lastMono = -1.0f;
    width.reset (fs, 0.03); output.reset (fs, 0.03);
    width.setCurrentAndTargetValue (param (pWidth) * 0.01f);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    corrCoeff = dsp::timeCoeff (300.0, fs);
    resetDsp();
}

void EliteWidth::resetDsp() noexcept
{
    for (auto& s : sideHp) s.reset();
    corrLR = corrLL = corrRR = sideAvg = totalAvg = 0.0;
}

void EliteWidth::processDsp (float* left, float* right, int n) noexcept
{
    const float mono = param (pMonoBelow);
    if (mono != lastMono) { hp = BiquadCoeffs::highPass (sampleRateHz, mono, kButterQ); lastMono = mono; }
    const bool monoOn = mono > 20.5f;   // 20 Hz = off
    width.setTargetValue (param (pWidth) * 0.01f);
    output.setTargetValue (params::dbToGain (param (pOutput)));

    const double c = corrCoeff;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (left[i] + right[i]);
        float s = 0.5f * (left[i] - right[i]);
        if (monoOn) s = sideHp[1].process (hp, sideHp[0].process (hp, s));   // no side below MONO BELOW (24 dB/oct)
        s *= width.getNextValue();
        const float o = output.getNextValue();
        const float l = (m + s) * o, r = (m - s) * o;
        left[i] = l; right[i] = r;
        corrLR = c * corrLR + (1.0 - c) * (double) l * r;
        corrLL = c * corrLL + (1.0 - c) * (double) l * l;
        corrRR = c * corrRR + (1.0 - c) * (double) r * r;
        sideAvg = c * sideAvg + (1.0 - c) * (double) s * s;
        totalAvg = c * totalAvg + (1.0 - c) * ((double) m * m + (double) s * s);
    }
    const double den = std::sqrt (corrLL * corrRR);
    setReadout (correlation, den > 1.0e-12 ? (float) (corrLR / den) : 1.0f);
    setReadout (sideLevel, totalAvg > 1.0e-12 ? (float) (100.0 * sideAvg / totalAvg) : 0.0f);
}
} // namespace hem::pg
