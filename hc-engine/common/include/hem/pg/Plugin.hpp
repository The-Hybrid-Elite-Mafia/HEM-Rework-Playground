#pragma once
// =============================================================================
//  Base of every Playground plug-in engine (no JUCE).
//
//  THREAD CONTRACT
//    prepare / reset / state / parameter setters: message thread (or an offline render thread
//    on its own copy).
//    processStereo: audio thread. Never allocates, never locks, never waits.
//    Readouts (meters, gain reduction) go audio -> UI through relaxed atomics; the analyzer FIFO is
//    single-producer / single-consumer and only fed while a screen asks for it.
//
//  Bypass (the "bypass" control every plug-in has), the engine-wide BEFORE/AFTER switch and a
//  group switch crossfade over 5 ms against a dry copy delayed by the plug-in's latency.
// =============================================================================
#include "hem/pg/Param.hpp"

#include <array>
#include <span>
#include <utility>

namespace hem::pg
{
/** Fixed facts about one plug-in type. */
struct PluginInfo
{
    const char* id;          // stable: saved in sessions and preset folders ("elite-vox")
    const char* name;        // "Elite Vocalis"
    const char* shortName;   // shown on insert buttons ("VOCL")
    const char* blurb;       // one line for menus / tooltips
    uint32 accent;           // faceplate accent colour (ARGB)
    bool masterOnly = false; // only allowed in MASTER slots
};

/** A plug-in's saved settings: every control's real value, by id. */
struct PluginState
{
    std::string pluginId;
    std::vector<std::pair<std::string, float>> values;
};

class Plugin
{
public:
    static constexpr int kNumReadouts = 12;
    static constexpr int kAnalyzerFifoSize = 1 << 14;

    Plugin (const PluginInfo& info, params::Layout layout);
    virtual ~Plugin() = default;
    Plugin (const Plugin&) = delete;
    Plugin& operator= (const Plugin&) = delete;

    const PluginInfo& getInfo() const noexcept { return info; }
    ParamSet& parameters() noexcept { return paramSet; }
    const ParamSet& parameters() const noexcept { return paramSet; }

    //==============================================================================
    /** Message thread. Allocates everything the audio thread will need. */
    void prepare (double sampleRate, int maxBlockSize);
    /** Clears all internal state (tails, envelopes). */
    void reset() noexcept;

    /** Audio thread. Stereo, in place. Longer blocks than prepared are split. */
    void processStereo (float* left, float* right, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return latency; }
    virtual double getTailLengthSeconds() const { return 0.0; }

    //==============================================================================
    PluginState saveState() const;
    /** Every control the state names is set; controls it does not name go back to default. */
    void loadState (const PluginState&);
    /** Every control back to its default (bypass included). */
    void resetToDefaults();

    /** Message thread: set a control by id (real units). False if the plug-in has no such control. */
    bool setParameter (std::string_view id, float value);
    /** Testing / replay only: put an exact real value straight into a control. */
    bool setRawParameter (std::string_view id, float value);
    float getParameter (std::string_view id) const;

    //==============================================================================
    void setGlobalBypass (const std::atomic<bool>* flag) noexcept { globalBypass = flag; }
    void setGroupBypass (const std::atomic<bool>* flag) noexcept { groupBypass = flag; }
    void setTempoSource (const std::atomic<float>* bpm) noexcept { tempo = bpm; }
    float getTempo() const noexcept { return tempo != nullptr ? tempo->load (std::memory_order_relaxed) : 120.0f; }

    float getReadout (int index) const noexcept { return readouts[(std::size_t) index].load (std::memory_order_relaxed); }
    float getInputPeak() const noexcept { return inPeak.load (std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outPeak.load (std::memory_order_relaxed); }
    bool isBypassedNow() const noexcept;

    void setAnalyzerActive (bool on) noexcept { analyzerActive.store (on); }
    int readAnalyzer (float* dest, int maxSamples) noexcept;
    double getPreparedSampleRate() const noexcept { return sampleRateHz; }

    float param (const std::atomic<float>* p) const noexcept { return p->load (std::memory_order_relaxed); }
    std::atomic<float>* raw (std::string_view id);

protected:
    virtual void prepareDsp (double sampleRate, int maxBlock) = 0;
    virtual void processDsp (float* left, float* right, int numSamples) noexcept = 0;
    virtual void resetDsp() noexcept = 0;
    virtual int dspLatency() const noexcept { return 0; }

    void setReadout (int index, float value) noexcept { readouts[(std::size_t) index].store (value, std::memory_order_relaxed); }
    bool isAnalyzerActive() const noexcept { return analyzerActive.load (std::memory_order_relaxed); }
    void pushAnalyzer (const float* left, const float* right, int numSamples) noexcept;

    const PluginInfo& info;
    ParamSet paramSet;
    double sampleRateHz = 48000.0;
    int maxBlockSize = 512;

private:
    std::atomic<float>* bypassParam = nullptr;
    const std::atomic<bool>* globalBypass = nullptr;
    const std::atomic<bool>* groupBypass = nullptr;
    const std::atomic<float>* tempo = nullptr;

    std::vector<float> alignedDryL, alignedDryR;            // maxBlock each
    std::vector<float> dryDelayL, dryDelayR;  // latency alignment of the dry path
    int dryDelayPos = 0, latency = 0;
    float wetMix = 1.0f, wetStep = 0.0f;      // 1 = processed, 0 = bypassed
    bool wasFullyBypassed = false;

    std::array<std::atomic<float>, kNumReadouts> readouts {};
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    std::atomic<bool> analyzerActive { false };
    AbstractFifo analyzerFifo { kAnalyzerFifoSize };
    std::vector<float> analyzerData = std::vector<float> ((std::size_t) kAnalyzerFifoSize, 0.0f);
};
} // namespace hem::pg
