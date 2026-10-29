#pragma once
// =============================================================================
//  WAV -> MP3 with the MP3 encoder that is part of Windows (Media Foundation, Windows 8 and later).
//  Nothing extra to install or license. Input: a 16-bit PCM WAV at 44.1 or 48 kHz, mono or stereo.
//  Constant bitrate; `kbps` is rounded to what the encoder offers (128 / 160 / 192 / 224 / 256 / 320).
//  Any thread (it starts COM for itself). On other systems it returns false with a message.
// =============================================================================
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
};
} // namespace hem::pg
