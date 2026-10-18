#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteTune::info { "elite-tune", "Elite Tune", "TUNE",
                                   "Vocal pitch correction: pulls a single voice to the nearest note of your key, natural or hard snap", 0xff4ff0e0 };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout tuneLayout()
    {
        params::Layout l;
        params::addChoice (l, "key", "Key", EliteTune::keys(), 0);
        params::addChoice (l, "scale", "Scale", EliteTune::scales(), 0);
        params::addFloat (l, "speed", "Retune Speed", 0.0f, 400.0f, 25.0f, "ms", 1.0f, 50.0f);   // 0 = instant snap
        params::addPercent (l, "amount", "Amount", 100.0f);
        params::addHz (l, "low", "Lowest Note", 50.0f, 400.0f, 80.0f);
        params::addHz (l, "high", "Highest Note", 200.0f, 1500.0f, 1000.0f);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }

    constexpr float kYinThreshold = 0.15f;   // lower = stricter about what counts as a clear note
    constexpr float kMaxCents = 300.0f;      // never bend more than this (protects against octave mistakes)

    inline float readFrac (const std::vector<float>& line, int writePos, double delaySamples) noexcept
    {
        const int size = (int) line.size();
        double rp = (double) writePos - delaySamples;
        while (rp < 0.0) rp += (double) size;
        int i0 = (int) rp;
        if (i0 >= size) i0 -= size;
        const float fr = (float) (rp - std::floor (rp));
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        return line[(size_t) i0] + fr * (line[(size_t) i1] - line[(size_t) i0]);
    }
}

float EliteTune::snapToScale (float midi, int key, int scale) noexcept
{
    static constexpr bool major[12] { true, false, true, false, true, true, false, true, false, true, false, true };
    static constexpr bool minor[12] { true, false, true, true, false, true, false, true, true, false, true, false };
    if (scale <= 0) return std::round (midi);
    const bool* allowed = scale == 1 ? major : minor;
    const int centre = (int) std::round (midi);
    float best = (float) centre, bestDist = 1.0e9f;
    for (int n = centre - 2; n <= centre + 2; ++n)
    {
        const int degree = ((n - key) % 12 + 12) % 12;
        if (! allowed[degree]) continue;
        const float dist = std::abs ((float) n - midi);
        if (dist < bestDist) { bestDist = dist; best = (float) n; }
    }
    return best;
}

EliteTune::EliteTune() : ElitePlugin (info, tuneLayout())
{
    pKey = raw ("key"); pScale = raw ("scale"); pSpeed = raw ("speed"); pAmount = raw ("amount");
    pLow = raw ("low"); pHigh = raw ("high"); pOutput = raw ("output");
}

void EliteTune::prepareDsp (double fs, int)
{
    decim = juce::jmax (1, (int) std::lround (fs / 24000.0));
    anaRate = fs / (double) decim;
    anaWin = (int) std::lround (0.02 * anaRate);                  // 20 ms integration window
    tauMaxAlloc = (int) std::ceil (anaRate / 50.0) + 2;            // down to 50 Hz
    hop = juce::jmax (32, (int) std::lround (0.005 * anaRate));   // a new reading every ~5 ms
    ana.assign ((size_t) (anaWin + tauMaxAlloc), 0.0f);
    frame.assign (ana.size(), 0.0f);
    yin.assign ((size_t) tauMaxAlloc + 1, 0.0f);
    shiftWin = (int) std::lround (0.03 * fs);                      // 30 ms grain: smooth on voices
    lineL.assign ((size_t) shiftWin + 4, 0.0f);
    lineR.assign ((size_t) shiftWin + 4, 0.0f);
    output.reset (fs, 0.03);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteTune::resetDsp() noexcept
{
    std::fill (ana.begin(), ana.end(), 0.0f);
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    anaPos = hopCount = decimCount = writePos = 0;
    decimAcc = 0.0f;
    phase = 0.5;
    ratio = targetRatio = 1.0f;
    lastHz = lastCents = lastTarget = 0.0f;
}

float EliteTune::detectPitch() noexcept
{
    // YIN (de Cheveigne & Kawahara 2002) on the most recent anaWin + tauMax samples
    const int size = (int) ana.size();
    for (int i = 0; i < size; ++i) frame[(size_t) i] = ana[(size_t) ((anaPos + i) % size)];
    const float lowHz = param (pLow), highHz = juce::jmax (param (pHigh), lowHz * 1.5f);
    const int tauMin = juce::jmax (2, (int) std::floor (anaRate / highHz));
    const int tauMax = juce::jmin (tauMaxAlloc - 1, (int) std::ceil (anaRate / lowHz));
    const int W = anaWin;
    const float* x = frame.data() + (size - W - tauMax);   // start so x[j + tau] stays inside the frame

    float energy = 0.0f;
    for (int j = 0; j < W; ++j) energy += x[j] * x[j];
    if (energy < (float) W * 1.0e-6f) return 0.0f;   // quieter than about -60 dBFS: nothing to tune

    yin[0] = 1.0f;
    float running = 0.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float d = 0.0f;
        for (int j = 0; j < W; ++j) { const float diff = x[j] - x[j + tau]; d += diff * diff; }
        running += d;
        yin[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;   // cumulative mean normalised
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (yin[(size_t) tau] < kYinThreshold)
        {
            while (tau + 1 < tauMax && yin[(size_t) tau + 1] < yin[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) return 0.0f;   // breath, noise, consonant: leave it alone
    // parabola through the three points around the dip for a sub-sample period
    const float a = yin[(size_t) best - 1], b = yin[(size_t) best], c = yin[(size_t) best + 1];
    const float den = a - 2.0f * b + c;
    const float shift = std::abs (den) > 1.0e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den) : 0.0f;
    return (float) (anaRate / ((double) best + shift));
}

void EliteTune::processDsp (float* left, float* right, int n) noexcept
{
    const int key = juce::jlimit (0, 11, (int) param (pKey));
    const int scale = juce::jlimit (0, 2, (int) param (pScale));
    const float speedMs = param (pSpeed), amount = param (pAmount) * 0.01f;
    const float glide = speedMs < 0.5f ? 0.0f : dsp::timeCoeff (speedMs, sampleRateHz);
    output.setTargetValue (params::dbToGain (param (pOutput)));
    const double W = (double) shiftWin;
    const double maxNudge = 0.006 / W;   // drift back to the resting point at no more than ~10 cents
    const int lineSize = (int) lineL.size();
    const int anaSize = (int) ana.size();
    const double pi = std::numbers::pi;

    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];

        // ---- listen: build the low-rate mono copy and look for the note every hop
        decimAcc += 0.5f * (l + r);
        if (++decimCount >= decim)
        {
            ana[(size_t) anaPos] = decimAcc / (float) decim;
            if (++anaPos >= anaSize) anaPos = 0;
            decimAcc = 0.0f; decimCount = 0;
            if (++hopCount >= hop)
            {
                hopCount = 0;
                const float hz = detectPitch();
                lastHz = hz;
                if (hz > 0.0f)
                {
                    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
                    const float target = snapToScale (midi, key, scale);
                    const float cents = juce::jlimit (-kMaxCents, kMaxCents, (target - midi) * 100.0f * amount);
                    targetRatio = std::exp2 (cents / 1200.0f);
                    lastCents = cents; lastTarget = target;
                }
                else
                {
                    targetRatio = 1.0f;
                    lastCents = 0.0f;
                }
            }
        }

        // ---- correct: glide toward the target ratio and read the delay line with two moving heads
        ratio = targetRatio + glide * (ratio - targetRatio);
        lineL[(size_t) writePos] = l;
        lineR[(size_t) writePos] = r;
        phase += (1.0 - (double) ratio) / W;
        if (std::abs (ratio - 1.0f) < 1.0e-4f && std::abs (targetRatio - 1.0f) < 1.0e-4f)
            phase += juce::jlimit (-maxNudge, maxNudge, 0.5 - phase);   // no correction: settle to one clean head
        phase -= std::floor (phase);
        const double phaseB = phase + 0.5 - (phase >= 0.5 ? 1.0 : 0.0);
        const float sA = (float) std::sin (pi * phase);
        const float gA = sA * sA;
        const float gB = 1.0f - gA;
        const float o = output.getNextValue();
        left[i] = (gA * readFrac (lineL, writePos, phase * W) + gB * readFrac (lineL, writePos, phaseB * W)) * o;
        right[i] = (gA * readFrac (lineR, writePos, phase * W) + gB * readFrac (lineR, writePos, phaseB * W)) * o;
        if (++writePos >= lineSize) writePos = 0;
    }
    setReadout (detectedHz, lastHz);
    setReadout (correctionCents, lastCents);
    setReadout (targetMidi, lastTarget);
}
} // namespace pg
