#pragma once
// EliteHush engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "hem/pg/Fft.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE HUSH - hiss and noise remover. Learns the noise floor of every frequency on its own
    (the quietest level it keeps coming back to) and turns down only what sits near that floor,
    so the music on top is left alone. LISTEN plays just what is being removed. */
class EliteHush final : public Plugin
{
public:
    static const PluginInfo info;
    EliteHush();
    enum Readout { removedDb, noiseDb };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;
    int dspLatency() const noexcept override { return fftSize; }

private:
    void processFrame() noexcept;
    std::atomic<float> *pAmount, *pReduction, *pFrom, *pListen, *pOutput;
    int fftSize = 2048, hop = 512, pos = 0, hopCount = 0;
    dsp::Fft fft;
    std::vector<float> window, inL, inR, outL, outR;
    std::vector<std::complex<float>> bufL, bufR;
    std::vector<float> power, smoothed, noise, gain, gainTmp;
    // noise floor = lowest level over the last ~4 s (8 slots of 0.5 s each), per frequency
    static constexpr int kSlots = 8;
    std::vector<float> slotMin, curMin;
    int framesPerSlot = 47, slotFrame = 0, slot = 0, slotsFilled = 0;
    float removedAvg = 0.0f;
    SmoothedValue<float> output;
};
} // namespace hem::pg
