#pragma once
// EliteMultiband engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "hem/pg/Dynamics.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE MULTIBAND - three-band compressor for mastering and busses. Linkwitz-Riley 24 dB/oct
    crossovers (the bands add back up flat), one compressor per band, per-band gain. */
class EliteMultiband final : public Plugin
{
public:
    static const PluginInfo info;
    EliteMultiband();
    enum Readout { lowGr, midGr, highGr };
    static String id (int band, const char* what) { return std::string (band == 0 ? "low_" : band == 1 ? "mid_" : "high_") + what; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateFilters() noexcept;
    struct Band { std::atomic<float> *thr, *ratio, *gain; dsp::CompressorCore comp; SmoothedValue<float> g; };
    std::array<Band, 3> bands;
    std::atomic<float> *pLowX, *pHighX, *pAttack, *pRelease, *pMix, *pOutput;
    float lastLowX = -1.0f, lastHighX = -1.0f, lastAtk = -1.0f, lastRel = -1.0f;
    std::array<float, 3> lastThr { -999.0f, -999.0f, -999.0f }, lastRatio { 0.0f, 0.0f, 0.0f };
    BiquadCoeffs lp1, hp1, lp2, hp2;
    // [channel][stage]: low path (LP1 x2, then allpass at f2 = LP2 x2 + HP2 x2), rest path (HP1 x2), mid (LP2 x2), high (HP2 x2)
    std::array<std::array<BiquadState, 2>, 2> sLp1, sHp1, sLp2, sHp2, sApLo, sApHi;
    SmoothedValue<float> mix, output;
};
} // namespace hem::pg
