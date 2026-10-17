#pragma once

#include "Engine/PlaygroundEngine.h"

namespace pg
{
struct ExportOptions
{
    juce::File file;
    double sampleRate = 48000.0;
    int bitDepth = 24;               // 16 (TPDF dither), 24, 32 float
    double tailSeconds = 3.0;        // reverb / delay ring-out after the last track ends
    bool beforeMode = false;         // export the BEFORE (all inserts bypassed) version
    /** CLEAN instead of MASTERED: master inserts bypassed (no loudness / limiting), and the file is
        turned down if needed so its peaks sit at or under kCleanPeakDb (never turned up). For a
        mastering engineer or another app to finish. */
    bool clean = false;
    static constexpr float kCleanPeakDb = -3.0f;
};

/**
    Offline WAV render of a session, on its own engine instance (the live engine keeps playing).
    Construct on the message thread (builds the engine and plugins), then call render() on any thread.
*/
class OfflineExport
{
public:
    OfflineExport (const juce::ValueTree& session, const std::map<juce::String, SampleData::Ptr>& media, ExportOptions);

    bool render (const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, juce::String& error);

    juce::int64 getLengthSamples() const noexcept { return lengthSamples; }
    /** Highest output sample written (after render); > 1 means the file clips. */
    float getPeak() const noexcept { return peak; }
    /** CLEAN export: gain applied to keep the peaks under kCleanPeakDb (1 = left alone). */
    float getCleanGain() const noexcept { return cleanGain; }

private:
    PlaygroundEngine engine;
    ExportOptions options;
    juce::int64 lengthSamples = 0;
    int latency = 0;
    float peak = 0.0f;
    float cleanGain = 1.0f;
};
} // namespace pg
