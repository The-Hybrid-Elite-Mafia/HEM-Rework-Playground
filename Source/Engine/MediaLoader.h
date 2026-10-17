#pragma once

#include "Engine/SampleData.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>

namespace pg
{
/** Decodes audio files (WAV, AIFF, FLAC, OGG, MP3) into SampleData. Files at another rate than the
    engine's are converted once here with the high-quality sinc converter (SincResampler); the engine's
    on-the-fly interpolation is only left for a device-rate change after loading.
    Thread-safe: each call uses its own format manager, so it can run on any background thread. */
struct MediaLoader
{
    /** The rate files are converted to (the engine sets it in prepareToPlay). 0 = keep native rate. */
    static void setTargetRate (double rate) noexcept;
    static double getTargetRate() noexcept;
    static constexpr double kMaxMinutes = 20.0;
    static SampleData::Ptr decode (const juce::File&, juce::String& error);
    /** Wildcard for file choosers: "*.wav;*.aif;..." */
    static juce::String wildcard();
    static bool isAudioFile (const juce::File&);
    /** Decodes every file (skipping duplicates); failures are listed in `failed` as "name: reason". */
    static std::map<juce::String, SampleData::Ptr> decodeAll (const juce::StringArray& paths, juce::StringArray& failed,
                                                              const std::function<bool()>& shouldCancel = {});
};
} // namespace pg
