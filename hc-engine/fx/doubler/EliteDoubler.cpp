#include "EliteDoubler.hpp"

namespace hem::pg
{
const PluginInfo EliteDoubler::info { "elite-doubler", "Elite Doubler", "DBLR",
                                      "Instant double-track: two drifting copies spread left and right behind the original", 0xffc77dff };

namespace
{
    params::Layout doublerLayout()
    {
        params::Layout l;
        params::addPercent (l, "amount", "Amount", 0.0f);
        params::addPercent (l, "width", "Width", 100.0f);
        params::addMs (l, "delay", "Delay", 5.0f, 40.0f, 18.0f);
        params::addPercent (l, "drift", "Drift", 40.0f);
        params::addHz (l, "tone", "Low Cut", 20.0f, 1000.0f, 150.0f);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }
    constexpr double kButterQ = 0.7071067811865476;
    constexpr float kMaxDriftMs = 1.5f;
    constexpr float kSecondVoice = 1.35f;   // the second copy sits a little later than the first

    inline float readFrac (const std::vector<float>& line, int writePos, float delaySamples) noexcept
    {
        const int size = (int) line.size();
        float rp = (float) writePos - delaySamples;
        while (rp < 0.0f) rp += (float) size;
        const int whole = (int) rp;
        const float fr = rp - (float) whole;
        // rp can round up to exactly `size` after the wrap above; 0.10.0 then read one sample past
        // the end of the line (a one-sample click, and a read outside the buffer). Fixed here.
        const int i0 = whole >= size ? whole - size : whole;
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return line[(size_t) i0] + fr * (line[(size_t) i1] - line[(size_t) i0]);
    }
}

EliteDoubler::EliteDoubler() : Plugin (info, doublerLayout())
{
    pAmount = raw ("amount"); pWidth = raw ("width"); pDelay = raw ("delay"); pDrift = raw ("drift");
    pTone = raw ("tone"); pOutput = raw ("output");
}

void EliteDoubler::prepareDsp (double fs, int)
{
    const double maxMs = 40.0 * kSecondVoice + kMaxDriftMs + 2.0;
    line.assign ((size_t) std::ceil (maxMs * 0.001 * fs) + 4, 0.0f);
    lastTone = -1.0f;
    for (auto* s : { &amount, &width, &delayMs, &drift, &output }) s->reset (fs, 0.05);
    amount.setCurrentAndTargetValue (param (pAmount) * 0.01f);
    width.setCurrentAndTargetValue (param (pWidth) * 0.01f);
    delayMs.setCurrentAndTargetValue (param (pDelay));
    drift.setCurrentAndTargetValue (param (pDrift) * 0.01f);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteDoubler::resetDsp() noexcept
{
    std::fill (line.begin(), line.end(), 0.0f);
    writePos = 0;
    phaseA = 0.0; phaseB = 0.37; phaseC = 0.71;
    for (auto& s : toneSt) s.reset();
}

void EliteDoubler::processDsp (float* left, float* right, int n) noexcept
{
    const float tone = param (pTone);
    if (tone != lastTone) { toneHp = BiquadCoeffs::highPass (sampleRateHz, tone, kButterQ); lastTone = tone; }
    amount.setTargetValue (param (pAmount) * 0.01f);
    width.setTargetValue (param (pWidth) * 0.01f);
    delayMs.setTargetValue (param (pDelay));
    drift.setTargetValue (param (pDrift) * 0.01f);
    output.setTargetValue (params::dbToGain (param (pOutput)));

    const double twoPi = 2.0 * std::numbers::pi;
    // three slow, unrelated wobbles so the copies never line up the same way twice
    const double incA = 0.31 / sampleRateHz, incB = 0.47 / sampleRateHz, incC = 0.13 / sampleRateHz;
    const float msToSamples = (float) (sampleRateHz * 0.001);
    const int size = (int) line.size();
    float spread = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        const float src = toneSt[1].process (toneHp, toneSt[0].process (toneHp, 0.5f * (l + r)));
        line[(size_t) writePos] = src;

        const float d = delayMs.getNextValue(), dr = drift.getNextValue() * kMaxDriftMs;
        const float slow = (float) std::sin (twoPi * phaseC);
        const float dA = d + dr * (0.7f * (float) std::sin (twoPi * phaseA) + 0.3f * slow);
        const float dB = d * kSecondVoice + dr * (0.7f * (float) std::sin (twoPi * phaseB) - 0.3f * slow);
        const float vA = readFrac (line, writePos, jmax (1.0f, dA * msToSamples));
        const float vB = readFrac (line, writePos, jmax (1.0f, dB * msToSamples));
        phaseA += incA; if (phaseA >= 1.0) phaseA -= 1.0;
        phaseB += incB; if (phaseB >= 1.0) phaseB -= 1.0;
        phaseC += incC; if (phaseC >= 1.0) phaseC -= 1.0;

        const float w = width.getNextValue(), a = amount.getNextValue(), o = output.getNextValue();
        const float near = 0.5f + 0.5f * w, far = 0.5f - 0.5f * w;   // copy A leans left, copy B leans right
        left[i] = (l + a * (vA * near + vB * far)) * o;
        right[i] = (r + a * (vB * near + vA * far)) * o;
        spread = dB - dA;
        if (++writePos >= size) writePos = 0;
    }
    setReadout (spreadMs, spread);
}
} // namespace hem::pg
