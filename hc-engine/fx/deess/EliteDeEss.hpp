#pragma once
// EliteDeEss engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE DE-ESS - sibilance tamer: listens to the S / T / SH range and turns only that range down
    while it is too loud. Everything below FREQUENCY is untouched. LISTEN = hear what it catches. */
class EliteDeEss final : public Plugin
{
public:
    static const PluginInfo info;
    EliteDeEss();
    enum Readout { gr, sibilanceDb };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pFreq, *pThreshold, *pRange, *pListen, *pOutput;
    float lastFreq = -1.0f;
    BiquadCoeffs splitHp, splitLp, detBp;
    std::array<BiquadState, 2> hpL, hpR, lpL, lpR, det;
    float env = 0.0f, att = 0.0f, rel = 0.0f, grSmooth = 0.0f, grCoeff = 0.0f;
    SmoothedValue<float> output;
};
} // namespace hem::pg
