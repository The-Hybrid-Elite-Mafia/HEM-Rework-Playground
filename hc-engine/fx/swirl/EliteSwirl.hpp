#pragma once
// EliteSwirl engine — ported from HEM Rework Playground 0.10.0 (Source/Plugins). No JUCE.
#include "hem/pg/Plugin.hpp"
#include "hem/pg/Biquad.hpp"
#include "hem/pg/DspMath.hpp"
#include "hem/pg/Fft.hpp"
#include <complex>

namespace hem::pg
{
//==============================================================================
/** ELITE SWIRL KILLER - spectral Mid/Side de-swirl for AI-generated audio.

    Every STFT bin is split into Mid (what L and R share) and Side (the difference). Real panning
    and width move together with the Mid, so over ~130 ms the Side of such a bin is coherent with
    its Mid. Codec swirl / shimmer is Side that has no steady relation to the Mid (low Mid/Side
    coherence) and flickers. Above FROM, those bins are treated; the Mid is never touched, so a
    centred vocal breath at 12 kHz survives while the swirl around it goes.
      REMOVE - turns the flagged Side down (to -26 dB at most).
      REPAIR - keeps the part of the Side that follows the Mid (stable panning / width) and drops
               only the erratic rest.
    PROTECT leaves very wide bins alone (deliberate stereo: wide pads, stereo reverbs).
    LISTEN plays only what is being taken out.

    Latency: one FFT frame (2048 samples up to 64 kHz, 4096 above); the engine compensates it.
    The editor reads a lock-free FIFO of per-band frames (level, swirl, stereo spread) for the 3D view. */
class EliteSwirl final : public Plugin
{
public:
    static const PluginInfo info;
    static StringArray modes() { return { "Remove", "Repair" }; }
    static constexpr int kBands = 48;
    static constexpr int kFrameFifo = 256;
    struct Frame { std::array<float, kBands> level {}, swirl {}, spread {}, pan {}; };

    EliteSwirl();
    double getTailLengthSeconds() const override { return 0.0; }
    enum Readout { removedDb, swirlPercent, coherence, whistleHz, shieldActive };

    /** Message thread (UI): pops up to `max` frames, oldest first. */
    int readFrames (Frame* dest, int max) noexcept;
    /** Centre frequency of display band b. */
    static float bandHz (int b) noexcept;

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;
    int dspLatency() const noexcept override { return fftSize; }

private:
    void processFrame() noexcept;
    std::atomic<float> *pAmount, *pFrom, *pProtect, *pMode, *pListen, *pOutput, *pWhistle, *pShield;
    int fftSize = 2048, hop = 512, pos = 0, hopCount = 0;
    dsp::Fft fft;
    std::vector<float> window, inL, inR, outL, outR;
    std::vector<std::complex<float>> bufL, bufR;
    // per-bin state
    std::vector<std::complex<float>> smCross;
    std::vector<float> sPowAvg;
    std::vector<float> mPowAvg, gain, gainTmp, sideSlow, whistleGain, whistleTmp;
    float hfAvg = 0.0f, corrAvg = 1.0f;
    int shieldHold = 0;
    std::vector<std::array<float, 5>> sideHist;
    std::vector<int> bandOf;
    std::vector<float> bandCount;
    int histPos = 0;
    SmoothedValue<float> output;
    AbstractFifo frameFifo { kFrameFifo };
    std::vector<Frame> frames = std::vector<Frame> ((size_t) kFrameFifo);
    Frame work;
};
} // namespace hem::pg
