#pragma once

#include "ElitePlugin.h"
#include "Dsp/Dynamics.h"
#include "Dsp/Loudness.h"
#include "Engine/Biquad.h"
#include "Engine/LookaheadLimiter.h"
#include "Dsp/Fft.h"
#include <complex>

namespace pg
{
//==============================================================================
/** ELITE VOX - one-move vocal / drum leveller: gate, SQUEEZE compressor with automatic makeup,
    built-in output limiter, output level. Modes set the timing for vocals, drums, bass or a bus. */
class EliteVox final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray modes() { return { "Vocal", "Drums", "Bass", "Bus" }; }
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
    juce::SmoothedValue<float> makeup, output;
    float lastGate = 1.0e9f, lastSqueeze = 1.0e9f; int lastMode = -1;
};

//==============================================================================
/** ELITE EQ - six-band paragraphic EQ (cuts are 24 dB/oct) with a live spectrum analyzer. */
class EliteEq final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static constexpr int kBands = 6;
    static juce::StringArray typesFor (int band);
    /** Default type/frequency for a band. */
    static float defaultFreq (int band);
    EliteEq();

    /** Message thread (UI curve): the 1-2 biquads of a band for the current parameters. Returns stages (0 = band off / flat). */
    int bandCoeffs (int band, double sampleRate, BiquadCoeffs out[2]) const;
    /** Message thread (UI curve): what the EQ does at `hz` in dB for the current settings, all bands + OUTPUT. */
    float responseDb (double sampleRate, double hz) const;
    static juce::String id (int band, const char* what) { return "b" + juce::String (band + 1) + "_" + what; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    struct BandParams { std::atomic<float> *on, *type, *freq, *gain, *q; };
    std::array<BandParams, kBands> bp {};
    std::atomic<float>* pOutput = nullptr;
    std::array<std::array<BiquadCoeffs, 2>, kBands> coeffs {};
    std::array<int, kBands> stages {};
    std::array<std::array<std::array<BiquadState, 2>, 2>, kBands> st {};   // band x channel x stage
    std::array<std::array<float, 5>, kBands> cache {};
    juce::SmoothedValue<float> output;
};

//==============================================================================
/** ELITE COMP - smooth bus / vocal compressor with auto release (ARC), three characters
    (Clean, Electro, Warm) and parallel MIX. */
class EliteComp final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray modes() { return { "Clean", "Electro", "Warm" }; }
    EliteComp();
    enum Readout { gr };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pThr, *pRatio, *pAtk, *pRel, *pArc, *pMode, *pGain, *pMix;
    dsp::CompressorCore comp;
    juce::SmoothedValue<float> makeup, mix;
    std::array<float, 8> cache {};
};

//==============================================================================
/** ELITE BASS - psychoacoustic bass: adds the 2nd-4th harmonics of the bass below FREQ so the low
    end is heard on small speakers, and lets you turn the original sub down (ORIGINAL). */
class EliteBass final : public ElitePlugin
{
public:
    static const PluginInfo info;
    EliteBass();
    enum Readout { originalLevel, harmonicsLevel };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateFilters (float freq) noexcept;
    std::atomic<float> *pFreq, *pIntensity, *pOriginal, *pOutput, *pSolo;
    BiquadCoeffs lp, hp, hHp, hLp;
    std::array<std::array<BiquadState, 2>, 2> lowSt {}, highSt {};   // channel x stage
    std::array<BiquadState, 2> hHpSt {}, hLpSt {};
    float env = 0.0f, envAtt = 0.0f, envRel = 0.0f, lastFreq = -1.0f;
    juce::SmoothedValue<float> intensity, original, output;
};

//==============================================================================
/** ELITE CHANNEL - gate + 4-band EQ + compressor channel strip, order switchable. */
class EliteChannel final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray orders() { return { "EQ > Dyn", "Dyn > EQ" }; }
    static juce::StringArray compModes() { return { "Clean", "Vox" }; }
    static constexpr int kEqBands = 4;
    static juce::String eqId (int band, const char* what) { return "eq" + juce::String (band + 1) + "_" + what; }
    EliteChannel();
    enum Readout { gateGr, compGr };

    int bandCoeffs (int band, double sampleRate, BiquadCoeffs& out) const;

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateSettings() noexcept;
    std::atomic<float> *pGateOn, *pGateThr, *pGateFloor, *pGateRel;
    std::atomic<float> *pEqOn;
    std::array<std::array<std::atomic<float>*, 3>, kEqBands> pEq {};   // freq, gain, q
    std::atomic<float> *pCompOn, *pCompThr, *pCompRatio, *pCompAtk, *pCompRel, *pCompMode;
    std::atomic<float> *pOutput, *pPhase, *pOrder;
    dsp::GateCore gate;
    dsp::CompressorCore comp;
    std::array<BiquadCoeffs, kEqBands> eq {};
    std::array<std::array<BiquadState, 2>, kEqBands> eqSt {};
    std::array<float, 32> cache {};
    juce::SmoothedValue<float> output, makeup;
};

//==============================================================================
/** ELITE VERB - 8-line feedback-delay-network reverb (Room / Plate / Hall). */
class EliteVerb final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray types() { return { "Room", "Plate", "Hall" }; }
    EliteVerb();
    double getTailLengthSeconds() const override { return 12.0; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    static constexpr int kLines = 8, kDiffusers = 4;
    struct Line { std::vector<float> buf; int write = 0; float length = 1000.0f, target = 1000.0f; float lp = 0.0f; float gain = 0.5f; };
    struct Allpass { std::vector<float> buf; int pos = 0; int length = 100; };
    void updateTargets() noexcept;
    float readFrac (const Line&, float delay) const noexcept;

    std::atomic<float> *pPre, *pSize, *pDecay, *pDamp, *pLowCut, *pMix, *pWidth, *pType;
    std::array<Line, kLines> lines;
    std::array<Allpass, kDiffusers> diffusers;
    std::vector<float> pre; int preWrite = 0;
    BiquadCoeffs lowCut; std::array<BiquadState, 2> lowCutSt {};
    float dampCoeff = 0.3f, lfoPhase = 0.0f, lfoInc = 0.0f, diffusion = 0.7f;
    std::array<float, 8> cache {};
    juce::SmoothedValue<float> mix, width;
};

//==============================================================================
/** ELITE DELAY - stereo / ping-pong delay with tempo sync, filtered and saturated feedback and ducking. */
class EliteDelay final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray divisions() { return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/1" }; }
    static double divisionBeats (int index);
    EliteDelay();
    double getTailLengthSeconds() const override { return 8.0; }
    enum Readout { delayMs };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pTime, *pSync, *pDiv, *pFeedback, *pLowCut, *pHighCut, *pPing, *pMix, *pDuck;
    std::vector<float> bufL, bufR;
    int write = 0;
    float current = 0.0f, duckEnv = 0.0f, duckRel = 0.0f;
    BiquadCoeffs hp, lp; std::array<BiquadState, 2> hpSt {}, lpSt {};
    float lastLow = -1.0f, lastHigh = -1.0f;
    juce::SmoothedValue<float> feedback, mix, ping;
};

//==============================================================================
/** ELITE MASTER - final look-ahead limiter with character, true-peak and LUFS metering and
    "match target" for streaming loudness. MASTER slots only (it has 1.5 ms of look-ahead). */
class EliteMaster final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray characters() { return { "Clean", "Punch", "Warm" }; }
    static juce::StringArray targets() { return { "Streaming -14 LUFS", "Apple -16 LUFS", "Loud -9 LUFS", "Club -7 LUFS" }; }
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
    juce::SmoothedValue<float> input;
    float lastCeil = 1.0e9f, lastRel = -1.0f;
    std::atomic<bool> resetRequest { false };
};
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
class EliteSwirl final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray modes() { return { "Remove", "Repair" }; }
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
    juce::SmoothedValue<float> output;
    juce::AbstractFifo frameFifo { kFrameFifo };
    std::vector<Frame> frames = std::vector<Frame> ((size_t) kFrameFifo);
    Frame work;
};
//==============================================================================
/** ELITE TRANSIENT - punch and sustain shaper: turns the hit (attack) and the ring-out (sustain)
    up or down without touching the level in between. No look-ahead, no latency. */
class EliteTransient final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray speeds() { return { "Fast", "Medium", "Slow" }; }
    EliteTransient();
    enum Readout { gainNow };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pAttack, *pSustain, *pSpeed, *pOutput, *pMix;
    float envFast = 0.0f, envSlow = 0.0f, envHold = 0.0f;
    float aFast = 0.0f, rFast = 0.0f, aSlow = 0.0f, rSlow = 0.0f, rHold = 0.0f;
    int lastSpeed = -1;
    juce::SmoothedValue<float> output, mix;
};

//==============================================================================
/** ELITE MULTIBAND - three-band compressor for mastering and busses. Linkwitz-Riley 24 dB/oct
    crossovers (the bands add back up flat), one compressor per band, per-band gain. */
class EliteMultiband final : public ElitePlugin
{
public:
    static const PluginInfo info;
    EliteMultiband();
    enum Readout { lowGr, midGr, highGr };
    static juce::String id (int band, const char* what) { return juce::String (band == 0 ? "low_" : band == 1 ? "mid_" : "high_") + what; }

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    void updateFilters() noexcept;
    struct Band { std::atomic<float> *thr, *ratio, *gain; dsp::CompressorCore comp; juce::SmoothedValue<float> g; };
    std::array<Band, 3> bands;
    std::atomic<float> *pLowX, *pHighX, *pAttack, *pRelease, *pMix, *pOutput;
    float lastLowX = -1.0f, lastHighX = -1.0f, lastAtk = -1.0f, lastRel = -1.0f;
    std::array<float, 3> lastThr { -999.0f, -999.0f, -999.0f }, lastRatio { 0.0f, 0.0f, 0.0f };
    BiquadCoeffs lp1, hp1, lp2, hp2;
    // [channel][stage]: low path (LP1 x2, then allpass at f2 = LP2 x2 + HP2 x2), rest path (HP1 x2), mid (LP2 x2), high (HP2 x2)
    std::array<std::array<BiquadState, 2>, 2> sLp1, sHp1, sLp2, sHp2, sApLo, sApHi;
    juce::SmoothedValue<float> mix, output;
};

//==============================================================================
/** ELITE WIDTH - stereo width with mono bass: makes the sides wider or narrower above a
    frequency and folds everything below it to mono (tight low end, safe on club systems). */
class EliteWidth final : public ElitePlugin
{
public:
    static const PluginInfo info;
    EliteWidth();
    enum Readout { correlation, sideLevel };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pWidth, *pMonoBelow, *pOutput;
    float lastMono = -1.0f;
    BiquadCoeffs hp;
    std::array<BiquadState, 2> sideHp;
    juce::SmoothedValue<float> width, output;
    double corrLR = 0.0, corrLL = 0.0, corrRR = 0.0, sideAvg = 0.0, totalAvg = 0.0;
    float corrCoeff = 0.999f;
};

//==============================================================================
/** ELITE HUSH - hiss and noise remover. Learns the noise floor of every frequency on its own
    (the quietest level it keeps coming back to) and turns down only what sits near that floor,
    so the music on top is left alone. LISTEN plays just what is being removed. */
class EliteHush final : public ElitePlugin
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
    juce::SmoothedValue<float> output;
};

//==============================================================================
/** ELITE DE-ESS - sibilance tamer: listens to the S / T / SH range and turns only that range down
    while it is too loud. Everything below FREQUENCY is untouched. LISTEN = hear what it catches. */
class EliteDeEss final : public ElitePlugin
{
public:
    static const PluginInfo info;
    EliteDeEss();
    enum Readout { gr, sibilanceDb };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pFreq, *pThreshold, *pRange, *pListen, *pOutput;
    float lastFreq = -1.0f;
    BiquadCoeffs splitHp, splitLp, detBp;
    std::array<BiquadState, 2> hpL, hpR, lpL, lpR, det;
    float env = 0.0f, att = 0.0f, rel = 0.0f, grSmooth = 0.0f, grCoeff = 0.0f;
    juce::SmoothedValue<float> output;
};

//==============================================================================
/** ELITE DOUBLER - turns one vocal (or guitar) into a wide double: two slightly late, slightly
    drifting copies panned left and right behind the original. */
class EliteDoubler final : public ElitePlugin
{
public:
    static const PluginInfo info;
    EliteDoubler();
    enum Readout { spreadMs };

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pAmount, *pWidth, *pDelay, *pDrift, *pTone, *pOutput;
    std::vector<float> line;
    int writePos = 0;
    double phaseA = 0.0, phaseB = 0.37, phaseC = 0.71;
    float lastTone = -1.0f;
    BiquadCoeffs toneHp;
    std::array<BiquadState, 2> toneSt;
    juce::SmoothedValue<float> amount, width, delayMs, drift, output;
};

//==============================================================================
/** ELITE CONSOLE - mixing-desk glue: gentle channel saturation, transformer weight and air,
    a touch of left / right crosstalk. Output follows the input level (drive does not get louder). */
class EliteConsole final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray characters() { return { "Clean Desk", "British Iron", "American Punch", "Tube Bus" }; }
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
    juce::SmoothedValue<float> drive, crosstalk, output;
};

//==============================================================================
/** ELITE LO-FI - old-tape and old-sampler character: wow and flutter, bit depth and sample-rate
    crush, vinyl crackle and hiss, dark tone, dry / wet mix. */
class EliteLofi final : public ElitePlugin
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
    juce::uint32 rng = 0x48454D21u;
    float crackleEnv = 0.0f;
    juce::SmoothedValue<float> mix, output;
};

//==============================================================================
/** ELITE TUNE - vocal pitch correction. Hears the note being sung (one voice), finds the nearest
    note of the chosen key / scale and pulls the pitch there. SPEED 0 = hard robot snap, slower =
    natural. Works on one voice at a time (lead vocal, a solo instrument), not chords. */
class EliteTune final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static juce::StringArray keys() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    static juce::StringArray scales() { return { "Chromatic", "Major", "Minor" }; }
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
    juce::SmoothedValue<float> output;
};

//==============================================================================
/** ELITE BALANCE - automatic tone balance: listens to the whole mix for a few seconds, compares
    eight frequency regions to a smooth target tilt and gently corrects the ones that stick out or
    are missing. AMOUNT = how much of the correction to use, RANGE = the most any band may move. */
class EliteBalance final : public ElitePlugin
{
public:
    static const PluginInfo info;
    static constexpr int kBands = 8;
    static float bandHz (int b) noexcept { return 62.5f * (float) (1 << b); }   // 62.5 Hz .. 8 kHz
    static juce::StringArray speeds() { return { "Slow", "Medium", "Fast" }; }
    EliteBalance();
    /** readout b = correction in dB applied to band b (0..7) */

protected:
    void prepareDsp (double, int) override;
    void processDsp (float*, float*, int) noexcept override;
    void resetDsp() noexcept override;

private:
    std::atomic<float> *pAmount, *pTilt, *pRange, *pSpeed, *pFreeze, *pOutput;
    std::array<BiquadCoeffs, kBands> bp, eq;
    std::array<BiquadState, kBands> bpSt;
    std::array<std::array<BiquadState, kBands>, 2> eqSt;
    std::array<float, kBands> power {}, applied {};
    float envCoeff = 0.9999f;
    int sinceUpdate = 0, updateEvery = 256;
    double warmSamples = 0.0;
    juce::SmoothedValue<float> output;
};
} // namespace pg
