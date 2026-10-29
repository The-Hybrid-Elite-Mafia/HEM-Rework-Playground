#pragma once
// EliteComp engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "Dynamics.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE COMP - smooth bus / vocal compressor with auto release (ARC), three characters
    (Clean, Electro, Warm) and parallel MIX. */
class EliteComp final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray modes() { return { "Clean", "Electro", "Warm" }; }
    EliteComp();
    enum Readout { gr };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pThr, *pRatio, *pAtk, *pRel, *pArc, *pMode, *pGain, *pMix;
    dsp::CompressorCore comp;
    SmoothedValue<float> makeup, mix;
    std::array<float, 8> cache {};
};
} // namespace hem::pg
