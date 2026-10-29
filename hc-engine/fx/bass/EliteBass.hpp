#pragma once
// EliteBass engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE BASS - psychoacoustic bass: adds the 2nd-4th harmonics of the bass below FREQ so the low
    end is heard on small speakers, and lets you turn the original sub down (ORIGINAL). */
class EliteBass final : public Plugin
{
public:
    static const PluginInfo info;
    EliteBass();
    enum Readout { originalLevel, harmonicsLevel };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateFilters (float freq) noexcept;
    std::atomic<float> *pFreq, *pIntensity, *pOriginal, *pOutput, *pSolo;
    BiquadCoeffs lp, hp, hHp, hLp;
    std::array<std::array<BiquadState, 2>, 2> lowSt {}, highSt {};   // channel x stage
    std::array<BiquadState, 2> hHpSt {}, hLpSt {};
    float env = 0.0f, envAtt = 0.0f, envRel = 0.0f, lastFreq = -1.0f;
    SmoothedValue<float> intensity, original, output;
};
} // namespace hem::pg
