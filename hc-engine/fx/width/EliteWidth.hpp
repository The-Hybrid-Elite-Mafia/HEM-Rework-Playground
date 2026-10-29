#pragma once
// EliteWidth engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE WIDTH - stereo width with mono bass: makes the sides wider or narrower above a
    frequency and folds everything below it to mono (tight low end, safe on club systems). */
class EliteWidth final : public Plugin
{
public:
    static const PluginInfo info;
    EliteWidth();
    enum Readout { correlation, sideLevel };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pWidth, *pMonoBelow, *pOutput;
    float lastMono = -1.0f;
    BiquadCoeffs hp;
    std::array<BiquadState, 2> sideHp;
    SmoothedValue<float> width, output;
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0, sideAvg = 0.0, totalAvg = 0.0;
    float corrCoeff = 0.999f;
};
} // namespace hem::pg
