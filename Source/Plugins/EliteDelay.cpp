#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteDelay::info { "elite-delay", "Elite Delay", "DLY", "Stereo / ping-pong delay with tempo sync and ducking", 0xff1fb59a };

namespace
{
    constexpr double kMaxSeconds = 4.0;

    juce::AudioProcessorValueTreeState::ParameterLayout delayLayout()
    {
        params::Layout l;
        params::addMs (l, "time", "Time", 1.0f, 2000.0f, 375.0f);
        params::addBool (l, "sync", "Tempo Sync", true);
        params::addChoice (l, "division", "Note", EliteDelay::divisions(), 8);   // 1/4
        params::addPercent (l, "feedback", "Feedback", 30.0f);
        params::addHz (l, "lowcut", "Low Cut", 20.0f, 2000.0f, 150.0f);
        params::addHz (l, "highcut", "High Cut", 1000.0f, 20000.0f, 8000.0f);
        params::addPercent (l, "pingpong", "Ping-Pong", 0.0f);
        params::addPercent (l, "mix", "Mix", 25.0f);
        params::addPercent (l, "duck", "Duck", 0.0f);
        return l;
    }
}

double EliteDelay::divisionBeats (int index)
{
    static constexpr double beats[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return beats[juce::jlimit (0, 11, index)];
}

EliteDelay::EliteDelay() : ElitePlugin (info, delayLayout())
{
    pTime = raw ("time"); pSync = raw ("sync"); pDiv = raw ("division"); pFeedback = raw ("feedback");
    pLowCut = raw ("lowcut"); pHighCut = raw ("highcut"); pPing = raw ("pingpong"); pMix = raw ("mix"); pDuck = raw ("duck");
}

void EliteDelay::prepareDsp (double fs, int)
{
    const auto size = (size_t) std::ceil (kMaxSeconds * fs) + 4;
    bufL.assign (size, 0.0f); bufR.assign (size, 0.0f);
    write = 0;
    duckRel = dsp::timeCoeff (250.0, fs);
    feedback.reset (fs, 0.05); mix.reset (fs, 0.03); ping.reset (fs, 0.05);
    feedback.setCurrentAndTargetValue (param (pFeedback) * 0.01f * 0.95f);
    mix.setCurrentAndTargetValue (param (pMix) * 0.01f);
    ping.setCurrentAndTargetValue (param (pPing) * 0.01f);
    lastLow = lastHigh = -1.0f;
    current = -1.0f;
}

void EliteDelay::resetDsp() noexcept
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    for (auto& s : hpSt) s.reset();
    for (auto& s : lpSt) s.reset();
    duckEnv = 0.0f;
}

void EliteDelay::processDsp (float* left, float* right, int n) noexcept
{
    const bool sync = param (pSync) > 0.5f;
    const double seconds = sync ? divisionBeats ((int) param (pDiv)) * 60.0 / juce::jlimit (30.0, 300.0, (double) getTempo())
                                : param (pTime) * 0.001;
    const float target = (float) juce::jlimit (1.0, (double) bufL.size() - 3.0, seconds * sampleRateHz);
    if (current < 0.0f) current = target;

    const float lo = param (pLowCut), hi = param (pHighCut);
    if (lo != lastLow || hi != lastHigh)
    {
        hp = BiquadCoeffs::highPass (sampleRateHz, lo, 0.7071068);
        lp = BiquadCoeffs::lowPass (sampleRateHz, hi, 0.7071068);
        lastLow = lo; lastHigh = hi;
    }
    feedback.setTargetValue (param (pFeedback) * 0.01f * 0.95f);
    mix.setTargetValue (param (pMix) * 0.01f);
    ping.setTargetValue (param (pPing) * 0.01f);
    const float duck = param (pDuck) * 0.01f;
    const int size = (int) bufL.size();

    for (int i = 0; i < n; ++i)
    {
        // glide toward the new time (tape-style), at most 1/2000 of the distance per sample
        current += (target - current) * 0.0005f;
        float rp = (float) write - current;
        if (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp; const float frac = rp - (float) i0;
        const int i1 = (i0 + 1) % size;
        float wl = bufL[(size_t) i0] + frac * (bufL[(size_t) i1] - bufL[(size_t) i0]);
        float wr = bufR[(size_t) i0] + frac * (bufR[(size_t) i1] - bufR[(size_t) i0]);

        // filtered, softly saturated feedback (stays musical even at high feedback)
        float fl = lpSt[0].process (lp, hpSt[0].process (hp, wl));
        float fr = lpSt[1].process (lp, hpSt[1].process (hp, wr));
        fl = std::tanh (fl); fr = std::tanh (fr);

        const float xl = left[i], xr = right[i];
        const float p = ping.getNextValue(), fb = feedback.getNextValue();
        const float mono = 0.5f * (xl + xr);
        const float inL = (1.0f - p) * xl + p * mono;
        const float inR = (1.0f - p) * xr;
        bufL[(size_t) write] = inL + fb * ((1.0f - p) * fl + p * fr);
        bufR[(size_t) write] = inR + fb * ((1.0f - p) * fr + p * fl);
        if (++write >= size) write = 0;

        // ducking: the echoes step back while the dry signal is loud
        const float a = juce::jmax (std::abs (xl), std::abs (xr));
        duckEnv = a > duckEnv ? a : a + duckRel * (duckEnv - a);
        const float duckGain = 1.0f - duck * juce::jmin (1.0f, duckEnv * 4.0f);

        const float m = mix.getNextValue();
        left[i] = xl + m * (fl * duckGain - xl);
        right[i] = xr + m * (fr * duckGain - xr);
    }
    setReadout (delayMs, (float) (current / sampleRateHz * 1000.0));
}
} // namespace pg
