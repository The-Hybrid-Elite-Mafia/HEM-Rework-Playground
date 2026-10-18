#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteDeEss::info { "elite-de-ess", "Elite De-Ess", "DESS",
                                    "Sibilance tamer: turns down harsh S and T sounds only when they jump out", 0xffffc857 };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout deEssLayout()
    {
        params::Layout l;
        params::addHz (l, "freq", "Frequency", 3000.0f, 12000.0f, 6500.0f);
        params::addDb (l, "threshold", "Threshold", -60.0f, 0.0f, -30.0f);
        params::addDb (l, "range", "Range", 0.0f, 20.0f, 10.0f);
        params::addBool (l, "listen", "Listen (ess only)", false);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }
    constexpr double kButterQ = 0.7071067811865476;
}

EliteDeEss::EliteDeEss() : ElitePlugin (info, deEssLayout())
{
    pFreq = raw ("freq"); pThreshold = raw ("threshold"); pRange = raw ("range"); pListen = raw ("listen"); pOutput = raw ("output");
}

void EliteDeEss::prepareDsp (double fs, int)
{
    lastFreq = -1.0f;
    att = dsp::timeCoeff (1.0, fs);
    rel = dsp::timeCoeff (60.0, fs);
    grCoeff = dsp::timeCoeff (2.0, fs);
    output.reset (fs, 0.03);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteDeEss::resetDsp() noexcept
{
    for (auto* a : { &hpL, &hpR, &lpL, &lpR, &det }) for (auto& s : *a) s.reset();
    env = grSmooth = 0.0f;
}

void EliteDeEss::processDsp (float* left, float* right, int n) noexcept
{
    const float f = param (pFreq);
    if (f != lastFreq)
    {
        // the part that gets turned down: everything above ~70 % of FREQUENCY
        // Linkwitz-Riley split (two Butterworth stages each side): low + high add back up flat
        splitHp = BiquadCoeffs::highPass (sampleRateHz, f * 0.7, kButterQ);
        splitLp = BiquadCoeffs::lowPass (sampleRateHz, f * 0.7, kButterQ);
        // the detector listens to a band around FREQUENCY only
        detBp = BiquadCoeffs::bandPass (sampleRateHz, f, 1.4);
        lastFreq = f;
    }
    const float thr = param (pThreshold), range = param (pRange);
    const bool listen = param (pListen) > 0.5f;
    output.setTargetValue (params::dbToGain (param (pOutput)));

    float maxGr = 0.0f, maxSib = -120.0f;
    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        const float m = 0.5f * (l + r);
        const float d = det[1].process (detBp, det[0].process (detBp, m));
        const float x = std::abs (d);
        env = x > env ? x + att * (env - x) : x + rel * (env - x);
        const float levelDb = dsp::linToDb (env * 1.41421356f + 1.0e-9f);   // band level as a sine peak
        const float wantGr = juce::jlimit (0.0f, range, levelDb - thr);
        grSmooth = wantGr + grCoeff * (grSmooth - wantGr);
        const float g = dsp::dbToLin (-grSmooth);

        const float hl = hpL[1].process (splitHp, hpL[0].process (splitHp, l));
        const float hr = hpR[1].process (splitHp, hpR[0].process (splitHp, r));
        const float lowL = lpL[1].process (splitLp, lpL[0].process (splitLp, l));
        const float lowR = lpR[1].process (splitLp, lpR[0].process (splitLp, r));
        const float o = output.getNextValue();
        if (listen)
        {
            left[i] = hl * (1.0f - g) * o;     // only what is being taken away
            right[i] = hr * (1.0f - g) * o;
        }
        else
        {
            left[i] = (lowL + hl * g) * o;
            right[i] = (lowR + hr * g) * o;
        }
        maxGr = juce::jmax (maxGr, grSmooth);
        maxSib = juce::jmax (maxSib, levelDb);
    }
    setReadout (gr, maxGr);
    setReadout (sibilanceDb, maxSib);
}
} // namespace pg
