#pragma once
// EliteVox engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "Dynamics.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE VOX - one-move vocal / drum leveller: gate, SQUEEZE compressor with automatic makeup,
    built-in output limiter, output level. Modes set the timing for vocals, drums, bass or a bus. */
class EliteVox final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray modes() { return { "Vocal", "Drums", "Bass", "Bus" }; }
    EliteVox();

    enum Readout { gateGr, compGr, autoGain, limitGr };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateSettings() noexcept;
    std::atomic<float> *pGate, *pSqueeze, *pOutput, *pMode;
    dsp::GateCore gate;
    dsp::CompressorCore comp;
    dsp::SafetyLimiter limiter;
    SmoothedValue<float> makeup, output;
    float lastGate = 1.0e9f, lastSqueeze = 1.0e9f; int lastMode = -1;
};
} // namespace hem::pg
