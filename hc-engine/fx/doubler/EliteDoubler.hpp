#pragma once
// EliteDoubler engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE DOUBLER - turns one vocal (or guitar) into a wide double: two slightly late, slightly
    drifting copies panned left and right behind the original. */
class EliteDoubler final : public Plugin
{
public:
    static const PluginInfo info;
    EliteDoubler();
    enum Readout { spreadMs };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pAmount, *pWidth, *pDelay, *pDrift, *pTone, *pOutput;
    std::vector<float> line;
    int writePos = 0;
    double phaseA = 0.0, phaseB = 0.37, phaseC = 0.71;
    float lastTone = -1.0f;
    BiquadCoeffs toneHp;
    std::array<BiquadState, 2> toneSt;
    SmoothedValue<float> amount, width, delayMs, drift, output;
};
} // namespace hem::pg
