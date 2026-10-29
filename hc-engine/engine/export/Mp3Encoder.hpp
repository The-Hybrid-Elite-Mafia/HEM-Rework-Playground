#pragma once
// =============================================================================
//  WAV -> MP3 with the MP3 encoder that is part of Windows (Media Foundation, Windows 8 and later).
//  Nothing extra to install or license. Input: a 16-bit PCM WAV at 44.1 or 48 kHz, mono or stereo.
//  Constant bitrate; `kbps` is rounded to what the encoder offers (128 / 160 / 192 / 224 / 256 / 320).
//  Any thread (it starts COM for itself). On other systems it returns false with a message.
// =============================================================================
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace hem::pg
{
struct Mp3Encoder
{
    static constexpr int kDefaultKbps = 320;
    static bool encode (const std::filesystem::path& wavIn, const std::filesystem::path& mp3Out, int kbps, std::string& error,
                        const std::function<void (double)>& progress = {}, const std::function<bool()>& shouldCancel = {});

    /** The 16-bit samples handed to the encoder (`channels` of every frame of `in`, which has `fileChannels`).
        0.11.1 and later: exactly the samples of the 16-bit WAV. `legacy` = what 0.10.0 did: read as x / 32768,
        then x 32767 and rounded, which made the MP3 0.0003 dB quieter and moved some samples by 1 step. */
    static void toEncoderSamples (const std::int16_t* in, int frames, int fileChannels, int channels, std::int16_t* out, bool legacy) noexcept;

    /** For the 0.10.0 comparison tool only: encode the old way, to prove the rest of the export is unchanged. */
    static void setLegacySamplesForCheck (bool on) noexcept;
    static bool legacySamplesForCheck() noexcept;
};
} // namespace hem::pg
