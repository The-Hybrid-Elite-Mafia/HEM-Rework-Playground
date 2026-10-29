#pragma once
// EliteMaster engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "Loudness.hpp"
#include "LookaheadLimiter.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE MASTER - final look-ahead limiter with character, true-peak and LUFS metering and
    "match target" for streaming loudness. MASTER slots only (it has 1.5 ms of look-ahead). */
class EliteMaster final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray characters() { return { "Clean", "Punch", "Warm" }; }
    static StringArray targets() { return { "Streaming -14 LUFS", "Apple -16 LUFS", "Loud -9 LUFS", "Club -7 LUFS" }; }
    static float targetLufs (int index);
    EliteMaster();
    enum Readout { gr, momentary, shortTerm, integrated, truePeak, suggestedInput };

    /** Message thread: set INPUT so the integrated loudness lands on the chosen target. False if nothing measured yet. */
    bool matchTarget();
    /** Message thread: restart the loudness / true-peak measurement. */
    void resetMeters() noexcept { resetRequest.store (true); }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;
    int dspLatency() const noexcept override { return limiter.getLatencySamples(); }

private:
    std::atomic<float> *pInput, *pCeiling, *pRelease, *pChar, *pTarget;
    LookaheadLimiter limiter;
    dsp::LoudnessMeter loudness;
    dsp::TruePeakMeter tpMeter;
    BiquadCoeffs warmLow, warmHigh; std::array<std::array<BiquadState, 2>, 2> warmSt {};
    SmoothedValue<float> input;
    float lastCeil = 1.0e9f, lastRel = -1.0f;
    std::atomic<bool> resetRequest { false };
};
} // namespace hem::pg
