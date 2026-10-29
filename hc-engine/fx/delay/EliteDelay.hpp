#pragma once
// EliteDelay engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE DELAY - stereo / ping-pong delay with tempo sync, filtered and saturated feedback and ducking. */
class EliteDelay final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray divisions() { return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" }; }
    static double divisionBeats (int index);
    EliteDelay();
    double getTailLengthSeconds() const override { return 8.0; }
    enum Readout { delayMs };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pTime, *pSync, *pDiv, *pFeedback, *pLowCut, *pHighCut, *pPing, *pMix, *pDuck;
    std::vector<float> bufL, bufR;
    int write = 0;
    float current = 0.0f, duckEnv = 0.0f, duckRel = 0.0f;
    BiquadCoeffs hp, lp; std::array<BiquadState, 2> hpSt {}, lpSt {};
    float lastLow = -1.0f, lastHigh = -1.0f;
    SmoothedValue<float> feedback, mix, ping;
};
} // namespace hem::pg
