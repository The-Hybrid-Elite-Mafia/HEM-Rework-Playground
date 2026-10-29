#pragma once
// EliteChannel engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "Dynamics.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE CHANNEL - gate + 4-band EQ + compressor channel strip, order switchable. */
class EliteChannel final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray orders() { return { "EQ > Dyn", "Dyn > EQ" }; }
    static StringArray compModes() { return { "Clean", "Vox" }; }
    static constexpr int kEqBands = 4;
    static String eqId (int band, const char* what) { return "eq" + std::to_string (band + 1) + "_" + what; }
    EliteChannel();
    enum Readout { gateGr, compGr };

    int bandCoeffs (int band, double sampleRate, BiquadCoeffs& out) const;

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateSettings() noexcept;
    std::atomic<float> *pGateOn, *pGateThr, *pGateFloor, *pGateRel;
    std::atomic<float> *pEqOn;
    std::array<std::array<std::atomic<float>*, 3>, kEqBands> pEq {};   // freq, gain, q
    std::atomic<float> *pCompOn, *pCompThr, *pCompRatio, *pCompAtk, *pCompRel, *pCompMode;
    std::atomic<float> *pOutput, *pPhase, *pOrder;
    dsp::GateCore gate;
    dsp::CompressorCore comp;
    std::array<BiquadCoeffs, kEqBands> eq {};
    std::array<std::array<BiquadState, 2>, kEqBands> eqSt {};
    std::array<float, 32> cache {};
    SmoothedValue<float> output, makeup;
};
} // namespace hem::pg
