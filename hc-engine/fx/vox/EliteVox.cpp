#include "EliteVox.hpp"

namespace hem::pg
{
const PluginInfo EliteVox::info { "elite-vox", "Elite Vocalis", "VOCL", "One-move leveller: gate, squeeze, auto gain, built-in limiter", 0xffff8a2a };

namespace
{
    params::Layout voxLayout()
    {
        params::Layout l;
        params::addDb (l, "gate", "Gate", -80.0f, -10.0f, -80.0f);          // -80 = off
        params::addDb (l, "squeeze", "Squeeze", 0.0f, 36.0f, 0.0f);         // how far below 0 dBFS compression starts
        params::addDb (l, "output", "Output", -24.0f, 12.0f, 0.0f);
        params::addChoice (l, "mode", "Mode", EliteVox::modes(), 0);
        return l;
    }

    struct VoxTiming { float ratio, attack, release, gateHold, gateRelease; bool autoRelease; dsp::CompressorCore::Detector det; };
    VoxTiming timing (int mode)
    {
        switch (mode)
        {
            case 1:  return { 4.0f, 12.0f, 60.0f, 40.0f, 80.0f, false, dsp::CompressorCore::Detector::peak };   // Drums: let the hit through, fast recovery
            case 2:  return { 4.0f, 20.0f, 180.0f, 30.0f, 150.0f, true, dsp::CompressorCore::Detector::rms };   // Bass: steady
            case 3:  return { 2.0f, 25.0f, 250.0f, 10.0f, 200.0f, true, dsp::CompressorCore::Detector::rms };   // Bus: gentle glue
            default: return { 3.0f, 3.0f, 120.0f, 20.0f, 120.0f, true, dsp::CompressorCore::Detector::peak };  // Vocal
        }
    }
}

EliteVox::EliteVox() : Plugin (info, voxLayout())
{
    pGate = raw ("gate"); pSqueeze = raw ("squeeze"); pOutput = raw ("output"); pMode = raw ("mode");
}

void EliteVox::prepareDsp (double fs, int)
{
    gate.prepare (fs);
    comp.prepare (fs);
    limiter.prepare (fs, 50.0f);
    makeup.reset (fs, 0.05); output.reset (fs, 0.02);
    lastGate = lastSqueeze = 1.0e9f; lastMode = -1;
    updateSettings();
    makeup.setCurrentAndTargetValue (makeup.getTargetValue());
    output.setCurrentAndTargetValue (output.getTargetValue());
}

void EliteVox::resetDsp() noexcept
{
    gate.reset(); comp.reset(); limiter.reset();
}

void EliteVox::updateSettings() noexcept
{
    const float g = param (pGate), sq = param (pSqueeze);
    const int mode = jlimit (0, 3, (int) param (pMode));
    if (g != lastGate || sq != lastSqueeze || mode != lastMode)
    {
        const auto t = timing (mode);
        dsp::GateCore::Settings gs;
        gs.enabled = g > -79.95f;
        gs.thresholdDb = g; gs.ratio = 3.0f; gs.rangeDb = 40.0f;
        gs.attackMs = 0.5f; gs.holdMs = t.gateHold; gs.releaseMs = t.gateRelease;
        gate.setSettings (gs);

        dsp::CompressorCore::Settings cs;
        cs.thresholdDb = -sq; cs.ratio = t.ratio; cs.kneeDb = 8.0f;
        cs.attackMs = t.attack; cs.releaseMs = t.release; cs.autoRelease = t.autoRelease; cs.detector = t.det;
        comp.setSettings (cs);

        // Automatic makeup: half of the reduction a full-scale peak would get, so pushing SQUEEZE
        // makes it denser without simply getting quieter (the built-in limiter catches the rest).
        const float slope = 1.0f - 1.0f / t.ratio;
        makeup.setTargetValue (params::dbToGain (sq * slope * 0.5f));
        setReadout (autoGain, sq * slope * 0.5f);
        lastGate = g; lastSqueeze = sq; lastMode = mode;
    }
    output.setTargetValue (params::dbToGain (param (pOutput)));
}

void EliteVox::processDsp (float* left, float* right, int n) noexcept
{
    updateSettings();
    float maxGate = 0.0f, maxComp = 0.0f, minLim = 1.0f;
    constexpr float ceiling = 0.966f;   // -0.3 dBFS
    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];
        const float gGr = gate.process (l, r);
        const float gg = dsp::dbToLin (-gGr);
        l *= gg; r *= gg;
        const float cGr = comp.process (l, r);
        // OUTPUT goes in BEFORE the built-in limiter, so turning it up can never push past -0.3 dBFS
        const float cg = dsp::dbToLin (-cGr) * makeup.getNextValue() * output.getNextValue();
        l *= cg; r *= cg;
        limiter.process (l, r, ceiling);
        left[i] = l; right[i] = r;
        maxGate = jmax (maxGate, gGr); maxComp = jmax (maxComp, cGr); minLim = jmin (minLim, limiter.gain());
    }
    setReadout (gateGr, maxGate);
    setReadout (compGr, maxComp);
    setReadout (limitGr, -params::gainToDb (minLim));
}
} // namespace hem::pg
