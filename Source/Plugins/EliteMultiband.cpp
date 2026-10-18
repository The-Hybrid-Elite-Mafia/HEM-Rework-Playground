#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteMultiband::info { "elite-multiband", "Elite Multiband", "MBND",
                                        "Three-band compressor: control lows, mids and highs on their own (mastering, busses)", 0xff4fd1ff };

namespace
{
    constexpr double kButterQ = 0.7071067811865476;   // two in a row = Linkwitz-Riley 24 dB/oct

    juce::AudioProcessorValueTreeState::ParameterLayout multibandLayout()
    {
        params::Layout l;
        params::addHz (l, "low_x", "Low / Mid", 40.0f, 1000.0f, 180.0f);
        params::addHz (l, "high_x", "Mid / High", 1000.0f, 12000.0f, 3500.0f);
        const char* names[] { "Low", "Mid", "High" };
        for (int b = 0; b < 3; ++b)
        {
            const juce::String n (names[b]);
            params::addDb (l, EliteMultiband::id (b, "thr").toRawUTF8(), (n + " Threshold").toRawUTF8(), -50.0f, 0.0f, 0.0f);
            params::addFloat (l, EliteMultiband::id (b, "ratio").toRawUTF8(), (n + " Ratio").toRawUTF8(), 1.0f, 10.0f, 2.0f, ":1", 0.01f, 3.0f);
            params::addDb (l, EliteMultiband::id (b, "gain").toRawUTF8(), (n + " Gain").toRawUTF8(), -12.0f, 12.0f, 0.0f);
        }
        params::addMs (l, "attack", "Attack", 0.5f, 100.0f, 15.0f);
        params::addMs (l, "release", "Release", 20.0f, 1000.0f, 150.0f);
        params::addPercent (l, "mix", "Mix", 100.0f);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }

    inline float run2 (std::array<BiquadState, 2>& st, const BiquadCoeffs& c, float x) noexcept
    {
        return st[1].process (c, st[0].process (c, x));
    }
}

EliteMultiband::EliteMultiband() : ElitePlugin (info, multibandLayout())
{
    for (int b = 0; b < 3; ++b)
    {
        bands[(size_t) b].thr = raw (id (b, "thr"));
        bands[(size_t) b].ratio = raw (id (b, "ratio"));
        bands[(size_t) b].gain = raw (id (b, "gain"));
    }
    pLowX = raw ("low_x"); pHighX = raw ("high_x"); pAttack = raw ("attack"); pRelease = raw ("release");
    pMix = raw ("mix"); pOutput = raw ("output");
}

void EliteMultiband::prepareDsp (double fs, int)
{
    lastLowX = lastHighX = lastAtk = lastRel = -1.0f;
    lastThr.fill (-999.0f); lastRatio.fill (0.0f);
    for (auto& b : bands)
    {
        b.comp.prepare (fs);
        b.g.reset (fs, 0.03);
        b.g.setCurrentAndTargetValue (params::dbToGain (b.gain->load()));
    }
    mix.reset (fs, 0.03); output.reset (fs, 0.03);
    mix.setCurrentAndTargetValue (param (pMix) * 0.01f);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    updateFilters();
    resetDsp();
}

void EliteMultiband::resetDsp() noexcept
{
    for (auto* set : { &sLp1, &sHp1, &sLp2, &sHp2, &sApLo, &sApHi })
        for (auto& ch : *set) for (auto& s : ch) s.reset();
    for (auto& b : bands) b.comp.reset();
}

void EliteMultiband::updateFilters() noexcept
{
    const float lowX = param (pLowX);
    const float highX = juce::jmax (param (pHighX), lowX * 1.5f);   // keep the mid band at least half an octave wide
    if (lowX == lastLowX && highX == lastHighX) return;
    lp1 = BiquadCoeffs::lowPass (sampleRateHz, lowX, kButterQ);
    hp1 = BiquadCoeffs::highPass (sampleRateHz, lowX, kButterQ);
    lp2 = BiquadCoeffs::lowPass (sampleRateHz, highX, kButterQ);
    hp2 = BiquadCoeffs::highPass (sampleRateHz, highX, kButterQ);
    lastLowX = lowX; lastHighX = highX;
}

void EliteMultiband::processDsp (float* left, float* right, int n) noexcept
{
    updateFilters();
    const float atk = param (pAttack), rel = param (pRelease);
    for (int b = 0; b < 3; ++b)
    {
        auto& band = bands[(size_t) b];
        const float thr = band.thr->load(), ratio = band.ratio->load();
        if (thr != lastThr[(size_t) b] || ratio != lastRatio[(size_t) b] || atk != lastAtk || rel != lastRel)
        {
            dsp::CompressorCore::Settings s;
            s.thresholdDb = thr; s.ratio = ratio; s.kneeDb = 6.0f;
            s.attackMs = atk; s.releaseMs = rel; s.autoRelease = false;
            s.detector = dsp::CompressorCore::Detector::rms; s.rmsMs = 10.0f;
            band.comp.setSettings (s);
            lastThr[(size_t) b] = thr; lastRatio[(size_t) b] = ratio;
        }
        band.g.setTargetValue (params::dbToGain (band.gain->load()));
    }
    lastAtk = atk; lastRel = rel;
    mix.setTargetValue (param (pMix) * 0.01f);
    output.setTargetValue (params::dbToGain (param (pOutput)));

    std::array<float, 3> maxGr { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < n; ++i)
    {
        const float in[2] { left[i], right[i] };
        float lo[2], mi[2], hi[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const float l = run2 (sLp1[(size_t) ch], lp1, in[ch]);
            const float rest = run2 (sHp1[(size_t) ch], hp1, in[ch]);
            mi[ch] = run2 (sLp2[(size_t) ch], lp2, rest);
            hi[ch] = run2 (sHp2[(size_t) ch], hp2, rest);
            // the low band gets the same phase turn the mid + high pair got at the upper crossover,
            // so all three add back up flat (LR4 all-pass = LP + HP at that frequency)
            lo[ch] = run2 (sApLo[(size_t) ch], lp2, l) + run2 (sApHi[(size_t) ch], hp2, l);
        }
        float outL = 0.0f, outR = 0.0f;
        float* parts[3][2] { { &lo[0], &lo[1] }, { &mi[0], &mi[1] }, { &hi[0], &hi[1] } };
        for (int b = 0; b < 3; ++b)
        {
            auto& band = bands[(size_t) b];
            const float grDb = band.comp.process (*parts[b][0], *parts[b][1]);
            maxGr[(size_t) b] = juce::jmax (maxGr[(size_t) b], grDb);
            const float g = dsp::dbToLin (-grDb) * band.g.getNextValue();
            outL += *parts[b][0] * g;
            outR += *parts[b][1] * g;
        }
        const float m = mix.getNextValue(), o = output.getNextValue();
        // MIX blends against the split-and-summed signal (same phase), so parallel use does not comb
        const float dryL = lo[0] + mi[0] + hi[0], dryR = lo[1] + mi[1] + hi[1];
        left[i] = (dryL + m * (outL - dryL)) * o;
        right[i] = (dryR + m * (outR - dryR)) * o;
    }
    setReadout (lowGr, maxGr[0]);
    setReadout (midGr, maxGr[1]);
    setReadout (highGr, maxGr[2]);
}
} // namespace pg
