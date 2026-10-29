#pragma once
// EliteBalance engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE BALANCE - automatic tone balance: listens to the whole mix for a few seconds, compares
    eight frequency regions to a smooth target tilt and gently corrects the ones that stick out or
    are missing. AMOUNT = how much of the correction to use, RANGE = the most any band may move. */
class EliteBalance final : public Plugin
{
public:
    static const PluginInfo info;
    static constexpr int kBands = 8;
    static float bandHz (int b) noexcept { return 62.5f * (float) (1 << b); }   // 62.5 Hz .. 8 kHz
    static StringArray speeds() { return { "Slow", "Medium", "Fast" }; }
    EliteBalance();
    /** readout b = correction in dB applied to band b (0..7) */

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pAmount, *pTilt, *pRange, *pSpeed, *pFreeze, *pOutput;
    std::array<BiquadCoeffs, kBands> bp, eq;
    std::array<BiquadState, kBands> bpSt;
    std::array<std::array<BiquadState, kBands>, 2> eqSt;
    std::array<float, kBands> power {}, applied {};
    float envCoeff = 0.9999f;
    int sinceUpdate = 0, updateEvery = 256;
    double warmSamples = 0.0;
    SmoothedValue<float> output;
};
} // namespace hem::pg
