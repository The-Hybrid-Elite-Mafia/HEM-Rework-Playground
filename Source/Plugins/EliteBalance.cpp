#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteBalance::info { "elite-balance", "Elite Balance", "BAL",
                                      "Automatic tone balance: finds the frequency areas that stick out or are missing and evens them out", 0xffb6ff5c };

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout balanceLayout()
    {
        params::Layout l;
        params::addPercent (l, "amount", "Amount", 50.0f);
        params::addFloat (l, "tilt", "Tilt", -6.0f, 3.0f, -1.5f, "dB/oct", 0.1f);   // 0 = flat like pink noise, negative = darker target
        params::addDb (l, "range", "Range", 1.0f, 12.0f, 6.0f);
        params::addChoice (l, "speed", "Speed", EliteBalance::speeds(), 1);
        params::addBool (l, "freeze", "Freeze", false);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }
    constexpr double kBandQ = 1.41;           // about one octave wide
    constexpr float kSilenceDb = -70.0f;      // below this the balance is left where it is
    constexpr float kFollow = 0.08f;          // how far each update moves toward the new correction
    double secondsFor (int speed) { return speed == 0 ? 3.0 : speed == 2 ? 0.3 : 1.0; }
}

EliteBalance::EliteBalance() : ElitePlugin (info, balanceLayout())
{
    pAmount = raw ("amount"); pTilt = raw ("tilt"); pRange = raw ("range"); pSpeed = raw ("speed");
    pFreeze = raw ("freeze"); pOutput = raw ("output");
}

void EliteBalance::prepareDsp (double fs, int)
{
    for (int b = 0; b < kBands; ++b)
    {
        bp[(size_t) b] = BiquadCoeffs::bandPass (fs, bandHz (b), kBandQ);
        eq[(size_t) b] = BiquadCoeffs::identity();
    }
    updateEvery = juce::jmax (64, (int) std::lround (256.0 * fs / 48000.0));
    output.reset (fs, 0.03);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteBalance::resetDsp() noexcept
{
    for (auto& s : bpSt) s.reset();
    for (auto& ch : eqSt) for (auto& s : ch) s.reset();
    power.fill (0.0f);
    applied.fill (0.0f);
    for (int b = 0; b < kBands; ++b) { eq[(size_t) b] = BiquadCoeffs::identity(); setReadout (b, 0.0f); }
    sinceUpdate = 0;
    warmSamples = 0.0;
}

void EliteBalance::processDsp (float* left, float* right, int n) noexcept
{
    const int speed = juce::jlimit (0, 2, (int) param (pSpeed));
    envCoeff = dsp::timeCoeff (secondsFor (speed) * 1000.0, sampleRateHz);
    const bool freeze = param (pFreeze) > 0.5f;
    output.setTargetValue (params::dbToGain (param (pOutput)));

    for (int i = 0; i < n; ++i)
    {
        const float l = left[i], r = right[i];
        if (! freeze)
        {
            const float m = 0.5f * (l + r);
            for (int b = 0; b < kBands; ++b)
            {
                const float y = bpSt[(size_t) b].process (bp[(size_t) b], m);
                power[(size_t) b] = y * y + envCoeff * (power[(size_t) b] - y * y);
            }
            warmSamples += 1.0;
        }
        if (++sinceUpdate >= updateEvery)
        {
            sinceUpdate = 0;
            if (! freeze && warmSamples > 0.5 * sampleRateHz)
            {
                std::array<float, kBands> dev {};
                float mean = 0.0f, loudest = -200.0f;
                const float tilt = param (pTilt);
                for (int b = 0; b < kBands; ++b)
                {
                    const float level = 10.0f * std::log10 (power[(size_t) b] + 1.0e-20f);
                    loudest = juce::jmax (loudest, level);
                    dev[(size_t) b] = level - tilt * (float) (b - 4);   // distance from the target line (anchored at 1 kHz)
                    mean += dev[(size_t) b];
                }
                mean /= (float) kBands;
                if (loudest > kSilenceDb)
                {
                    const float amount = param (pAmount) * 0.01f, range = param (pRange);
                    for (int b = 0; b < kBands; ++b)
                    {
                        const float want = juce::jlimit (-range, range, -(dev[(size_t) b] - mean) * amount);
                        applied[(size_t) b] += kFollow * (want - applied[(size_t) b]);
                    }
                }
            }
            for (int b = 0; b < kBands; ++b)
            {
                eq[(size_t) b] = BiquadCoeffs::peak (sampleRateHz, bandHz (b), kBandQ, applied[(size_t) b]);
                setReadout (b, applied[(size_t) b]);
            }
        }
        float ol = l, orr = r;
        for (int b = 0; b < kBands; ++b)
        {
            ol = eqSt[0][(size_t) b].process (eq[(size_t) b], ol);
            orr = eqSt[1][(size_t) b].process (eq[(size_t) b], orr);
        }
        const float o = output.getNextValue();
        left[i] = ol * o;
        right[i] = orr * o;
    }
}
} // namespace pg
