#include "EliteConsole.hpp"

namespace hem::pg
{
const PluginInfo EliteConsole::info { "elite-console", "Elite Console", "DESK",
                                      "Mixing-desk glue: channel drive, transformer weight and air, a little crosstalk", 0xffd9a35f };

namespace
{
    params::Layout consoleLayout()
    {
        params::Layout l;
        params::addDb (l, "drive", "Drive", 0.0f, 18.0f, 0.0f);
        params::addChoice (l, "character", "Character", EliteConsole::characters(), 0);
        params::addPercent (l, "crosstalk", "Crosstalk", 0.0f);
        params::addDb (l, "output", "Output", -12.0f, 12.0f, 0.0f);
        return l;
    }

    // per desk: saturation asymmetry (even harmonics), low shelf, high shelf
    struct Desk { float bias, lowHz, lowDb, highHz, highDb; };
    Desk deskFor (int c)
    {
        switch (c)
        {
            case 1:  return { 0.08f, 100.0f, 1.0f, 12000.0f, 1.5f };   // Heavy Iron: weight and air
            case 2:  return { 0.04f, 80.0f, 1.5f, 10000.0f, 0.5f };    // Hard Punch: low-end push
            case 3:  return { 0.18f, 120.0f, 1.0f, 8000.0f, -1.0f };   // Tube Bus: warm, rounder top
            default: return { 0.0f, 100.0f, 0.0f, 12000.0f, 0.0f };    // Clean Desk: drive only
        }
    }
    constexpr float kMaxCrosstalk = 0.1f;   // 100 % = the other side at -20 dB
}

EliteConsole::EliteConsole() : Plugin (info, consoleLayout())
{
    pDrive = raw ("drive"); pChar = raw ("character"); pCrosstalk = raw ("crosstalk"); pOutput = raw ("output");
}

void EliteConsole::prepareDsp (double fs, int)
{
    lastChar = -1;
    xtalkLp = BiquadCoeffs::lowPass1 (fs, 3000.0);
    dcBlock = BiquadCoeffs::highPass1 (fs, 8.0);
    for (auto* s : { &drive, &crosstalk, &output }) s->reset (fs, 0.03);
    drive.setCurrentAndTargetValue (params::dbToGain (param (pDrive)));
    crosstalk.setCurrentAndTargetValue (param (pCrosstalk) * 0.01f * kMaxCrosstalk);
    output.setCurrentAndTargetValue (params::dbToGain (param (pOutput)));
    resetDsp();
}

void EliteConsole::resetDsp() noexcept
{
    for (auto* a : { &lowSt, &highSt, &xtSt, &dcSt }) for (auto& s : *a) s.reset();
}

void EliteConsole::processDsp (float* left, float* right, int n) noexcept
{
    const int c = jlimit (0, 3, (int) param (pChar));
    if (c != lastChar)
    {
        const auto d = deskFor (c);
        lowBump = BiquadCoeffs::lowShelf (sampleRateHz, d.lowHz, d.lowDb);
        highAir = BiquadCoeffs::highShelf (sampleRateHz, d.highHz, d.highDb);
        bias = d.bias;
        lastChar = c;
    }
    drive.setTargetValue (params::dbToGain (param (pDrive)));
    crosstalk.setTargetValue (param (pCrosstalk) * 0.01f * kMaxCrosstalk);
    output.setTargetValue (params::dbToGain (param (pOutput)));

    const float offset = std::tanh (bias);
    const float slope = 1.0f - offset * offset;   // the curve's slope at rest: divided out so quiet parts stay at unity
    float peakIn = 0.0f, peakOut = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = drive.getNextValue(), xt = crosstalk.getNextValue(), o = output.getNextValue();
        const float inL = left[i], inR = right[i];
        // each channel bleeds a little dull copy of the other one into it
        const float l0 = inL + xt * xtSt[0].process (xtalkLp, inR);
        const float r0 = inR + xt * xtSt[1].process (xtalkLp, inL);
        // drive into a soft curve, then back down by the same amount: quiet parts keep their level,
        // loud peaks get rounded and pick up harmonics
        const float sl = (std::tanh (l0 * g + bias) - offset) / (g * slope);
        const float sr = (std::tanh (r0 * g + bias) - offset) / (g * slope);
        peakIn = jmax (peakIn, std::abs (l0), std::abs (r0));
        peakOut = jmax (peakOut, std::abs (sl), std::abs (sr));
        float l = dcSt[0].process (dcBlock, sl);
        float r = dcSt[1].process (dcBlock, sr);
        l = highSt[0].process (highAir, lowSt[0].process (lowBump, l)) * o;
        r = highSt[1].process (highAir, lowSt[1].process (lowBump, r)) * o;
        left[i] = l; right[i] = r;
    }
    // how much the peaks were rounded off (dB)
    setReadout (driveDb, peakIn > 1.0e-6f && peakOut > 1.0e-9f ? jmax (0.0f, dsp::linToDb (peakIn) - dsp::linToDb (peakOut)) : 0.0f);
}
} // namespace hem::pg
