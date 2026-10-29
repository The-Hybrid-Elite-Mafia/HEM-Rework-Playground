#pragma once
// EliteLofi engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE LO-FI - old-tape and old-sampler character: wow and flutter, bit depth and sample-rate
    crush, vinyl crackle and hiss, dark tone, dry / wet mix. */
class EliteLofi final : public Plugin
{
public:
    static const PluginInfo info;
    EliteLofi();

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pWow, *pFlutter, *pBits, *pRate, *pVinyl, *pTone, *pMix, *pOutput;
    std::vector<float> lineL, lineR;
    int writePos = 0;
    double wowPhase = 0.0, flutterPhase = 0.0;
    float holdL = 0.0f, holdR = 0.0f, holdCount = 0.0f;
    float lastTone = -1.0f;
    BiquadCoeffs toneLp, noiseHp;
    std::array<BiquadState, 2> toneSt;
    BiquadState noiseSt;
    uint32 rng = 0x48454D21u;
    float crackleEnv = 0.0f;
    SmoothedValue<float> mix, output;
};
} // namespace hem::pg
