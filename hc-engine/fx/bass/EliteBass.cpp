#include "EliteBass.hpp"

namespace hem::pg
{
const PluginInfo EliteBass::info { "elite-bass", "Elite Bass", "BASS", "Bigger 808s: adds bass harmonics you hear on small speakers", 0xffe8502a };

namespace
{
    params::Layout bassLayout()
    {
        params::Layout l;
        params::addHz (l, "freq", "Frequency", 20.0f, 150.0f, 60.0f);
        params::addPercent (l, "intensity", "Intensity", 0.0f);
        params::addDb (l, "original", "Original Bass", -60.0f, 0.0f, 0.0f);   // -60 = muted
        params::addDb (l, "output", "Output", -24.0f, 12.0f, 0.0f);
        params::addBool (l, "solo", "Harmonics Only", false);
        return l;
    }

    // Chebyshev polynomials: for x = cos(t) they give exactly cos(2t), cos(3t), cos(4t).
    inline float cheb2 (float x) noexcept { return 2.0f * x * x - 1.0f; }
    inline float cheb3 (float x) noexcept { return x * (4.0f * x * x - 3.0f); }
    inline float cheb4 (float x) noexcept { const float x2 = x * x; return 8.0f * x2 * x2 - 8.0f * x2 + 1.0f; }
}

EliteBass::EliteBass() : Plugin (info, bassLayout())
{
    pFreq = raw ("freq"); pIntensity = raw ("intensity"); pOriginal = raw ("original"); pOutput = raw ("output"); pSolo = raw ("solo");
}

void EliteBass::updateFilters (float freq) noexcept
{
    const double f = jlimit (20.0, 150.0, (double) freq);
    lp = BiquadCoeffs::lowPass (sampleRateHz, f, 0.7071068);    // two of these = Linkwitz-Riley 24 dB/oct
    hp = BiquadCoeffs::highPass (sampleRateHz, f, 0.7071068);
    hHp = BiquadCoeffs::highPass (sampleRateHz, f * 1.5, 0.7071068);   // keep only what is above the fundamental
    hLp = BiquadCoeffs::lowPass (sampleRateHz, f * 6.0, 0.7071068);    // ... and below the 5th-6th harmonic
    lastFreq = freq;
}

void EliteBass::prepareDsp (double fs, int)
{
    envAtt = dsp::timeCoeff (2.0, fs);
    envRel = dsp::timeCoeff (40.0, fs);
    intensity.reset (fs, 0.03); original.reset (fs, 0.03); output.reset (fs, 0.03);
    intensity.setCurrentAndTargetValue (param (pIntensity) * 0.01f);
    original.setCurrentAndTargetValue (param (pOriginal) <= -59.9f ? 0.0f : params::dbToGain (param (pOriginal)));
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    updateFilters (param (pFreq));
}

void EliteBass::resetDsp() noexcept
{
    for (auto& ch : lowSt) for (auto& s : ch) s.reset();
    for (auto& ch : highSt) for (auto& s : ch) s.reset();
    for (auto& s : hHpSt) s.reset();
    for (auto& s : hLpSt) s.reset();
    env = 0.0f;
}

void EliteBass::processDsp (float* left, float* right, int n) noexcept
{
    const float f = param (pFreq);
    if (std::abs (f - lastFreq) > 0.01f) updateFilters (f);
    intensity.setTargetValue (param (pIntensity) * 0.01f);
    original.setTargetValue (param (pOriginal) <= -59.9f ? 0.0f : params::dbToGain (param (pOriginal)));
    output.setTargetValue (params::dbToGain (param (pOutput)));
    const bool solo = param (pSolo) > 0.5f;

    float lowPeak = 0.0f, harmPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float low[2], high[2];
        const float in[2] { left[i], right[i] };
        for (int c = 0; c < 2; ++c)
        {
            low[c] = lowSt[(size_t) c][1].process (lp, lowSt[(size_t) c][0].process (lp, in[c]));
            high[c] = highSt[(size_t) c][1].process (hp, highSt[(size_t) c][0].process (hp, in[c]));
        }

        // Harmonics from the mono bass: normalise by its envelope so the Chebyshev polynomials see
        // a unit-amplitude wave (clean, predictable harmonics), then scale back by the envelope.
        const float mono = 0.5f * (low[0] + low[1]);
        const float a = std::abs (mono);
        env = a > env ? a + envAtt * (env - a) : a + envRel * (env - a);
        const float x = jlimit (-1.0f, 1.0f, mono / (env + 1.0e-5f));
        float h = env * (0.55f * cheb2 (x) + 0.35f * cheb3 (x) + 0.2f * cheb4 (x));
        h = hHpSt[1].process (hHp, hHpSt[0].process (hHp, h));
        h = hLpSt[1].process (hLp, hLpSt[0].process (hLp, h));

        const float hg = 2.0f * intensity.getNextValue();   // up to +6 dB of harmonics
        const float og = original.getNextValue();
        const float out = output.getNextValue();
        if (solo)
        {
            left[i] = h * hg * out;
            right[i] = h * hg * out;
        }
        else
        {
            // LR4 low + high sum back to the input (all-pass); the original sub is scaled by ORIGINAL
            left[i]  = (high[0] + og * low[0] + hg * h) * out;
            right[i] = (high[1] + og * low[1] + hg * h) * out;
        }
        lowPeak = jmax (lowPeak, std::abs (mono) * og);
        harmPeak = jmax (harmPeak, std::abs (h * hg));
    }
    setReadout (originalLevel, params::gainToDb (lowPeak));
    setReadout (harmonicsLevel, params::gainToDb (harmPeak));
}
} // namespace hem::pg
