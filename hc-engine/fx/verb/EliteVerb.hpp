#pragma once
// EliteVerb engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE VERB - 8-line feedback-delay-network reverb (Room / Plate / Hall). */
class EliteVerb final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray types() { return { "Room", "Plate", "Hall" }; }
    EliteVerb();
    double getTailLengthSeconds() const override { return 12.0; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    static constexpr int kLines = 8, kDiffusers = 4;
    struct Line { std::vector<float> buf; int write = 0; float length = 1000.0f, target = 1000.0f; float lp = 0.0f; float gain = 0.5f; };
    struct Allpass { std::vector<float> buf; int pos = 0; int length = 100; };
    void updateTargets() noexcept;
    float readFrac (const Line&, float delay) const noexcept;

    std::atomic<float> *pPre, *pSize, *pDecay, *pDamp, *pLowCut, *pMix, *pWidth, *pType;
    std::array<Line, kLines> lines;
    std::array<Allpass, kDiffusers> diffusers;
    std::vector<float> pre; int preWrite = 0;
    BiquadCoeffs lowCut; std::array<BiquadState, 2> lowCutSt {};
    float dampCoeff = 0.3f, lfoPhase = 0.0f, lfoInc = 0.0f, diffusion = 0.7f;
    std::array<float, 8> cache {};
    SmoothedValue<float> mix, width;
};
} // namespace hem::pg
