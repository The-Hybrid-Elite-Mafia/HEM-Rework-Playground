#pragma once
// =============================================================================
//  MediaLoader — turns audio files (WAV, AIFF, FLAC, Ogg Vorbis, MP3) into SampleData.
//  Files at another rate than the engine's are converted once here with the sinc converter, so
//  the audio thread only interpolates after a device-rate change.
//  Any background thread (no shared state except the target rate, which is atomic).
// =============================================================================
#include "SampleData.hpp"

#include <functional>
#include <map>
#include <string>

namespace hem::pg
{
struct MediaLoader
{
    /** The rate files are converted to (the engine sets it when it starts). 0 = keep native rate. */
    static void setTargetRate (double rate) noexcept;
    static double getTargetRate() noexcept;

    static constexpr double kMaxMinutes = 20.0;

    /** How MP3 start padding is handled. Legacy = keep it, like 0.10.0 (old sessions line up exactly).
        Gapless = remove the encoder delay the file declares (LAME / Xing header), so the music starts at 0. */
    enum class Mp3Start { legacy, gapless };
    static void setMp3Start (Mp3Start) noexcept;
    static Mp3Start getMp3Start() noexcept;

    /** Decodes one file. Null + a plain-words reason in `error` if it cannot. */
    static SampleData::Ptr decode (const std::filesystem::path&, std::string& error);

    /** Decodes one file at its own rate (no conversion), e.g. for instrument samples. */
    static SampleData::Ptr decodeNative (const std::filesystem::path&, std::string& error);

    /** File-chooser pattern: "*.wav;*.bwf;*.aiff;*.aif;*.flac;*.ogg;*.mp3" */
    static std::string wildcard();
    static bool isAudioFile (const std::filesystem::path&);

    /** Decodes every path once; failures are listed in `failed` as plain-words reasons. Keys are the
        paths exactly as given (sessions store them as text). */
    static std::map<std::string, SampleData::Ptr> decodeAll (const std::vector<std::string>& paths, std::vector<std::string>& failed,
                                                             const std::function<bool()>& shouldCancel = {});

private:
    static SampleData::Ptr decodeAt (const std::filesystem::path&, double rate, std::string& error);
};
} // namespace hem::pg
