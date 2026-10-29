#pragma once
// EliteTune engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE TUNE - vocal pitch correction. Hears the note being sung (one voice), finds the nearest
    note of the chosen key / scale and pulls the pitch there. SPEED 0 = hard robot snap, slower =
    natural. Works on one voice at a time (lead vocal, a solo instrument), not chords. */
class EliteTune final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray keys() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    static StringArray scales() { return { "Chromatic", "Major", "Minor" }; }
    EliteTune();
    enum Readout { detectedHz, correctionCents, targetMidi };

    /** Nearest allowed MIDI note (fractional input) for key / scale. */
    static float snapToScale (float midi, int key, int scale) noexcept;

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

    int dspLatency() const noexcept override { return shiftWin / 2; }   // the shifter rests at half its window

private:
    float detectPitch() noexcept;
    std::atomic<float> *pKey, *pScale, *pSpeed, *pAmount, *pLow, *pHigh, *pOutput;
    // pitch detection runs on a mono copy at ~24 kHz (cheaper, plenty for voices up to 1 kHz)
    int decim = 2, decimCount = 0;
    float decimAcc = 0.0f;
    double anaRate = 24000.0;
    std::vector<float> ana, frame, yin;   // ring, linear copy, YIN difference function
    int anaPos = 0, anaWin = 480, tauMaxAlloc = 482, hopCount = 0, hop = 128;
    // pitch-shift delay line (two crossfading read heads)
    std::vector<float> lineL, lineR;
    int writePos = 0, shiftWin = 1440;
    double phase = 0.5;
    float ratio = 1.0f, targetRatio = 1.0f, lastHz = 0.0f, lastCents = 0.0f, lastTarget = 0.0f;
    SmoothedValue<float> output;
};
} // namespace hem::pg
