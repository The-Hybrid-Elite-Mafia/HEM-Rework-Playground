#pragma once
// =============================================================================
//  OfflineExport — renders a session to a WAV (or MP3) file on its own engine, while the live
//  engine keeps playing. Same steps as 0.10.0, so the same session gives the same file.
//  Construct on the message thread (it builds the engine and plug-ins), then render() on any thread.
// =============================================================================
#include "PlaygroundEngine.hpp"

#include <functional>

namespace hem::pg
{
struct ExportOptions
{
    std::filesystem::path file;
    double sampleRate = 48000.0;
    int bitDepth = 24;               // 16 (TPDF dither), 24, 32 float
    double tailSeconds = 3.0;        // reverb / delay ring-out after the last track ends
    bool beforeMode = false;         // export the BEFORE (all inserts bypassed) version
    /** CLEAN instead of MASTERED: master inserts bypassed, and the file turned down if needed so its
        peaks sit at or under kCleanPeakDb (never turned up). For a mastering engineer or another app. */
    bool clean = false;
    static constexpr float kCleanPeakDb = -3.0f;
    /** > 0: an MP3 at this bitrate instead of a WAV (16-bit WAV first, then Windows' MP3 encoder). 44.1 / 48 kHz only. */
    int mp3Kbps = 0;
};

class OfflineExport
{
public:
    OfflineExport (const Tree& session, const std::map<std::string, SampleData::Ptr>& media, ExportOptions);

    bool render (const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, std::string& error);
    /** The file render() writes: options.file with .wav or .mp3. */
    std::filesystem::path getTargetFile() const;

    int64 getLengthSamples() const noexcept { return lengthSamples; }
    /** Highest output sample written (after render); > 1 means the file clips. */
    float getPeak() const noexcept { return peak; }
    /** CLEAN export: gain applied to keep the peaks under kCleanPeakDb (1 = left alone). */
    float getCleanGain() const noexcept { return cleanGain; }

    static constexpr int kRenderBlock = 1024;

    /** 16-bit dither is (random A - random B) x 1 LSB. 0.10.0's compiler picked which of the two random
        numbers is drawn first; this switch exists only so the song checker can find out which. */
    static void setDitherDrawOrderForCheck (bool secondFirst) noexcept;
    static bool ditherDrawOrderSecondFirst() noexcept;

private:
    bool renderWav (const std::filesystem::path& target, const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, std::string& error);

    PlaygroundEngine engine;
    ExportOptions options;
    int64 lengthSamples = 0;
    int latency = 0;
    float peak = 0.0f;
    float cleanGain = 1.0f;
};
} // namespace hem::pg
