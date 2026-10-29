#pragma once
// EliteTransient engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE TRANSIENT - punch and sustain shaper: turns the hit (attack) and the ring-out (sustain)
    up or down without touching the level in between. No look-ahead, no latency. */
class EliteTransient final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray speeds() { return { "Fast", "Medium", "Slow" }; }
    EliteTransient();
    enum Readout { gainNow };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pAttack, *pSustain, *pSpeed, *pOutput, *pMix;
    float envFast = 0.0f, envSlow = 0.0f, envHold = 0.0f;
    float aFast = 0.0f, rFast = 0.0f, aSlow = 0.0f, rSlow = 0.0f, rHold = 0.0f;
    int lastSpeed = -1;
    SmoothedValue<float> output, mix;
};
} // namespace hem::pg
