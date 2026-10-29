#pragma once
// =============================================================================
//  The Rework Playground mixer (no JUCE).
//
//      32 TRACKS  (stem / WAV player -> 8 inserts -> fader + pan -> output: master or a bus,
//                  plus two post-fader sends to any bus)
//       6 BUSES   (sum -> 8 inserts -> fader + pan -> master)
//       1 MASTER  (sum -> fader -> 8 inserts -> device / export; the fader drives the limiter)
//
//  Strips are addressed by one index: tracks 0..31, buses 32..37, master 38.
//
//  THREADS
//    Message thread: everything that changes structure (insert / remove plug-ins, add / remove
//    tracks, session load). New plug-ins are fully prepared before they are published with an
//    atomic pointer store; replaced plug-ins and media are retired and only freed by
//    collectGarbage() once the audio thread has finished two more blocks.
//    Audio thread: process() only reads atomics and buffers made in prepare(). No locks, no
//    allocation. structureLock is shared only by prepare / release and the message thread.
//
//  Same maths, same order and same session format as 0.10.0, so a song renders the same.
// =============================================================================
#include "hem/pg/Plugin.hpp"
#include "Presets.hpp"
#include "Registry.hpp"
#include "Tree.hpp"
#include "media/SampleData.hpp"

#include <array>
#include <atomic>
#include <map>
#include <mutex>

namespace hem::pg
{
class PlaygroundEngine final
{
public:
    static constexpr int kMaxTracks = 32, kNumBuses = 6, kSlots = 8;
    static constexpr int kFirstBus = kMaxTracks, kMaster = kMaxTracks + kNumBuses, kNumStrips = kMaster + 1;
    static constexpr float kFaderOffDb = -60.0f;
    /** Longest latency the delay compensation can absorb per stage (samples). */
    static constexpr int kMaxCompensation = 16384;
    static constexpr int kTapSize = 1 << 15;
    static constexpr const char* kSessionType = "HEMPLAYGROUND_SESSION";
    static constexpr const char* kMixerType = "PLAYGROUND_MIXER";

    static bool isTrack (int strip) noexcept { return strip >= 0 && strip < kMaxTracks; }
    static bool isBus (int strip) noexcept   { return strip >= kFirstBus && strip < kMaster; }
    static int busStrip (int bus) noexcept   { return kFirstBus + bus; }

    /** Control ids. Track / bus numbers are 1-based ("t1_vol", "b3_pan"). */
    struct Ids
    {
        static std::string track (int t, const char* what) { return "t" + std::to_string (t + 1) + "_" + what; }
        static std::string bus (int b, const char* what)   { return "b" + std::to_string (b + 1) + "_" + what; }
        static constexpr const char* masterVol = "m_vol";
        static constexpr const char* bpm = "bpm";
    };
    static StringArray defaultBusNames() { return { "Drum Bus", "Vocal Bus", "Music Bus", "Reverb", "Delay", "FX Bus" }; }

    PlaygroundEngine();
    ~PlaygroundEngine();
    PlaygroundEngine (const PlaygroundEngine&) = delete;
    PlaygroundEngine& operator= (const PlaygroundEngine&) = delete;

    //============================================================================== audio
    /** Message thread, before the device starts (and again on a rate / block-size change). */
    void prepare (double sampleRate, int maximumBlockSize);
    /** The device stopped: retired items are freed at once from now on. */
    void release();
    /** Audio thread. Writes `numSamples` to every output channel (1 = mono sum, 2+ = stereo, extra cleared). */
    void process (float* const* outputs, int numChannels, int numSamples) noexcept;

    double getSampleRate() const noexcept { return fs.load(); }

    //============================================================================== mixer controls
    ParamSet& mixer() noexcept { return mixerParams; }
    const ParamSet& mixer() const noexcept { return mixerParams; }
    /** Message thread: set a mixer control by id (real units). */
    bool setMixer (std::string_view id, float value);
    float getMixer (std::string_view id) const;

    //============================================================================== library preview
    void startPreview (SampleData::Ptr media);
    void stopPreview();
    void resumePreview();
    void seekPreview (double seconds) noexcept { previewSeek.store (jmax (0.0, seconds)); }
    bool isPreviewPlaying() const noexcept { return previewOn.load(); }
    double getPreviewPosition() const noexcept { return (double) previewPos.load() / jmax (1.0, fs.load()); }
    SampleData::Ptr getPreviewMedia() const { return previewOwned; }

    //============================================================================== tracks (message thread)
    /** First free track, or -1 when all 32 are in use. The track's mixer controls go back to default. */
    int addTrack (SampleData::Ptr media, const std::string& name);
    void removeTrack (int track);
    void setTrackMedia (int track, SampleData::Ptr media);
    bool isTrackUsed (int track) const noexcept { return isTrack (track) && tracks[(size_t) track].used.load(); }
    int getNumUsedTracks() const noexcept;
    SampleData::Ptr getTrackMedia (int track) const;
    std::string getTrackName (int track) const;
    void setTrackName (int track, const std::string&);
    uint32 getTrackColour (int track) const;
    void setTrackColour (int track, uint32 argb);
    std::string getBusName (int bus) const;
    void setBusName (int bus, const std::string&);
    std::string getStripName (int strip) const;
    static uint32 paletteColour (int track) noexcept;

    //============================================================================== inserts (message thread)
    /** Creates, prepares and publishes plug-in `pluginId` in a slot. Null if the id is unknown or the
        plug-in is master-only and the strip is not the master. */
    Plugin* insertPlugin (int strip, int slot, std::string_view pluginId);
    void removePlugin (int strip, int slot);
    void swapSlots (int strip, int slotA, int slotB);
    Plugin* getPlugin (int strip, int slot) const noexcept;
    bool canHost (int strip, std::string_view pluginId) const;
    int getMasterLatencySamples() const;
    int getTotalLatencySamples() const noexcept { return totalLatency.load(); }

    /** Frees retired plug-ins / media once the audio thread can no longer see them. */
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
    double getSongLengthSeconds() const noexcept { return songLength.load(); }
    void setStopAtEnd (bool on) noexcept { stopAtEnd.store (on); }

    /** BEFORE / AFTER: true bypasses every insert of every strip (5 ms crossfades). */
    void setBeforeMode (bool before) noexcept { beforeMode.store (before); }
    bool isBeforeMode() const noexcept { return beforeMode.load(); }
    /** CLEAN export: the master inserts are bypassed; tracks and buses keep theirs. */
    void setMasterInsertsBypassed (bool off) noexcept { masterInsertsOff.store (off); }

    //============================================================================== master tap (analyzer)
    void setMasterTapActive (bool on) noexcept { tapActive.store (on, std::memory_order_relaxed); }
    int readMasterTap (float* dest, int maxSamples) noexcept;

    //============================================================================== meters
    float getPeak (int strip, int channel) const noexcept { return meters[(size_t) strip][(size_t) channel].load (std::memory_order_relaxed); }

    //============================================================================== session (message thread)
    Tree createSessionTree() const;
    /** Applies a session. `media` maps the file path stored per track (text, as saved) to decoded audio.
        Tracks whose file is missing keep their slot with no audio and are listed in `missing`. */
    bool applySessionTree (const Tree&, const std::map<std::string, SampleData::Ptr>& media, StringArray* missing = nullptr);
    /** Empty session: default bus names, Elite Verb on Reverb, Elite Delay on Delay, Elite Master last. */
    void newSession();
    static StringArray mediaFilesIn (const Tree& session);

private:
    struct Strip
    {
        std::array<std::atomic<Plugin*>, kSlots> slot {};
        std::array<std::unique_ptr<Plugin>, kSlots> owned;   // message thread
        SmoothedValue<float> gainL, gainR, sendA, sendB;      // audio thread
        std::atomic<float>* vol = nullptr; std::atomic<float>* pan = nullptr; std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr; std::atomic<float>* out = nullptr;
        std::atomic<float>* sa = nullptr; std::atomic<float>* saBus = nullptr;
        std::atomic<float>* sb = nullptr; std::atomic<float>* sbBus = nullptr;
    };
    struct Track
    {
        std::atomic<bool> used { false };
        std::atomic<const SampleData*> media { nullptr };
        SampleData::Ptr mediaOwned;      // message thread
        std::string name;                // message thread
        std::string missingFile;         // message thread: the file a loaded session asked for but was not found (kept on save)
        uint32 colour = 0xff808080;
    };
    struct Retired { std::unique_ptr<Plugin> plugin; SampleData::Ptr media; std::uint64_t epoch; };

    void processChunk (int64 pos, bool play, int n) noexcept;
    void snapGains() noexcept;
    void updateLatencies();
    int stripLatency (int strip) const;
    static void delayInPlace (float* line, int len, int writePos, float* io, int n, int d) noexcept;
    void readMedia (const SampleData& media, int64 pos, float* l, float* r, int n) const noexcept;
    void retire (std::unique_ptr<Plugin>, SampleData::Ptr);
    void resetTrackControls (int track);
    void updateSongLength();
    void wirePlugin (Plugin&, int strip);
    static params::Layout createLayout();
    float* buf (std::vector<float>& v, int channel) noexcept { return v.data() + (size_t) channel * (size_t) bufStride; }
    float* line (std::vector<float>& v, int channel) noexcept { return v.data() + (size_t) channel * (size_t) lineLength; }

    ParamSet mixerParams;
    std::array<Strip, kNumStrips> strips;
    std::array<Track, kMaxTracks> tracks;
    StringArray busNames = defaultBusNames();
    std::array<std::array<std::atomic<float>, 2>, kNumStrips> meters {};

    std::atomic<const SampleData*> preview { nullptr };
    SampleData::Ptr previewOwned;
    std::atomic<bool> previewOn { false };
    std::atomic<int64> previewPos { 0 };
    std::atomic<double> previewSeek { -1.0 };

    std::atomic<double> fs { 48000.0 };
    std::atomic<int> maxBlock { 512 };
    std::recursive_mutex structureLock;   // never taken by process()
    std::vector<Retired> retired;
    std::atomic<std::uint64_t> blocksDone { 0 };
    std::atomic<bool> audioRunning { false };

    std::atomic<bool> playing { false }, loopOn { false }, stopAtEnd { true }, beforeMode { false }, masterInsertsOff { false };
    std::atomic<int64> position { 0 };
    std::atomic<bool> tapActive { false };
    AbstractFifo tapFifo { kTapSize };
    std::vector<float> tapData = std::vector<float> ((size_t) kTapSize, 0.0f);
    std::atomic<double> locateRequest { -1.0 };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 }, songLength { 0.0 };

    // 2 (track), 2 * kNumBuses (bus), 2 (master), 2 (direct) channels of bufStride samples
    std::vector<float> trackBuf, busBuf, masterBuf, directBuf;
    int bufStride = 0;

    std::array<std::atomic<int>, kMaxTracks> trackComp {};
    std::array<std::atomic<int>, kNumBuses> busComp {};
    std::atomic<int> directComp { 0 }, totalLatency { 0 };
    std::vector<float> trackLines, busLines, directLines;   // 2 per track / bus / direct, lineLength samples each
    int lineLength = 0, linePos = 0;
};
} // namespace hem::pg
