#pragma once
// EliteEq engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE EQ - six-band paragraphic EQ (cuts are 24 dB/oct) with a live spectrum analyzer. */
class EliteEq final : public Plugin
{
public:
    static const PluginInfo info;
    static constexpr int kBands = 6;
    static StringArray typesFor (int band);
    /** Default type/frequency for a band. */
    static float defaultFreq (int band);
    EliteEq();

    /** Message thread (UI curve): the 1-2 biquads of a band for the current parameters. Returns stages (0 = band off / flat). */
    int bandCoeffs (int band, double sampleRate, BiquadCoeffs out[2]) const;
    /** Message thread (UI curve): what the EQ does at `hz` in dB for the current settings, all bands + OUTPUT. */
    float responseDb (double sampleRate, double hz) const;
    static String id (int band, const char* what) { return "b" + std::to_string (band + 1) + "_" + what; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    struct BandParams { std::atomic<float> *on, *type, *freq, *gain, *q; };
    std::array<BandParams, kBands> bp {};
    std::atomic<float>* pOutput = nullptr;
    std::array<std::array<BiquadCoeffs, 2>, kBands> coeffs {};
    std::array<int, kBands> stages {};
    std::array<std::array<std::array<BiquadState, 2>, 2>, kBands> st {};   // band x channel x stage
    std::array<std::array<float, 5>, kBands> cache {};
    SmoothedValue<float> output;
};
} // namespace hem::pg
