#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteComp::info { "elite-comp", "Elite Comp", "COMP", "Smooth compressor with auto release, Clean / Electro / Warm, parallel mix", 0xffe0a526 };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout compLayout()
    {
        params::Layout l;
        params::addDb (l, "threshold", "Threshold", -60.0f, 0.0f, -18.0f);
        params::addFloat (l, "ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 0.01f, 4.0f);
        params::addMs (l, "attack", "Attack", 0.05f, 200.0f, 10.0f);
        params::addMs (l, "release", "Release", 5.0f, 3000.0f, 150.0f);
        params::addBool (l, "arc", "Auto Release", true);
        params::addChoice (l, "mode", "Character", EliteComp::modes(), 0);
        params::addDb (l, "gain", "Gain", -12.0f, 24.0f, 0.0f);
        params::addPercent (l, "mix", "Mix", 100.0f);
        return l;
    }
}

EliteComp::EliteComp() : ElitePlugin (info, compLayout())
{
    pThr = raw ("threshold"); pRatio = raw ("ratio"); pAtk = raw ("attack"); pRel = raw ("release");
    pArc = raw ("arc"); pMode = raw ("mode"); pGain = raw ("gain"); pMix = raw ("mix");
}

void EliteComp::prepareDsp (double fs, int)
{
    comp.prepare (fs);
    cache.fill (-1.0e9f);
    makeup.reset (fs, 0.03); mix.reset (fs, 0.03);
    makeup.setCurrentAndTargetValue (params::dbToGain (param (pGain)));
    mix.setCurrentAndTargetValue (param (pMix) * 0.01f);
}

void EliteComp::resetDsp() noexcept { comp.reset(); }

void EliteComp::processDsp (float* left, float* right, int n) noexcept
{
    const std::array<float, 8> now { param (pThr), param (pRatio), param (pAtk), param (pRel), param (pArc), param (pMode), 0, 0 };
    const int mode = juce::jlimit (0, 2, (int) now[5]);
    if (now != cache)
    {
        dsp::CompressorCore::Settings s;
        s.thresholdDb = now[0]; s.ratio = now[1]; s.attackMs = now[2]; s.releaseMs = now[3]; s.autoRelease = now[4] > 0.5f;
        switch (mode)
        {
            case 1:  s.kneeDb = 1.0f;  s.detector = dsp::CompressorCore::Detector::peak; s.attackMs *= 0.5f; break;   // Electro: hard, snappy
            case 2:  s.kneeDb = 12.0f; s.detector = dsp::CompressorCore::Detector::rms; s.rmsMs = 10.0f; break;      // Warm: soft, smooth
            default: s.kneeDb = 6.0f;  s.detector = dsp::CompressorCore::Detector::peak; break;                       // Clean
        }
        comp.setSettings (s);
        cache = now;
    }
    makeup.setTargetValue (params::dbToGain (param (pGain)));
    mix.setTargetValue (param (pMix) * 0.01f);

    float maxGr = 0.0f;
    const bool warm = mode == 2;
    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        const float gr = comp.process (l, r);
        maxGr = juce::jmax (maxGr, gr);
        const float g = dsp::dbToLin (-gr) * makeup.getNextValue();
        float wl = l * g, wr = r * g;
        if (warm)
        {
            // gentle second/third-harmonic colour, unity for small signals
            wl = wl + 0.08f * wl * std::abs (wl) - 0.04f * wl * wl * wl;
            wr = wr + 0.08f * wr * std::abs (wr) - 0.04f * wr * wr * wr;
        }
        const float m = mix.getNextValue();
        left[i] = l + m * (wl - l);
        right[i] = r + m * (wr - r);
    }
    setReadout (gr, maxGr);
}
} // namespace pg
