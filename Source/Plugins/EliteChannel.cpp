#include "ElitePlugins.h"

namespace pg
{
const PluginInfo EliteChannel::info { "elite-channel", "Elite Channel", "CHAN", "Gate + 4-band EQ + compressor in one strip", 0xff3aa7d8 };

namespace
{
    struct BandDef { const char* name; float fMin, fMax, fDef, qDef; };
    constexpr BandDef kBands[EliteChannel::kEqBands] {
        { "Low",      20.0f,   500.0f,    80.0f, 0.7f },   // low shelf
        { "Low Mid",  50.0f,  5000.0f,   300.0f, 1.0f },   // bell
        { "High Mid", 500.0f, 15000.0f, 2500.0f, 1.0f },   // bell
        { "High",    2000.0f, 20000.0f, 10000.0f, 0.7f } }; // high shelf

    juce::AudioProcessorValueTreeState::ParameterLayout channelLayout()
    {
        params::Layout l;
        params::addBool (l, "gate_on", "Gate On", false);
        params::addDb (l, "gate_thr", "Gate Threshold", -80.0f, 0.0f, -50.0f);
        params::addDb (l, "gate_floor", "Gate Floor", 0.0f, 80.0f, 40.0f);
        params::addMs (l, "gate_rel", "Gate Release", 5.0f, 2000.0f, 100.0f);
        params::addBool (l, "eq_on", "EQ On", true);
        for (int b = 0; b < EliteChannel::kEqBands; ++b)
        {
            const auto& d = kBands[b];
            params::addHz (l, EliteChannel::eqId (b, "freq").toRawUTF8(), (juce::String (d.name) + " Freq").toRawUTF8(), d.fMin, d.fMax, d.fDef);
            params::addDb (l, EliteChannel::eqId (b, "gain").toRawUTF8(), (juce::String (d.name) + " Gain").toRawUTF8(), -18.0f, 18.0f, 0.0f);
            params::addFloat (l, EliteChannel::eqId (b, "q").toRawUTF8(), (juce::String (d.name) + " Q").toRawUTF8(), 0.1f, 10.0f, d.qDef, "", 0.01f, 1.0f);
        }
        params::addBool (l, "comp_on", "Comp On", true);
        params::addDb (l, "comp_thr", "Comp Threshold", -60.0f, 0.0f, -20.0f);
        params::addFloat (l, "comp_ratio", "Comp Ratio", 1.0f, 20.0f, 3.0f, ":1", 0.01f, 4.0f);
        params::addMs (l, "comp_atk", "Comp Attack", 0.1f, 100.0f, 10.0f);
        params::addMs (l, "comp_rel", "Comp Release", 10.0f, 2000.0f, 150.0f);
        params::addChoice (l, "comp_mode", "Comp Mode", EliteChannel::compModes(), 0);
        params::addDb (l, "output", "Output", -24.0f, 24.0f, 0.0f);
        params::addBool (l, "phase", "Phase Invert", false);
        params::addChoice (l, "order", "Order", EliteChannel::orders(), 0);
        return l;
    }

    BiquadCoeffs designBand (int band, double fs, float f, float g, float q)
    {
        if (std::abs (g) < 0.01f) return {};
        if (band == 0) return BiquadCoeffs::lowShelfQ (fs, f, g, juce::jlimit (0.3f, 2.0f, q));
        if (band == EliteChannel::kEqBands - 1) return BiquadCoeffs::highShelfQ (fs, f, g, juce::jlimit (0.3f, 2.0f, q));
        return BiquadCoeffs::peak (fs, f, q, g);
    }
}

EliteChannel::EliteChannel() : ElitePlugin (info, channelLayout())
{
    pGateOn = raw ("gate_on"); pGateThr = raw ("gate_thr"); pGateFloor = raw ("gate_floor"); pGateRel = raw ("gate_rel");
    pEqOn = raw ("eq_on");
    for (int b = 0; b < kEqBands; ++b)
        pEq[(size_t) b] = { raw (eqId (b, "freq")), raw (eqId (b, "gain")), raw (eqId (b, "q")) };
    pCompOn = raw ("comp_on"); pCompThr = raw ("comp_thr"); pCompRatio = raw ("comp_ratio");
    pCompAtk = raw ("comp_atk"); pCompRel = raw ("comp_rel"); pCompMode = raw ("comp_mode");
    pOutput = raw ("output"); pPhase = raw ("phase"); pOrder = raw ("order");
}

int EliteChannel::bandCoeffs (int band, double fs, BiquadCoeffs& out) const
{
    if (pEqOn->load() < 0.5f) return 0;
    const auto& p = pEq[(size_t) band];
    out = designBand (band, fs, p[0]->load(), p[1]->load(), p[2]->load());
    return out.isIdentity() ? 0 : 1;
}

void EliteChannel::prepareDsp (double fs, int)
{
    gate.prepare (fs);
    comp.prepare (fs);
    cache.fill (-1.0e9f);
    output.reset (fs, 0.02); makeup.reset (fs, 0.05);
    updateSettings();
    output.setCurrentAndTargetValue (output.getTargetValue());
    makeup.setCurrentAndTargetValue (makeup.getTargetValue());
}

void EliteChannel::resetDsp() noexcept
{
    gate.reset(); comp.reset();
    for (auto& b : eqSt) for (auto& s : b) s.reset();
}

void EliteChannel::updateSettings() noexcept
{
    std::array<float, 32> now {};
    int k = 0;
    for (auto* p : { pGateOn, pGateThr, pGateFloor, pGateRel, pEqOn, pCompOn, pCompThr, pCompRatio, pCompAtk, pCompRel, pCompMode })
        now[(size_t) k++] = param (p);
    for (auto& b : pEq) for (auto* p : b) now[(size_t) k++] = param (p);
    if (now == cache) return;
    cache = now;

    dsp::GateCore::Settings gs;
    gs.enabled = param (pGateOn) > 0.5f; gs.thresholdDb = param (pGateThr); gs.rangeDb = param (pGateFloor);
    gs.ratio = 4.0f; gs.attackMs = 0.5f; gs.holdMs = 25.0f; gs.releaseMs = param (pGateRel);
    gate.setSettings (gs);

    const bool vox = param (pCompMode) > 0.5f;
    dsp::CompressorCore::Settings cs;
    cs.thresholdDb = param (pCompThr); cs.ratio = param (pCompRatio); cs.attackMs = param (pCompAtk); cs.releaseMs = param (pCompRel);
    cs.kneeDb = vox ? 8.0f : 6.0f; cs.autoRelease = vox;
    comp.setSettings (cs);
    // Vox mode: automatic makeup (half of what a full-scale peak loses)
    const float slope = 1.0f - 1.0f / juce::jmax (1.0f, cs.ratio);
    makeup.setTargetValue (vox && param (pCompOn) > 0.5f ? params::dbToGain (-cs.thresholdDb * slope * 0.5f) : 1.0f);

    for (int b = 0; b < kEqBands; ++b)
    {
        const auto& p = pEq[(size_t) b];
        const auto next = designBand (b, sampleRateHz, param (p[0]), param (p[1]), param (p[2]));
        if (next.isIdentity() != eq[(size_t) b].isIdentity())
            for (auto& s : eqSt[(size_t) b]) s.reset();
        eq[(size_t) b] = next;
    }
}

void EliteChannel::processDsp (float* left, float* right, int n) noexcept
{
    updateSettings();
    output.setTargetValue (params::dbToGain (param (pOutput)) * (param (pPhase) > 0.5f ? -1.0f : 1.0f));
    const bool eqOn = param (pEqOn) > 0.5f, compOn = param (pCompOn) > 0.5f;
    const bool dynFirst = param (pOrder) > 0.5f;

    float maxGate = 0.0f, maxComp = 0.0f;
    auto runEq = [&] (float& l, float& r) noexcept
    {
        if (! eqOn) return;
        for (int b = 0; b < kEqBands; ++b)
            if (! eq[(size_t) b].isIdentity())
            {
                l = eqSt[(size_t) b][0].process (eq[(size_t) b], l);
                r = eqSt[(size_t) b][1].process (eq[(size_t) b], r);
            }
    };
    auto runDyn = [&] (float& l, float& r) noexcept
    {
        const float g = gate.process (l, r);
        maxGate = juce::jmax (maxGate, g);
        const float gg = dsp::dbToLin (-g);
        l *= gg; r *= gg;
        if (compOn)
        {
            const float c = comp.process (l, r);
            maxComp = juce::jmax (maxComp, c);
            const float cg = dsp::dbToLin (-c) * makeup.getNextValue();
            l *= cg; r *= cg;
        }
    };

    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];
        if (dynFirst) { runDyn (l, r); runEq (l, r); }
        else          { runEq (l, r); runDyn (l, r); }
        const float o = output.getNextValue();
        left[i] = l * o; right[i] = r * o;
    }
    setReadout (gateGr, maxGate);
    setReadout (compGr, maxComp);
}
} // namespace pg
