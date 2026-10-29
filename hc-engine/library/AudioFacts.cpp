#include "AudioFacts.hpp"
#include "hem/pg/Fft.hpp"
#include "Loudness.hpp"
#include <array>
#include <cmath>
#include <complex>

namespace hem::pg::library
{
namespace
{
constexpr int kFft = 2048;
constexpr const char* kNames[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

// Krumhansl-Kessler key profiles
constexpr std::array<double, 12> kMajor { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr std::array<double, 12> kMinor { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

double correlation (const std::array<double, 12>& a, const std::array<double, 12>& b, int shift)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[(size_t) i]; mb += b[(size_t) i]; }
    ma /= 12.0; mb /= 12.0;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double x = a[(size_t) ((i + shift) % 12)] - ma, y = b[(size_t) i] - mb;
        num += x * y; da += x * x; db += y * y;
    }
    return num / std::sqrt (da * db + 1e-30);
}

/** gain -> dB, -100 for silence (as 0.10.0). */
float gainToDb100 (float g) { return g > 0.0f ? jmax (-100.0f, std::log10 (g) * 20.0f) : -100.0f; }

float interp (const std::vector<float>& v, double x)
{
    const int i = (int) x;
    if (i < 0 || i + 1 >= (int) v.size()) return 0.0f;
    const float t = (float) (x - i);
    return v[(size_t) i] + t * (v[(size_t) i + 1] - v[(size_t) i]);
}
} // namespace

std::string AudioFacts::keyName() const
{
    if (keyRoot < 0) return {};
    return std::string (kNames[keyRoot % 12]) + (minor ? " minor" : " major");
}

AudioFacts measure (const AudioBuffer& audio, double sr, const std::atomic<bool>* cancel)
{
    AudioFacts f;
    const int n = audio.getNumSamples();
    const int chans = audio.getNumChannels();
    if (n == 0 || chans == 0 || sr <= 0.0) return f;
    f.seconds = n / sr;
    f.peakDb = gainToDb100 (audio.getMagnitude (0, n));
    {
        dsp::LoudnessMeter lm;
        lm.prepare (sr);
        lm.process (audio.getReadPointer (0), audio.getReadPointer (chans > 1 ? 1 : 0), n);
        f.lufs = lm.getIntegrated();
    }
    auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };

    // mono
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < chans; ++c) s += audio.getSample (c, i);
        mono[(size_t) i] = s / (float) chans;
    }

    // STFT: spectral flux (onsets); chroma comes from its own long-window pass below
    const int hop = jmax (64, (int) std::lround (sr * 512.0 / 48000.0));
    const double frameRate = sr / hop;
    dsp::Fft fft;
    fft.prepare (kFft);
    std::vector<float> window ((size_t) kFft);
    for (int i = 0; i < kFft; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * i / kFft);
    const int bins = kFft / 2;
    const int fluxTop = jmin (bins - 1, (int) (10000.0 * kFft / sr));
    std::vector<std::complex<float>> buf ((size_t) kFft);
    std::vector<float> prev ((size_t) bins, 0.0f), cur ((size_t) bins);
    std::vector<float> flux;
    const int frames = n > kFft ? 1 + (n - kFft) / hop : 0;
    flux.reserve ((size_t) frames);
    for (int fr = 0; fr < frames; ++fr)
    {
        if ((fr & 255) == 0 && cancelled()) return f;
        const int st = fr * hop;
        for (int i = 0; i < kFft; ++i) buf[(size_t) i] = { mono[(size_t) (st + i)] * window[(size_t) i], 0.0f };
        fft.perform (buf.data(), false);
        float fl = 0.0f;
        for (int k = 1; k <= fluxTop; ++k)
        {
            cur[(size_t) k] = std::log1p (1000.0f * std::abs (buf[(size_t) k]));
            fl += jmax (0.0f, cur[(size_t) k] - prev[(size_t) k]);
        }
        std::swap (prev, cur);
        flux.push_back (fr == 0 ? 0.0f : fl);
    }

    // chroma: 16384-point frames (about 3 Hz bins), spectral peaks only between 65 Hz and 1.1 kHz,
    // each peak shared between its two nearest pitch classes. Peaks keep drums and noise out, the
    // long window resolves the low notes, and each frame is normalised so loud parts don't dominate.
    std::array<double, 12> chroma {};
    {
        constexpr int kLong = 16384;
        dsp::Fft big;
        big.prepare (kLong);
        std::vector<float> win ((size_t) kLong);
        for (int i = 0; i < kLong; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * i / kLong);
        std::vector<std::complex<float>> b2 ((size_t) kLong);
        std::vector<float> mag ((size_t) kLong / 2);
        const int kLo = jmax (2, (int) (65.0 * kLong / sr)), kHi = jmin (kLong / 2 - 2, (int) (1100.0 * kLong / sr));
        for (int st = 0; st + kLong <= n; st += kLong / 2)
        {
            if (cancelled()) return f;
            for (int i = 0; i < kLong; ++i) b2[(size_t) i] = { mono[(size_t) (st + i)] * win[(size_t) i], 0.0f };
            big.perform (b2.data(), false);
            float top = 0.0f;
            for (int k = kLo - 1; k <= kHi + 1; ++k) { mag[(size_t) k] = std::abs (b2[(size_t) k]); top = jmax (top, mag[(size_t) k]); }
            if (top < 1.0e-4f) continue;
            std::array<double, 12> fc {};
            double e = 0.0;
            for (int k = kLo; k <= kHi; ++k)
            {
                const float m = mag[(size_t) k];
                if (m < top * 0.01f || m <= mag[(size_t) k - 1] || m < mag[(size_t) k + 1]) continue;
                // parabolic peak position
                const float a = mag[(size_t) k - 1], c = mag[(size_t) k + 1];
                const float den = a - 2.0f * m + c;
                const double kk = k + (std::abs (den) > 1e-12f ? 0.5 * (a - c) / den : 0.0);
                const double pitch = 69.0 + 12.0 * std::log2 (kk * sr / kLong / 440.0);
                const int lo = (int) std::floor (pitch);
                const double frac = pitch - lo;
                // low notes (bass, chord roots) count double: they carry the key, while the upper
                // partials of a bright note would otherwise push the key a fifth up
                const double w = std::sqrt ((double) m) * (pitch < 55.0 ? 2.0 : 1.0);
                fc[(size_t) (((lo % 12) + 12) % 12)] += w * (1.0 - frac);
                fc[(size_t) ((((lo + 1) % 12) + 12) % 12)] += w * frac;
                e += w;
            }
            if (e > 0.0)
                for (int p = 0; p < 12; ++p) chroma[(size_t) p] += fc[(size_t) p] / e;
        }
    }

    // key
    {
        double best = -2.0;
        for (int r = 0; r < 12; ++r)
            for (int m = 0; m < 2; ++m)
            {
                const double c = correlation (chroma, m == 0 ? kMajor : kMinor, r);
                if (c > best) { best = c; f.keyRoot = r; f.minor = m == 1; }
            }
        f.keyConfidence = (float) jmax (0.0, best);
        if (best < 0.3) f.keyRoot = -1;
    }

    // onset envelope: flux above its local mean (half a second)
    const int nf = (int) flux.size();
    if (nf < (int) (frameRate * 8.0)) return f;   // too short for a tempo
    std::vector<float> onset ((size_t) nf);
    {
        const int w = (int) (frameRate * 0.25);
        double run = 0.0;
        int lo = 0, hi = -1;
        for (int i = 0; i < nf; ++i)
        {
            while (hi < jmin (nf - 1, i + w)) run += flux[(size_t) ++hi];
            while (lo < i - w) run -= flux[(size_t) lo++];
            const float mean = (float) (run / (hi - lo + 1));
            onset[(size_t) i] = jmax (0.0f, flux[(size_t) i] - mean);
        }
    }

    // autocorrelation with a tempo prior (log-gaussian around 110 BPM), plus half the score of the
    // double period so a beat whose 8th notes are loud still reads as the beat
    const int lagMin = (int) std::floor (frameRate * 60.0 / 200.0), lagMax = (int) std::ceil (frameRate * 60.0 / 40.0);
    std::vector<double> ac ((size_t) lagMax * 2 + 2, 0.0);
    for (int lag = lagMin; lag <= 2 * lagMax + 1 && lag < nf; ++lag)
    {
        if ((lag & 31) == 0 && cancelled()) return f;
        double s = 0.0;
        for (int i = 0; i + lag < nf; ++i) s += (double) onset[(size_t) i] * onset[(size_t) (i + lag)];
        ac[(size_t) lag] = s / (nf - lag);
    }
    auto weighted = [&] (int lag)
    {
        if (lag <= 0 || lag >= (int) ac.size()) return 0.0;
        const double bpm = 60.0 * frameRate / lag;
        const double o = std::log2 (bpm / 110.0) / 1.0;
        return ac[(size_t) lag] * std::exp (-0.5 * o * o);
    };
    int bestLag = 0;
    double bestScore = 0.0;
    const int searchLo = (int) std::floor (frameRate * 60.0 / 185.0), searchHi = (int) std::ceil (frameRate * 60.0 / 58.0);
    for (int lag = searchLo; lag <= searchHi; ++lag)
    {
        const double s = weighted (lag) + 0.5 * weighted (2 * lag);
        if (s > bestScore) { bestScore = s; bestLag = lag; }
    }
    if (bestLag == 0) return f;

    // beat comb: fine tempo (+-3 %) and phase over the whole song
    const double coarse = 60.0 * frameRate / bestLag;
    double bestBpm = coarse, bestComb = -1.0;
    double mean = 0.0;
    for (float v : onset) mean += v;
    mean /= nf;
    for (double bpm = coarse * 0.97; bpm <= coarse * 1.03; bpm += 0.02)
    {
        if (cancelled()) return f;
        const double period = 60.0 * frameRate / bpm;
        for (double ph = 0.0; ph < period; ph += 0.5)
        {
            double s = 0.0;
            int k = 0;
            for (double t = ph; t < nf - 1; t += period, ++k) s += interp (onset, t);
            s /= jmax (1, k);
            if (s > bestComb) { bestComb = s; bestBpm = bpm; }
        }
    }
    // Suno and Band Forge tempos are whole numbers: snap when the whole number fits as well
    {
        const double whole = std::round (bestBpm);
        if (std::abs (whole - bestBpm) < 0.25) bestBpm = whole;
    }
    f.bpm = std::round (bestBpm * 10.0) / 10.0;
    f.tempoConfidence = (float) jlimit (0.0, 1.0, mean > 0.0 ? (bestComb / mean - 1.0) / 3.0 : 0.0);
    return f;
}
} // namespace hem::pg::library
