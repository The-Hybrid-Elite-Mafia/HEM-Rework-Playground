#pragma once

#include "Plugins/PluginRegistry.h"
#include "Engine/SampleData.h"
#include <functional>
#include <map>

namespace pg
{
/**
    The Rework Playground mixer.

        32 TRACKS  (stem / WAV player -> 8 inserts -> fader + pan -> output: master or a bus,
                    plus two post-fader sends to any bus)
         6 BUSES   (sum -> 8 inserts -> fader + pan -> master)
         1 MASTER  (sum -> fader -> 8 inserts -> device / export; the fader drives the limiter)

    Strips are addressed by one index: tracks 0..31, buses 32..37, master 38.

    THREADS
      Message thread: everything that changes structure (insert / remove plugins, add / remove
      tracks, session load). New plugins are fully prepared before they are published with an
      atomic pointer store; replaced plugins and media are retired and only deleted by
      collectGarbage() once the audio thread has finished two more blocks.
      Audio thread: processBlock only reads atomics and pre-allocated buffers. No locks, no
      allocation. structureLock is shared only by prepareToPlay / releaseResources and the message
      thread (never by processBlock), so a device restart cannot race a plugin insert.
      Mixer controls are APVTS parameters (automation-ready, attachment-bound in the UI).
*/
class PlaygroundEngine final : public juce::AudioProcessor
{
public:
    static constexpr int kMaxTracks = 32, kNumBuses = 6, kSlots = 8;
    static constexpr int kFirstBus = kMaxTracks, kMaster = kMaxTracks + kNumBuses, kNumStrips = kMaster + 1;
    static constexpr float kFaderOffDb = -60.0f;

    static bool isTrack (int strip) noexcept { return strip >= 0 && strip < kMaxTracks; }
    static bool isBus (int strip) noexcept   { return strip >= kFirstBus && strip < kMaster; }
    static int busStrip (int bus) noexcept   { return kFirstBus + bus; }

    /** Parameter ids. Track / bus numbers are 1-based in ids ("t1_vol", "b3_pan"). */
    struct Ids
    {
        static juce::String track (int t, const char* what) { return "t" + juce::String (t + 1) + "_" + what; }
        static juce::String bus (int b, const char* what)   { return "b" + juce::String (b + 1) + "_" + what; }
        static constexpr const char* masterVol = "m_vol";
        static constexpr const char* bpm = "bpm";
    };
    static juce::StringArray defaultBusNames() { return { "Drum Bus", "Vocal Bus", "Music Bus", "Reverb", "Delay", "FX Bus" }; }

    PlaygroundEngine();
    ~PlaygroundEngine() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    const juce::String getName() const override { return "HEM Rework Playground"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    double getEngineSampleRate() const noexcept { return fs.load(); }

    //============================================================================== LIBRARY preview
    /** Message thread: plays one file straight to the output, after the master (not processed by the
        inserts, not in exports, independent of the transport). Replaces any preview playing. */
    void startPreview (SampleData::Ptr media);
    void stopPreview();
    /** Carries on from where it stopped (from the top if it had played to the end). */
    void resumePreview();
    void seekPreview (double seconds) noexcept { previewSeek.store (juce::jmax (0.0, seconds)); }
    bool isPreviewPlaying() const noexcept { return previewOn.load(); }
    double getPreviewPosition() const noexcept { return (double) previewPos.load() / juce::jmax (1.0, fs.load()); }
    /** The file now in the preview (message thread), or null. */
    SampleData::Ptr getPreviewMedia() const { return previewOwned; }

    //============================================================================== tracks (message thread)
    /** First free track, or -1 when all 32 are in use. Mixer controls of the track go back to default. */
    int addTrack (SampleData::Ptr media, const juce::String& name);
    void removeTrack (int track);
    void setTrackMedia (int track, SampleData::Ptr media);
    bool isTrackUsed (int track) const noexcept { return isTrack (track) && tracks[(size_t) track].used.load(); }
    int getNumUsedTracks() const noexcept;
    SampleData::Ptr getTrackMedia (int track) const;
    juce::String getTrackName (int track) const;
    void setTrackName (int track, const juce::String&);
    juce::Colour getTrackColour (int track) const;
    void setTrackColour (int track, juce::Colour);
    juce::String getBusName (int bus) const;
    void setBusName (int bus, const juce::String&);
    juce::String getStripName (int strip) const;

    //============================================================================== inserts (message thread)
    /** Creates, prepares and publishes plugin `pluginId` in a slot. Returns it, or nullptr if the id
        is unknown or the plugin is master-only and the strip is not the master. */
    ElitePlugin* insertPlugin (int strip, int slot, const juce::String& pluginId);
    void removePlugin (int strip, int slot);
    /** Moves / swaps two slots of the same strip. */
    void swapSlots (int strip, int slotA, int slotB);
    ElitePlugin* getPlugin (int strip, int slot) const noexcept;
    bool canHost (int strip, const juce::String& pluginId) const;
    /** Total latency of the master inserts (Elite Master look-ahead). */
    int getMasterLatencySamples() const;
    /** Whole-mix latency: compensated track latency + bus latency + master inserts. Export trims it. */
    int getTotalLatencySamples() const noexcept { return totalLatency.load(); }
    /** Longest latency the delay compensation can absorb per stage (samples). */
    static constexpr int kMaxCompensation = 16384;

    /** Deletes retired plugins / media once the audio thread can no longer see them. Message-thread timer. */
    void collectGarbage();
    int pendingGarbage() const noexcept { return (int) retired.size(); }

    //============================================================================== transport (any thread)
    void play() noexcept  { playing.store (true); }
    void stop() noexcept  { playing.store (false); }
    bool isPlaying() const noexcept { return playing.load(); }
    void setPositionSeconds (double seconds) noexcept;
    double getPositionSeconds() const noexcept;
    void setLoop (bool on, double startSeconds, double endSeconds) noexcept;
    bool isLooping() const noexcept { return loopOn.load(); }
    double getLoopStart() const noexcept { return loopStart.load(); }
    double getLoopEnd() const noexcept { return loopEnd.load(); }
    /** Longest track in seconds (message thread keeps it up to date). */
    double getSongLengthSeconds() const noexcept { return songLength.load(); }
    /** Playback stops by itself at the song end unless looping. */
    void setStopAtEnd (bool on) noexcept { stopAtEnd.store (on); }

    /** BEFORE / AFTER: true bypasses every insert of every strip (5 ms crossfades). */
    void setBeforeMode (bool before) noexcept { beforeMode.store (before); }
    /** CLEAN export: the master inserts (Elite Master etc.) are bypassed, tracks and buses keep theirs. */
    void setMasterInsertsBypassed (bool off) noexcept { masterInsertsOff.store (off); }
    bool isBeforeMode() const noexcept { return beforeMode.load(); }

    //============================================================================== master tap (3D analyzer)
    /** While active, the audio thread copies the master output (mono sum) into a lock-free FIFO.
        When the FIFO is full the newest samples are dropped; nothing waits. */
    static constexpr int kTapSize = 1 << 15;
    void setMasterTapActive (bool on) noexcept { tapActive.store (on, std::memory_order_relaxed); }
    /** UI thread: takes up to maxSamples samples out of the tap. Returns how many. */
    int readMasterTap (float* dest, int maxSamples) noexcept;

    //============================================================================== meters (UI reads)
    float getPeak (int strip, int channel) const noexcept { return meters[(size_t) strip][(size_t) channel].load (std::memory_order_relaxed); }

    //============================================================================== session (message thread)
    /** Everything that makes a song: mixer, inserts + their settings, track files, names, transport. */
    juce::ValueTree createSessionTree() const;
    /** Applies a session tree. `media` maps the absolute file path stored per track to decoded audio
        (decode them first, off the message thread); tracks whose file is missing keep their slot
        with no audio and are reported in `missing`. Returns false for a tree that is not a session. */
    bool applySessionTree (const juce::ValueTree&, const std::map<juce::String, SampleData::Ptr>& media, juce::StringArray* missing = nullptr);
    /** Empty session with the default bus names, Elite Verb on Reverb and Elite Delay on Delay. */
    void newSession();
    static juce::StringArray mediaFilesIn (const juce::ValueTree& session);
    static constexpr const char* kSessionType = "HEMPLAYGROUND_SESSION";

private:
    struct Strip
    {
        std::array<std::atomic<ElitePlugin*>, kSlots> slot {};
        std::array<std::unique_ptr<ElitePlugin>, kSlots> owned;   // message thread
        juce::SmoothedValue<float> gainL, gainR, sendA, sendB;     // audio thread
        std::atomic<float>* vol = nullptr; std::atomic<float>* pan = nullptr; std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr; std::atomic<float>* out = nullptr;
        std::atomic<float>* sa = nullptr; std::atomic<float>* saBus = nullptr;
        std::atomic<float>* sb = nullptr; std::atomic<float>* sbBus = nullptr;
    };
    struct Track
    {
        std::atomic<bool> used { false };
        std::atomic<SampleData*> media { nullptr };
        SampleData::Ptr mediaOwned;                 // message thread
        juce::String name; juce::Colour colour;      // message thread
    };
    struct Retired { std::unique_ptr<ElitePlugin> plugin; SampleData::Ptr media; juce::uint64 epoch; };
    std::atomic<SampleData*> preview { nullptr };
    SampleData::Ptr previewOwned;                   // message thread
    std::atomic<bool> previewOn { false };
    std::atomic<juce::int64> previewPos { 0 };
    std::atomic<double> previewSeek { -1.0 };

    void processChunk (juce::int64 pos, bool play, int n) noexcept;
    void snapGains() noexcept;
    /** Message thread (or prepare): recomputes plugin delay compensation from the inserts. */
    void updateLatencies();
    int stripLatency (int strip) const;
    static void delayInPlace (float* line, int len, int writePos, float* io, int n, int d) noexcept;
    void readMedia (const SampleData& media, juce::int64 pos, float* l, float* r, int n) const noexcept;
    void retire (std::unique_ptr<ElitePlugin>, SampleData::Ptr);
    void resetTrackControls (int track);
    void updateSongLength();
    void wirePlugin (ElitePlugin&, int strip);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    std::array<Strip, kNumStrips> strips;
    std::array<Track, kMaxTracks> tracks;
    juce::StringArray busNames = defaultBusNames();
    std::array<std::array<std::atomic<float>, 2>, kNumStrips> meters {};

    std::atomic<double> fs { 48000.0 };
    std::atomic<int> maxBlock { 512 };
    juce::CriticalSection structureLock;   // never taken by processBlock
    std::vector<Retired> retired;
    std::atomic<juce::uint64> blocksDone { 0 };
    std::atomic<bool> audioRunning { false };

    std::atomic<bool> playing { false }, loopOn { false }, stopAtEnd { true }, beforeMode { false }, masterInsertsOff { false };
    std::atomic<juce::int64> position { 0 };          // engine samples
    std::atomic<bool> tapActive { false };
    juce::AbstractFifo tapFifo { kTapSize };
    std::vector<float> tapData = std::vector<float> ((size_t) kTapSize, 0.0f);
    std::atomic<double> locateRequest { -1.0 };       // seconds, -1 = none
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 }, songLength { 0.0 };

    juce::AudioBuffer<float> trackBuf, busBuf, masterBuf, directBuf;   // 2, 2 * kNumBuses, 2, 2 channels x maxBlock

    // Plugin delay compensation: every track is delayed to the slowest track, every bus to the
    // slowest bus, and tracks going straight to the master wait for the bus path too.
    std::array<std::atomic<int>, kMaxTracks> trackComp {};
    std::array<std::atomic<int>, kNumBuses> busComp {};
    std::atomic<int> directComp { 0 }, totalLatency { 0 };
    juce::AudioBuffer<float> trackLines, busLines, directLines;   // 2 per track / bus / direct, lineLength samples
    int lineLength = 0, linePos = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaygroundEngine)
};
} // namespace pg
