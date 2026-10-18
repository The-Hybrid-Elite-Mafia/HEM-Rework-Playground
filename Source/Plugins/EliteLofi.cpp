#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteLofi::info { "elite-lofi", "Elite Lo-Fi", "LOFI",
                                   "Old tape and sampler character: wow, flutter, crushed bits, vinyl crackle and hiss", 0xffe8a87c };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout lofiLayout()
    {
        params::Layout l;
        params::addPercent (l, "wow", "Wow", 20.0f);
        params::addPercent (l, "flutter", "Flutter", 15.0f);
        params::addFloat (l, "bits", "Bits", 4.0f, 16.0f, 16.0f, "bit", 1.0f);           // 16 = off
        params::addFloat (l, "rate", "Sample Rate", 2000.0f, 48000.0f, 48000.0f, "Hz", 1.0f, 12000.0f);   // 48000 = off
        params::addPercent (l, "vinyl", "Vinyl", 20.0f);
        params::addHz (l, "tone", "Tone", 1000.0f, 20000.0f, 12000.0f);
        params::addPercent (l, "mix", "Mix", 100.0f);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }
    constexpr double kButterQ = 0.7071067811865476;
    constexpr float kWowMs = 4.0f, kFlutterMs = 0.4f;     // depth at 100 %
    constexpr double kWowHz = 0.55, kFlutterHz = 6.3;

    inline float readFrac (const std::vector<float>& line, int writePos, float delaySamples) noexcept
    {
        const int size = (int) line.size();
        float rp = (float) writePos - delaySamples;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp;
        if (i0 >= size) i0 -= size;
        const float fr = rp - std::floor (rp);
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return line[(size_t) i0] + fr * (line[(size_t) i1] - line[(size_t) i0]);
    }
}

EliteLofi::EliteLofi() : ElitePlugin (info, lofiLayout())
{
    pWow = raw ("wow"); pFlutter = raw ("flutter"); pBits = raw ("bits"); pRate = raw ("rate");
    pVinyl = raw ("vinyl"); pTone = raw ("tone"); pMix = raw ("mix"); pOutput = raw ("output");
}

void EliteLofi::prepareDsp (double fs, int)
{
    const size_t size = (size_t) std::ceil (2.0 * (kWowMs + kFlutterMs) * 0.001 * fs) + 8;
    lineL.assign (size, 0.0f);
    lineR.assign (size, 0.0f);
    lastTone = -1.0f;
    noiseHp = BiquadCoeffs::highPass (fs, 400.0, kButterQ);
    mix.reset (fs, 0.03); output.reset (fs, 0.03);
    mix.setCurrentAndTargetValue (param (pMix) * 0.01f);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteLofi::resetDsp() noexcept
{
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    writePos = 0;
    wowPhase = flutterPhase = 0.0;
    holdL = holdR = 0.0f; holdCount = 1.0f;
    for (auto& s : toneSt) s.reset();
    noiseSt.reset();
    rng = 0x48454D21u;   // same crackle pattern every time playback starts (renders are repeatable)
    crackleEnv = 0.0f;
}

void EliteLofi::processDsp (float* left, float* right, int n) noexcept
{
    const float tone = param (pTone);
    if (tone != lastTone) { toneLp = BiquadCoeffs::lowPass (sampleRateHz, tone, kButterQ); lastTone = tone; }
    const float wowMs = param (pWow) * 0.01f * kWowMs, flutterMs = param (pFlutter) * 0.01f * kFlutterMs;
    const float bits = param (pBits);
    const bool crush = bits < 15.5f;
    const float steps = std::exp2 (std::round (bits) - 1.0f);
    const float rate = param (pRate);
    const float holdStep = rate >= 47999.0f ? 1.0f : juce::jlimit (0.0f, 1.0f, (float) (rate / sampleRateHz));   // top = off at any session rate
    const float vinyl = param (pVinyl) * 0.01f;
    const float hissGain = 0.004f * vinyl;
    const float cracklesPerSample = (float) (8.0 * vinyl / sampleRateHz);   // up to 8 pops a second
    const float crackleDecay = dsp::timeCoeff (0.6, sampleRateHz);
    mix.setTargetValue (param (pMix) * 0.01f);
    output.setTargetValue (params::dbToGain (param (pOutput)));

    const double twoPi = 2.0 * std::numbers::pi;
    const float msToSamples = (float) (sampleRateHz * 0.001);
    const int size = (int) lineL.size();
    auto nextRandom = [this]() noexcept
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) rng * (1.0f / 4294967296.0f);   // 0..1
    };

    for (int i = 0; i < n; ++i)
    {
        const float dryL = left[i], dryR = right[i];
        lineL[(size_t) writePos] = dryL;
        lineR[(size_t) writePos] = dryR;
        // tape speed wobble: the read point swings between "now" and up to 2x the depth back
        const float d = (wowMs * (1.0f + (float) std::sin (twoPi * wowPhase))
                       + flutterMs * (1.0f + (float) std::sin (twoPi * flutterPhase))) * msToSamples;
        float l = readFrac (lineL, writePos, d);
        float r = readFrac (lineR, writePos, d);
        wowPhase += kWowHz / sampleRateHz; if (wowPhase >= 1.0) wowPhase -= 1.0;
        flutterPhase += kFlutterHz / sampleRateHz; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        if (++writePos >= size) writePos = 0;

        // old-sampler rate: hold each sample until the slower clock ticks
        holdCount += holdStep;
        if (holdCount >= 1.0f) { holdCount -= 1.0f; holdL = l; holdR = r; }
        l = holdL; r = holdR;
        if (crush) { l = std::round (l * steps) / steps; r = std::round (r * steps) / steps; }

        l = toneSt[0].process (toneLp, l);
        r = toneSt[1].process (toneLp, r);

        if (vinyl > 0.0f)
        {
            const float white = nextRandom() * 2.0f - 1.0f;
            if (nextRandom() < cracklesPerSample) crackleEnv = 0.05f + 0.25f * nextRandom();
            const float pop = crackleEnv * (nextRandom() * 2.0f - 1.0f);
            crackleEnv *= crackleDecay;
            const float noise = noiseSt.process (noiseHp, white * hissGain) + pop * vinyl;
            l += noise; r += noise;
        }
        const float m = mix.getNextValue(), o = output.getNextValue();
        left[i] = (dryL + m * (l - dryL)) * o;
        right[i] = (dryR + m * (r - dryR)) * o;
    }
}
} // namespace pg
