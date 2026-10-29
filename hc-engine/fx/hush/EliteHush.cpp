#include "EliteHush.hpp"

namespace hem::pg
{
const PluginInfo EliteHush::info { "elite-hush", "Elite Hush", "HUSH",
                                   "Hiss and noise remover: learns the noise floor by itself, leaves the music on top alone", 0xff9fb6ff };

namespace
{
    params::Layout hushLayout()
    {
        params::Layout l;
        params::addPercent (l, "amount", "Amount", 0.0f);                     // how far above the floor still counts as noise
        params::addDb (l, "reduction", "Reduction", 3.0f, 30.0f, 12.0f);       // the most a frequency is turned down
        params::addHz (l, "from", "From", 20.0f, 8000.0f, 20.0f);              // only treat above this (20 = everything)
        params::addBool (l, "listen", "Listen (removed only)", false);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }
    constexpr float kEps = 1.0e-14f;
}

EliteHush::EliteHush() : Plugin (info, hushLayout())
{
    pAmount = raw ("amount"); pReduction = raw ("reduction"); pFrom = raw ("from"); pListen = raw ("listen"); pOutput = raw ("output");
}

void EliteHush::prepareDsp (double fs, int)
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
    power.assign (bins, 0.0f);
    smoothed.assign (bins, 0.0f);
    noise.assign (bins, 0.0f);
    gain.assign (bins, 1.0f);
    gainTmp.assign (bins, 1.0f);
    slotMin.assign (bins * (size_t) kSlots, 0.0f);
    curMin.assign (bins, 0.0f);
    framesPerSlot = jmax (1, (int) std::lround (0.5 * fs / (double) hop));
    output.reset (fs, 0.03);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteHush::resetDsp() noexcept
{
    for (auto* v : { &inL, &inR, &outL, &outR }) std::fill (v->begin(), v->end(), 0.0f);
    std::fill (power.begin(), power.end(), 0.0f);
    std::fill (smoothed.begin(), smoothed.end(), 0.0f);
    std::fill (noise.begin(), noise.end(), 0.0f);
    std::fill (slotMin.begin(), slotMin.end(), 0.0f);
    std::fill (curMin.begin(), curMin.end(), 0.0f);
    slotFrame = slot = slotsFilled = 0;
    std::fill (gain.begin(), gain.end(), 1.0f);
    pos = hopCount = 0;
    removedAvg = 0.0f;
}

void EliteHush::processDsp (float* left, float* right, int n) noexcept
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

void EliteHush::processFrame() noexcept
{
    const int N = fftSize;
    for (int i = 0; i < N; ++i)
    {
        const size_t src = (size_t) ((pos + i) % N);   // oldest sample first
        bufL[(size_t) i] = { inL[src] * window[(size_t) i], 0.0f };
        bufR[(size_t) i] = { inR[src] * window[(size_t) i], 0.0f };
    }
    fft.perform (bufL.data(), false);
    fft.perform (bufR.data(), false);

    const size_t bins = (size_t) N / 2 + 1;
    const float amount = param (pAmount) * 0.01f;
    const float floorGain = params::dbToGain (-param (pReduction));
    const bool listen = param (pListen) > 0.5f;
    const int fromBin = (int) std::floor (param (pFrom) * (float) N / (float) sampleRateHz);
    // everything up to (1 + 3 * amount) times the noise floor (up to +6 dB) is treated as noise
    const float over = 1.0f + 3.0f * amount;
    constexpr float kBias = 1.6f;   // the lowest level seen sits below the average noise level

    float removedSum = 0.0f, totalSum = 0.0f, noiseSum = 0.0f;
    for (size_t k = 0; k < bins; ++k)
    {
        const float p = 0.5f * (std::norm (bufL[k]) + std::norm (bufR[k]));
        power[k] = 0.6f * power[k] + 0.4f * p;   // a little smoothing so single frames do not flicker
    }
    // across frequency: the median of 13 neighbours, so a held note (a few bins) is never mistaken
    // for the floor - only the broad background between the notes is
    for (size_t k = 0; k < bins; ++k)
    {
        std::array<float, 13> v {};
        for (int j = -6; j <= 6; ++j)
            v[(size_t) (j + 6)] = power[(size_t) jlimit (0, (int) bins - 1, (int) k + j)];
        std::nth_element (v.begin(), v.begin() + 6, v.end());
        smoothed[k] = v[6];
    }
    // over time: the lowest smoothed level in the last ~4 s
    const bool slotStart = slotFrame == 0;
    for (size_t k = 0; k < bins; ++k)
    {
        curMin[k] = slotStart ? smoothed[k] : jmin (curMin[k], smoothed[k]);
        float m = curMin[k];
        for (int s = 0; s < jmin (slotsFilled, kSlots); ++s) m = jmin (m, slotMin[k * (size_t) kSlots + (size_t) s]);
        noise[k] = m * kBias;
    }
    if (++slotFrame >= framesPerSlot)
    {
        for (size_t k = 0; k < bins; ++k) slotMin[k * (size_t) kSlots + (size_t) slot] = curMin[k];
        slot = (slot + 1) % kSlots;
        slotsFilled = jmin (kSlots, slotsFilled + 1);
        slotFrame = 0;
    }
    for (size_t k = 0; k < bins; ++k)
    {
        float g = 1.0f;
        // AMOUNT at 0 = off (0.10.0 still took out what sat right on the floor at 0)
        if (amount > 0.0f && (int) k >= fromBin && power[k] > kEps)
        {
            // Wiener-style: keep the part of the power that is above the scaled noise floor
            const float keep = 1.0f - over * noise[k] / power[k];
            g = jlimit (floorGain, 1.0f, keep);
        }
        gainTmp[k] = g;
        noiseSum += noise[k] / kBias;
    }
    // smooth across neighbouring bins (stops "musical noise" twinkles) and over time (fast down, slower up)
    for (size_t k = 0; k < bins; ++k)
    {
        const float a = gainTmp[k > 0 ? k - 1 : k], b = gainTmp[k], c = gainTmp[k + 1 < bins ? k + 1 : k];
        const float target = 0.25f * a + 0.5f * b + 0.25f * c;
        gain[k] = target < gain[k] ? target : gain[k] + 0.35f * (target - gain[k]);
    }
    for (size_t k = 0; k < bins; ++k)
    {
        const float g = listen ? 1.0f - gain[k] : gain[k];
        removedSum += power[k] * (1.0f - gain[k] * gain[k]);
        totalSum += power[k];
        bufL[k] *= g; bufR[k] *= g;
        if (k > 0 && k < (size_t) N / 2)
        {
            bufL[(size_t) N - k] = std::conj (bufL[k]);
            bufR[(size_t) N - k] = std::conj (bufR[k]);
        }
    }
    fft.perform (bufL.data(), true);
    fft.perform (bufR.data(), true);
    const float scale = 1.0f / ((float) N * 2.0f);   // 1/N for the inverse FFT, /2 for the 4x sqrt-Hann overlap
    for (int i = 0; i < N; ++i)
    {
        const size_t dst = (size_t) ((pos + i) % N);
        outL[dst] += bufL[(size_t) i].real() * window[(size_t) i] * scale;
        outR[dst] += bufR[(size_t) i].real() * window[(size_t) i] * scale;
    }
    const float removed = totalSum > kEps ? -10.0f * std::log10 (jmax (1.0e-6f, 1.0f - removedSum / totalSum)) : 0.0f;
    removedAvg = 0.8f * removedAvg + 0.2f * removed;
    setReadout (removedDb, removedAvg);
    // noise floor shown in dBFS for a full-scale sine reading 0 dB (Hann window coherent gain: N/4 per side)
    const float norm = 4.0f / (float) N;
    setReadout (noiseDb, noiseSum > kEps ? 10.0f * std::log10 (noiseSum * norm * norm) : -120.0f);
}
} // namespace hem::pg
