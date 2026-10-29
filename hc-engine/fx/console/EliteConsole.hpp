#pragma once
// EliteConsole engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE CONSOLE - mixing-desk glue: gentle channel saturation, transformer weight and air,
    a touch of left / right crosstalk. Output follows the input level (drive does not get louder). */
class EliteConsole final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray characters() { return { "Clean Desk", "Heavy Iron", "Hard Punch", "Tube Bus" }; }
    EliteConsole();
    enum Readout { driveDb };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pDrive, *pChar, *pCrosstalk, *pOutput;
    int lastChar = -1;
    BiquadCoeffs lowBump, highAir, xtalkLp, dcBlock;
    std::array<BiquadState, 2> lowSt, highSt, xtSt, dcSt;
    float bias = 0.0f;
    SmoothedValue<float> drive, crosstalk, output;
};
} // namespace hem::pg
