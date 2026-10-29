#include "Waterfall.hpp"
#include "hem/pg/Core.hpp"
#include <cmath>

namespace hem::pg::analysis
{
float Waterfall::pointHz (int i) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) i / (float) (kPoints - 1));   // 20 Hz .. 20 kHz
}

void Waterfall::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    window.resize ((size_t) kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * (float) i / (float) kFftSize);
    ring.assign ((size_t) kFftSize, 0.0f);
    work.assign ((size_t) kFftSize * 2, 0.0f);
    spectrum.assign ((size_t) kFftSize, {});
    fft.prepare (kFftSize);
    rows.assign ((size_t) kRows * kPoints, kFloorDb);
    pending.assign ((size_t) kPoints, kFloorDb);

    // each point covers the bins between its neighbours' geometric midpoints; low points that
    // fall between two bins are interpolated instead
    binLo.assign ((size_t) kPoints, 0);
    binHi.assign ((size_t) kPoints, 0);
    binPos.assign ((size_t) kPoints, 0.0f);
    const float binHz = (float) fs / (float) kFftSize;
    const int maxBin = kFftSize / 2 - 1;
    for (int i = 0; i < kPoints; ++i)
    {
        const float f = pointHz (i);
        const float lo = i == 0 ? f : std::sqrt (f * pointHz (i - 1));
        const float hi = i == kPoints - 1 ? f : std::sqrt (f * pointHz (i + 1));
        binPos[(size_t) i] = jlimit (1.0f, (float) maxBin, f / binHz);
        binLo[(size_t) i] = jlimit (1, maxBin, (int) std::ceil (lo / binHz));
        binHi[(size_t) i] = jlimit (1, maxBin, (int) std::floor (hi / binHz));
    }
    reset();
}

void Waterfall::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (rows.begin(), rows.end(), kFloorDb);
    std::fill (pending.begin(), pending.end(), kFloorDb);
    ringPos = sinceHop = newest = halfCount = 0;
    made = 0;
}

void Waterfall::add (const float* mono, int n)
{
    if (ring.empty()) prepare (fs);
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) ringPos] = mono[i];
        ringPos = (ringPos + 1) % kFftSize;
        if (++sinceHop >= kHop) { sinceHop = 0; runFft(); }
    }
}

void Waterfall::runFft()
{
    for (int i = 0; i < kFftSize; ++i)
        work[(size_t) i] = ring[(size_t) ((ringPos + i) % kFftSize)] * window[(size_t) i];
    std::fill (work.begin() + kFftSize, work.end(), 0.0f);
    for (int i = 0; i < kFftSize; ++i) spectrum[(size_t) i] = { work[(size_t) i], 0.0f };
    fft.perform (spectrum.data(), false);
    for (int i = 0; i <= kFftSize / 2; ++i) work[(size_t) i] = std::abs (spectrum[(size_t) i]);

    // a full-scale sine through a Hann window peaks at N/4 in its bin
    const float norm = 4.0f / (float) kFftSize;
    auto toDb = [norm] (float mag) { return jmax (kFloorDb, 20.0f * std::log10 (jmax (1.0e-9f, mag * norm))); };
    for (int p = 0; p < kPoints; ++p)
    {
        float mag;
        if (binHi[(size_t) p] >= binLo[(size_t) p])
        {
            mag = 0.0f;
            for (int b = binLo[(size_t) p]; b <= binHi[(size_t) p]; ++b) mag = jmax (mag, work[(size_t) b]);
        }
        else
        {
            const float x = binPos[(size_t) p];
            const int b = (int) x;
            const float t = x - (float) b;
            mag = work[(size_t) b] + t * (work[(size_t) b + 1] - work[(size_t) b]);
        }
        pending[(size_t) p] = jmax (halfCount == 0 ? kFloorDb : pending[(size_t) p], toDb (mag));
    }
    if (++halfCount < 2) return;
    halfCount = 0;
    newest = (newest + 1) % kRows;
    std::copy (pending.begin(), pending.end(), rows.begin() + (size_t) newest * kPoints);
    std::fill (pending.begin(), pending.end(), kFloorDb);
    ++made;
}
} // namespace hem::pg::analysis
