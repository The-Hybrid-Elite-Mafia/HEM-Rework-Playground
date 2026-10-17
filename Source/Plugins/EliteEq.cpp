#include "ElitePlugins.h"
#include <complex>

namespace pg
{
const PluginInfo EliteEq::info { "elite-eq", "Elite EQ", "EQ", "Six-band EQ with live analyzer", 0xff2ec8e6 };

namespace
{
    // band 1: Low Cut / Low Shelf / Bell, bands 2-5: Bell / Low Shelf / High Shelf / Notch, band 6: High Shelf / High Cut / Bell
    constexpr float kDefaultFreq[EliteEq::kBands] { 30.0f, 120.0f, 400.0f, 1500.0f, 4000.0f, 10000.0f };
    constexpr int kDefaultType[EliteEq::kBands] { 1, 0, 0, 0, 0, 0 };

    juce::AudioProcessorValueTreeState::ParameterLayout eqLayout()
    {
        params::Layout l;
        for (int b = 0; b < EliteEq::kBands; ++b)
        {
            const auto n = juce::String (b + 1);
            params::addBool (l, EliteEq::id (b, "on").toRawUTF8(), ("Band " + n + " On").toRawUTF8(), true);
            params::addChoice (l, EliteEq::id (b, "type").toRawUTF8(), ("Band " + n + " Type").toRawUTF8(), EliteEq::typesFor (b), kDefaultType[b]);
            params::addHz (l, EliteEq::id (b, "freq").toRawUTF8(), ("Band " + n + " Freq").toRawUTF8(), 20.0f, 20000.0f, kDefaultFreq[b]);
            params::addDb (l, EliteEq::id (b, "gain").toRawUTF8(), ("Band " + n + " Gain").toRawUTF8(), -18.0f, 18.0f, 0.0f);
            params::addFloat (l, EliteEq::id (b, "q").toRawUTF8(), ("Band " + n + " Q").toRawUTF8(), 0.1f, 10.0f, 0.8f, "", 0.01f, 1.0f);
        }
        params::addDb (l, "output", "Output", -18.0f, 18.0f, 0.0f);
        return l;
    }

    enum class Kind { off, lowCut, highCut, lowShelf, highShelf, bell, notch };

    Kind kindOf (int band, int type)
    {
        if (band == 0) return type == 0 ? Kind::lowCut : type == 1 ? Kind::lowShelf : Kind::bell;
        if (band == EliteEq::kBands - 1) return type == 0 ? Kind::highShelf : type == 1 ? Kind::highCut : Kind::bell;
        return type == 0 ? Kind::bell : type == 1 ? Kind::lowShelf : type == 2 ? Kind::highShelf : Kind::notch;
    }

    int design (Kind k, double fs, double f, double gainDb, double q, BiquadCoeffs out[2])
    {
        switch (k)
        {
            case Kind::lowCut:  out[0] = BiquadCoeffs::highPass (fs, f, 0.5411961); out[1] = BiquadCoeffs::highPass (fs, f, 1.3065630); return 2;
            case Kind::highCut: out[0] = BiquadCoeffs::lowPass (fs, f, 0.5411961);  out[1] = BiquadCoeffs::lowPass (fs, f, 1.3065630);  return 2;
            case Kind::lowShelf:  if (std::abs (gainDb) < 0.01) return 0; out[0] = BiquadCoeffs::lowShelfQ (fs, f, gainDb, juce::jlimit (0.3, 2.0, q)); return 1;
            case Kind::highShelf: if (std::abs (gainDb) < 0.01) return 0; out[0] = BiquadCoeffs::highShelfQ (fs, f, gainDb, juce::jlimit (0.3, 2.0, q)); return 1;
            case Kind::bell:      if (std::abs (gainDb) < 0.01) return 0; out[0] = BiquadCoeffs::peak (fs, f, q, gainDb); return 1;
            case Kind::notch:     out[0] = BiquadCoeffs::peak (fs, f, juce::jmax (2.0, q * 4.0), -60.0); return 1;
            case Kind::off:       break;
        }
        return 0;
    }
}

juce::StringArray EliteEq::typesFor (int band)
{
    if (band == 0) return { "Low Cut", "Low Shelf", "Bell" };
    if (band == kBands - 1) return { "High Shelf", "High Cut", "Bell" };
    return { "Bell", "Low Shelf", "High Shelf", "Notch" };
}

float EliteEq::defaultFreq (int band) { return kDefaultFreq[juce::jlimit (0, kBands - 1, band)]; }

EliteEq::EliteEq() : ElitePlugin (info, eqLayout())
{
    for (int b = 0; b < kBands; ++b)
        bp[(size_t) b] = { raw (id (b, "on")), raw (id (b, "type")), raw (id (b, "freq")), raw (id (b, "gain")), raw (id (b, "q")) };
    pOutput = raw ("output");
    // The first band's low cut is off by default (on = true but type Low Shelf at 0 dB is flat)
}

int EliteEq::bandCoeffs (int band, double fs, BiquadCoeffs out[2]) const
{
    const auto& p = bp[(size_t) band];
    if (p.on->load() < 0.5f) return 0;
    return design (kindOf (band, (int) p.type->load()), fs, p.freq->load(), p.gain->load(), p.q->load(), out);
}

float EliteEq::responseDb (double fs, double hz) const
{
    const double w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (1.0, fs * 0.499, hz) / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    double db = 0.0;
    for (int b = 0; b < kBands; ++b)
    {
        BiquadCoeffs c[2];
        const int n = bandCoeffs (b, fs, c);
        for (int s = 0; s < n; ++s)
            db += 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs ((c[s].b0 + c[s].b1 * z1 + c[s].b2 * z2) / (1.0 + c[s].a1 * z1 + c[s].a2 * z2))));
    }
    return (float) db + pOutput->load();
}

void EliteEq::prepareDsp (double, int)
{
    for (auto& c : cache) c.fill (-1.0e9f);
    output.reset (sampleRateHz, 0.02);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
}

void EliteEq::resetDsp() noexcept
{
    for (auto& band : st) for (auto& ch : band) for (auto& s : ch) s.reset();
}

void EliteEq::processDsp (float* left, float* right, int n) noexcept
{
    for (int b = 0; b < kBands; ++b)
    {
        const auto& p = bp[(size_t) b];
        const std::array<float, 5> now { param (p.on), param (p.type), param (p.freq), param (p.gain), param (p.q) };
        if (now != cache[(size_t) b])
        {
            BiquadCoeffs c[2];
            const int newStages = now[0] < 0.5f ? 0 : design (kindOf (b, (int) now[1]), sampleRateHz, now[2], now[3], now[4], c);
            if (newStages != stages[(size_t) b])
                for (auto& ch : st[(size_t) b]) for (auto& s : ch) s.reset();
            coeffs[(size_t) b] = { c[0], c[1] };
            stages[(size_t) b] = newStages;
            cache[(size_t) b] = now;
        }
    }
    output.setTargetValue (params::dbToGain (param (pOutput)));

    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];
        for (int b = 0; b < kBands; ++b)
            for (int s = 0; s < stages[(size_t) b]; ++s)
            {
                l = st[(size_t) b][0][(size_t) s].process (coeffs[(size_t) b][(size_t) s], l);
                r = st[(size_t) b][1][(size_t) s].process (coeffs[(size_t) b][(size_t) s], r);
            }
        const float g = output.getNextValue();
        left[i] = l * g; right[i] = r * g;
    }
}
} // namespace pg
