#include "EliteSwirl.hpp"

namespace hem::pg
{
const PluginInfo EliteSwirl::info { "elite-swirl", "Elite Swirl Killer", "SWRL",
                                    "Kills AI swirl / shimmer in the stereo sides, keeps the centre untouched (3D view)", 0xff8f6bff };

namespace
{
    params::Layout swirlLayout()
    {
        params::Layout l;
        params::addPercent (l, "amount", "Amount", 0.0f);
        params::addHz (l, "from", "From", 1000.0f, 16000.0f, 4000.0f);
        params::addPercent (l, "protect", "Protect Width", 40.0f);
        params::addChoice (l, "mode", "Mode", EliteSwirl::modes(), 1);   // Repair
        params::addBool (l, "listen", "Listen (removed only)", false);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        params::addPercent (l, "whistle", "Whistle Hunter", 0.0f);       // side-only notches on steady whines
        params::addPercent (l, "shield", "Transient Shield", 60.0f);     // ease off on drum hits
        return l;
    }

    constexpr float kEps = 1.0e-12f;
    constexpr float kFloorGain = 0.05f;        // at most -26 dB on a Side bin
    constexpr float kLowBandHz = 40.0f, kHighBandHz = 20000.0f;

    inline float median5 (const std::array<float, 5>& v) noexcept
    {
        auto a = v;
        std::sort (a.begin(), a.end());
        return a[2];
    }

    inline float smoothstep (float e0, float e1, float x) noexcept
    {
        const float t = jlimit (0.0f, 1.0f, (x - e0) / (e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }
}

float EliteSwirl::bandHz (int b) noexcept
{
    return kLowBandHz * std::pow (kHighBandHz / kLowBandHz, ((float) b + 0.5f) / (float) kBands);
}

EliteSwirl::EliteSwirl() : Plugin (info, swirlLayout())
{
    pAmount = raw ("amount"); pFrom = raw ("from"); pProtect = raw ("protect");
    pMode = raw ("mode"); pListen = raw ("listen"); pOutput = raw ("output");
    pWhistle = raw ("whistle"); pShield = raw ("shield");
}

void EliteSwirl::prepareDsp (double fs, int)
{
    fftSize = fs > 64000.0 ? 4096 : 2048;
    hop = fftSize / 4;
    fft.prepare (fftSize);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)   // sqrt periodic Hann: analysis * synthesis = Hann, 4x overlap sums to 2
        window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * std::numbers::pi_v<float> * (float) i / (float) fftSize));
    for (auto* v : { &inL, &inR, &outL, &outR }) v->assign ((size_t) fftSize, 0.0f);
    bufL.assign ((size_t) fftSize, {});
    bufR.assign ((size_t) fftSize, {});
    const size_t bins = (size_t) fftSize / 2 + 1;
    sPowAvg.assign (bins, 0.0f);
    smCross.assign (bins, {});
    mPowAvg.assign (bins, 0.0f);
    gain.assign (bins, 1.0f);
    gainTmp.assign (bins, 1.0f);
    sideSlow.assign (bins, 0.0f);
    whistleGain.assign (bins, 1.0f);
    whistleTmp.assign (bins, 1.0f);
    sideHist.assign (bins, {});
    bandOf.assign (bins, -1);
    bandCount.assign ((size_t) kBands, 0.0f);
    for (size_t k = 1; k < bins; ++k)
    {
        const float f = (float) k * (float) fs / (float) fftSize;
        if (f < kLowBandHz || f >= kHighBandHz) continue;
        const int b = jlimit (0, kBands - 1, (int) (std::log (f / kLowBandHz) / std::log (kHighBandHz / kLowBandHz) * (float) kBands));
        bandOf[k] = b;
        bandCount[(size_t) b] += 1.0f;
    }
    output.reset (fs, 0.03);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
}

void EliteSwirl::resetDsp() noexcept
{
    for (auto* v : { &inL, &inR, &outL, &outR }) std::fill (v->begin(), v->end(), 0.0f);
    std::fill (sPowAvg.begin(), sPowAvg.end(), 0.0f);
    std::fill (smCross.begin(), smCross.end(), std::complex<float>());
    std::fill (mPowAvg.begin(), mPowAvg.end(), 0.0f);
    std::fill (gain.begin(), gain.end(), 1.0f);
    for (auto& h : sideHist) h.fill (0.0f);
    std::fill (sideSlow.begin(), sideSlow.end(), 0.0f);
    std::fill (whistleGain.begin(), whistleGain.end(), 1.0f);
    pos = hopCount = histPos = 0;
    hfAvg = 0.0f; corrAvg = 1.0f; shieldHold = 0;
}

void EliteSwirl::processDsp (float* left, float* right, int n) noexcept
{
    output.setTargetValue (params::dbToGain (param (pOutput)));
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t) pos] = left[i];
        inR[(size_t) pos] = right[i];
        const float g = output.getNextValue();
        left[i] = outL[(size_t) pos] * g;
        right[i] = outR[(size_t) pos] * g;
        outL[(size_t) pos] = 0.0f;
        outR[(size_t) pos] = 0.0f;
        if (++pos >= fftSize) pos = 0;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

void EliteSwirl::processFrame() noexcept
{
    const int N = fftSize;
    // oldest sample sits at `pos`
    for (int i = 0; i < N; ++i)
    {
        const int idx = (pos + i) % N;
        bufL[(size_t) i] = { inL[(size_t) idx] * window[(size_t) i], 0.0f };
        bufR[(size_t) i] = { inR[(size_t) idx] * window[(size_t) i], 0.0f };
    }
    fft.perform (bufL.data(), false);
    fft.perform (bufR.data(), false);

    const float amount = param (pAmount) * 0.01f;
    const float fromHz = param (pFrom);
    const float protect = param (pProtect) * 0.01f;
    const bool repair = param (pMode) > 0.5f;
    const bool listen = param (pListen) > 0.5f;
    const float binHz = (float) sampleRateHz / (float) N;
    const int half = N / 2;
    const float maxSideRatio = 1.0f - 0.7f * protect;   // wider than this = deliberate stereo, left alone
    constexpr float panRate = 0.08f;    // ~12 frames (~130 ms): what counts as "steady"
    const float whistleAmt = param (pWhistle) * 0.01f;
    const float shield = param (pShield) * 0.01f;
    const float frameMs = 1000.0f * (float) hop / (float) sampleRateHz;

    // TRANSIENT SHIELD: a sudden jump of the high-frequency energy in the newest hop (snare, crash,
    // stick) holds the cleaning back for every frame that contains the attack (+ ~35 ms), so attacks
    // keep their snap. First differences emphasise the highs without a filter.
    {
        double hf = 0.0;
        for (int i = N - hop; i < N; ++i)
        {
            const int a = (pos + i) % N, b = (pos + i - 1 + N) % N;
            const float dl = inL[(size_t) a] - inL[(size_t) b], dr = inR[(size_t) a] - inR[(size_t) b];
            hf += (double) dl * dl + (double) dr * dr;
        }
        const float hfF = (float) hf;
        if (hfF > 4.0f * hfAvg + 1.0e-7f) shieldHold = N / hop + jmax (1, (int) std::ceil (35.0f / frameMs));
        else if (shieldHold > 0) --shieldHold;
        hfAvg += 0.1f * (hfF - hfAvg);
    }
    const float amountNow = shieldHold > 0 ? amount * (1.0f - shield) : amount;
    setReadout (shieldActive, shieldHold > 0 ? 1.0f : 0.0f);
    double cNum = 0.0, cL = 0.0, cR = 0.0;

    const bool feedUi = isAnalyzerActive();
    if (feedUi)
    {
        work.level.fill (0.0f); work.swirl.fill (0.0f); work.spread.fill (0.0f); work.pan.fill (0.0f);
    }
    double sideIn = 0.0, sideOut = 0.0, sideHi = 0.0, swirlHi = 0.0;

    // 1. per-bin artifact score -> target Side gain
    for (int k = 1; k < half; ++k)
    {
        const auto L = bufL[(size_t) k], R = bufR[(size_t) k];
        const auto M = 0.5f * (L + R), S = 0.5f * (L - R);
        const float pM = std::norm (M), pS = std::norm (S);
        const float f = (float) k * binHz;

        // Mid / Side coherence over ~130 ms: real panning and width move WITH the centre (coherence
        // near 1); codec swirl is Side that has nothing steady to do with the Mid (near 0)
        auto& sc = smCross[(size_t) k];
        sc += panRate * (S * std::conj (M) - sc);
        mPowAvg[(size_t) k] += panRate * (pM - mPowAvg[(size_t) k]);
        sPowAvg[(size_t) k] += panRate * (pS - sPowAvg[(size_t) k]);
        const float coherence = jlimit (0.0f, 1.0f, std::norm (sc) / (mPowAvg[(size_t) k] * sPowAvg[(size_t) k] + 1.0e-20f));

        // Side flicker against its 5-frame median
        auto& hist = sideHist[(size_t) k];
        const float sMag = std::sqrt (pS);
        hist[(size_t) histPos] = sMag;
        const float med = median5 (hist);
        const float flicker = jlimit (0.0f, 1.0f, sMag / (med + 1.0e-9f) - 1.0f);

        const float w = smoothstep (fromHz * 0.7f, fromHz, f);
        const bool audible = pM + pS > 1.0e-10f;
        const float sideRatio = pS / (pM + pS + kEps);
        const float sideWeight = 1.0f - smoothstep (maxSideRatio, jmin (1.0f, maxSideRatio + 0.15f), sideRatio);
        float score = audible ? (1.0f - coherence) * (0.75f + 0.25f * flicker) * w * sideWeight : 0.0f;
        score = jlimit (0.0f, 1.0f, (score - 0.1f) / 0.9f);
        gainTmp[(size_t) k] = jmax (kFloorGain, 1.0f - amountNow * score);
        sideSlow[(size_t) k] += 0.03f * (sMag - sideSlow[(size_t) k]);   // ~350 ms: steady whines stand out
        whistleTmp[(size_t) k] = 1.0f;
        if (f >= fromHz)
        {
            const auto cr = L * std::conj (R);
            cNum += cr.real(); cL += std::norm (L); cR += std::norm (R);
        }

        if (audible) { sideHi += (double) (pS * w); swirlHi += (double) (pS * w * score); }   // energy-weighted: noise-floor bins don't count
        if (feedUi && bandOf[(size_t) k] >= 0)
        {
            const auto b = (size_t) bandOf[(size_t) k];
            work.level[b] += pM + pS;
            work.swirl[b] += score;
            work.spread[b] += pS / (pM + pS + kEps);
            const float pl = std::norm (L), pr = std::norm (R);
            work.pan[b] += (pr - pl) / (pl + pr + kEps);   // -1 left .. +1 right
        }
    }
    histPos = (histPos + 1) % 5;

    // STEREO COHERENCE above FROM: +1 solid / mono, 0 swish, -1 out of phase
    if (cL > 1.0e-12 && cR > 1.0e-12)
        corrAvg += 0.15f * ((float) (cNum / std::sqrt (cL * cR)) - corrAvg);
    setReadout (coherence, corrAvg);

    // WHISTLE / BIRDIE HUNTER: a Side bin that sits >= 9 dB above its neighbours for a long time is
    // a steady whine (e.g. the 12.2 kHz Suno whistle): notch it in the Side only, -24 dB at 100 %
    float loudestWhistle = 0.0f, whistleFreq = 0.0f;
    if (whistleAmt > 0.0f)
    {
        const int k0 = jmax (8, (int) (jmin (fromHz, 6000.0f) / binHz));
        const float notch = 1.0f - whistleAmt * (1.0f - 0.063f);
        for (int k = k0; k < half - 8; ++k)
        {
            const float v = sideSlow[(size_t) k];
            if (v < 1.0e-5f || v < sideSlow[(size_t) (k - 1)] || v < sideSlow[(size_t) (k + 1)]) continue;
            float nb = 0.0f;
            for (int j = 3; j <= 7; ++j) nb += sideSlow[(size_t) (k - j)] + sideSlow[(size_t) (k + j)];
            nb *= 0.1f;
            if (v > 2.8f * (nb + 1.0e-9f))
            {
                for (int j = -1; j <= 1; ++j) whistleTmp[(size_t) (k + j)] = jmin (whistleTmp[(size_t) (k + j)], notch);
                if (v > loudestWhistle) { loudestWhistle = v; whistleFreq = (float) k * binHz; }
            }
        }
    }
    setReadout (whistleHz, whistleFreq);

    // 2. smooth across 3 bins, then in time (fast down, slower up), apply
    for (int k = 1; k < half; ++k)
    {
        const float target = (gainTmp[(size_t) (k - 1)] + 2.0f * gainTmp[(size_t) k] + gainTmp[(size_t) jmin (k + 1, half - 1)]) * 0.25f;
        auto& g = gain[(size_t) k];
        g += (shieldHold > 0 ? 0.75f : (target < g ? 0.6f : 0.15f)) * (target - g);   // on a hit, let go at once

        const auto L = bufL[(size_t) k], R = bufR[(size_t) k];
        const auto M = 0.5f * (L + R), S = 0.5f * (L - R);
        std::complex<float> S2;
        if (repair)
        {
            // keep the part of the Side that follows the Mid (stable panning / width), cut the erratic rest
            const auto coef = smCross[(size_t) k] / (mPowAvg[(size_t) k] + 1.0e-9f);
            auto steady = coef * M;
            if (std::norm (steady) > std::norm (S)) steady *= std::sqrt (std::norm (S) / (std::norm (steady) + kEps));
            S2 = steady + g * (S - steady);
        }
        else
            S2 = g * S;
        auto& wg = whistleGain[(size_t) k];
        wg += (whistleTmp[(size_t) k] < wg ? 0.5f : 0.1f) * (whistleTmp[(size_t) k] - wg);
        S2 *= wg;

        if ((float) k * binHz >= fromHz)   // SIDE CUT reports the band being worked on, not the untouched lows
        {
            sideIn += std::norm (S);
            sideOut += std::norm (S2);
        }
        auto newL = M + S2, newR = M - S2;
        if (listen) { newL = L - newL; newR = R - newR; }
        bufL[(size_t) k] = newL;
        bufR[(size_t) k] = newR;
        // mirror for the real inverse transform
        bufL[(size_t) (N - k)] = std::conj (newL);
        bufR[(size_t) (N - k)] = std::conj (newR);
    }
    if (listen) { bufL[0] = bufR[0] = {}; bufL[(size_t) half] = bufR[(size_t) half] = {}; }

    fft.perform (bufL.data(), true);
    fft.perform (bufR.data(), true);
    const float scale = 0.5f / (float) N;   // 1/N for the inverse, 1/2 for the 4x sqrt-Hann overlap
    for (int i = 0; i < N; ++i)
    {
        const int idx = (pos + i) % N;
        outL[(size_t) idx] += bufL[(size_t) i].real() * window[(size_t) i] * scale;
        outR[(size_t) idx] += bufR[(size_t) i].real() * window[(size_t) i] * scale;
    }

    setReadout (removedDb, sideIn > 1.0e-12 ? (float) (10.0 * std::log10 (jmax (1.0e-12, sideOut) / sideIn)) : 0.0f);
    setReadout (swirlPercent, sideHi > 1.0e-9 ? (float) (100.0 * swirlHi / sideHi) : 0.0f);

    if (feedUi)
    {
        for (int b = 0; b < kBands; ++b)
        {
            const float c = jmax (1.0f, bandCount[(size_t) b]);
            work.level[(size_t) b] = 10.0f * std::log10 (work.level[(size_t) b] / c * 4.0f / ((float) N * (float) N) + 1.0e-12f);
            work.swirl[(size_t) b] /= c;
            work.spread[(size_t) b] /= c;
            work.pan[(size_t) b] /= c;
        }
        int s1, n1, s2, n2;
        frameFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) frames[(size_t) s1] = work;
        frameFifo.finishedWrite (n1);   // FIFO full: the frame is dropped
    }
}

int EliteSwirl::readFrames (Frame* dest, int max) noexcept
{
    int s1, n1, s2, n2;
    frameFifo.prepareToRead (max, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) dest[i] = frames[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) dest[n1 + i] = frames[(size_t) (s2 + i)];
    frameFifo.finishedRead (n1 + n2);
    return n1 + n2;
}
} // namespace hem::pg
