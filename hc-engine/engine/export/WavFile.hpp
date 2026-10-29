#pragma once
// =============================================================================
//  WavFile — writes stereo / mono WAV files the way 0.10.0 did, byte for byte:
//    RIFF, a 52-byte JUNK pad (room for a 64-bit header), a 16-byte fmt chunk, data.
//    16 / 24-bit: each sample -> round(2147483647 x s) (clamped), then the top 16 / 24 bits.
//    32-bit: IEEE float, unchanged.
//  Plus a small temp-file helper so a failed write never destroys the file already there.
//  Worker threads only (it allocates and touches the disk).
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace hem::pg
{
class WavWriter
{
public:
    WavWriter() = default;
    ~WavWriter();
    WavWriter (const WavWriter&) = delete;
    WavWriter& operator= (const WavWriter&) = delete;

    /** bitDepth 16, 24 or 32 (float). False + a reason if the file cannot be made. */
    bool open (const std::filesystem::path& file, double sampleRate, int numChannels, int bitDepth, std::string& error);
    /** Appends `n` frames from `numChannels` channel pointers (each starting at its first frame). */
    bool write (const float* const* channels, int n);
    /** Writes the final header and closes. False if the disk refused. */
    bool close();
    bool isOpen() const noexcept { return fp != nullptr; }

    /** One sample as 0.10.0 turned it into an integer before keeping the top bits. */
    static std::int32_t toInt32 (float s) noexcept;

private:
    bool writeHeader();
    std::FILE* fp = nullptr;
    double rate = 48000.0;
    int chans = 2, bits = 24;
    std::uint64_t frames = 0;
    bool failed = false;
    std::vector<unsigned char> scratch;
};

/** A temporary file next to `target`; replaceTarget() moves it into place, otherwise it is deleted. */
class TempFile
{
public:
    explicit TempFile (const std::filesystem::path& target);
    ~TempFile();
    const std::filesystem::path& path() const noexcept { return temp; }
    bool replaceTarget();
    void remove() noexcept;

private:
    std::filesystem::path target, temp;
};

std::string pathText (const std::filesystem::path&);
} // namespace hem::pg
