#include "WavFile.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace hem::pg
{
namespace
{
    void put16 (std::vector<unsigned char>& v, std::uint16_t x) { v.push_back ((unsigned char) (x & 0xff)); v.push_back ((unsigned char) (x >> 8)); }
    void put32 (std::vector<unsigned char>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((unsigned char) ((x >> (8 * i)) & 0xff)); }
    void tag (std::vector<unsigned char>& v, const char* t) { v.insert (v.end(), t, t + 4); }
    constexpr long kHeaderBytes = 12 + 8 + 52 + 8 + 16 + 8;   // RIFF+WAVE, JUNK, fmt, data header

    std::FILE* openFile (const std::filesystem::path& p, const char* mode)
    {
#if defined(_WIN32)
        std::wstring m (mode, mode + std::strlen (mode));
        std::FILE* f = nullptr;
        return _wfopen_s (&f, p.c_str(), m.c_str()) == 0 ? f : nullptr;
#else
        return std::fopen (p.c_str(), mode);
#endif
    }
}

std::string pathText (const std::filesystem::path& p)
{
    const auto u8 = p.u8string();
    return std::string (u8.begin(), u8.end());
}

std::int32_t WavWriter::toInt32 (float s) noexcept
{
    const double samp = s;
    if (samp <= -1.0) return std::numeric_limits<std::int32_t>::min();
    if (samp >= 1.0) return std::numeric_limits<std::int32_t>::max();
    return (std::int32_t) std::nearbyint ((double) std::numeric_limits<std::int32_t>::max() * samp);
}

WavWriter::~WavWriter()
{
    if (fp != nullptr) close();
}

bool WavWriter::open (const std::filesystem::path& file, double sampleRate, int numChannels, int bitDepth, std::string& error)
{
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) { error = "Unsupported bit depth."; return false; }
    if (numChannels < 1 || numChannels > 2) { error = "Only mono or stereo WAV files are written."; return false; }
    fp = openFile (file, "wb");
    if (fp == nullptr) { error = "Cannot write to " + pathText (file); return false; }
    rate = sampleRate;
    chans = numChannels;
    bits = bitDepth;
    frames = 0;
    failed = false;
    if (! writeHeader()) { error = "Cannot write to " + pathText (file); return false; }
    return true;
}

bool WavWriter::write (const float* const* channels, int n)
{
    if (fp == nullptr || failed) return false;
    if (n <= 0) return true;
    const int bytesPer = bits / 8;
    scratch.resize ((std::size_t) n * (std::size_t) chans * (std::size_t) bytesPer);
    unsigned char* d = scratch.data();
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < chans; ++c)
        {
            const float s = channels[c][i];
            if (bits == 32)
            {
                std::uint32_t u;
                std::memcpy (&u, &s, 4);
                for (int k = 0; k < 4; ++k) *d++ = (unsigned char) ((u >> (8 * k)) & 0xff);
            }
            else
            {
                const std::int32_t v = toInt32 (s) >> (bits == 16 ? 16 : 8);   // arithmetic shift: the top bits
                const auto u = (std::uint32_t) v;
                for (int k = 0; k < bytesPer; ++k) *d++ = (unsigned char) ((u >> (8 * k)) & 0xff);
            }
        }
    if (std::fwrite (scratch.data(), 1, scratch.size(), fp) != scratch.size()) { failed = true; return false; }
    frames += (std::uint64_t) n;
    return true;
}

bool WavWriter::writeHeader()
{
    const std::uint64_t bytesPerFrame = (std::uint64_t) chans * (std::uint64_t) bits / 8;
    const std::uint64_t dataSize = bytesPerFrame * frames;
    std::uint64_t riff = 4 + 8 + 40 + 8 + dataSize + (dataSize & 1) + (8 + 28);
    riff += (riff & 1);
    std::vector<unsigned char> h;
    tag (h, "RIFF"); put32 (h, (std::uint32_t) riff); tag (h, "WAVE");
    tag (h, "JUNK"); put32 (h, 52); h.insert (h.end(), 52, 0);
    tag (h, "fmt "); put32 (h, 16);
    put16 (h, (std::uint16_t) (bits == 32 ? 3 : 1));
    put16 (h, (std::uint16_t) chans);
    put32 (h, (std::uint32_t) (int) rate);
    put32 (h, (std::uint32_t) (int) ((double) bytesPerFrame * rate));
    put16 (h, (std::uint16_t) bytesPerFrame);
    put16 (h, (std::uint16_t) bits);
    tag (h, "data"); put32 (h, (std::uint32_t) dataSize);
    if (std::fseek (fp, 0, SEEK_SET) != 0) return false;
    return std::fwrite (h.data(), 1, h.size(), fp) == h.size() && (long) h.size() == kHeaderBytes;
}

bool WavWriter::close()
{
    if (fp == nullptr) return false;
    bool ok = ! failed;
    const std::uint64_t dataSize = (std::uint64_t) chans * (std::uint64_t) bits / 8 * frames;
    if (ok && std::fseek (fp, 0, SEEK_END) == 0 && (dataSize & 1) != 0) ok = std::fputc (0, fp) != EOF;   // pad to an even length
    if (ok) ok = writeHeader();
    if (std::fclose (fp) != 0) ok = false;
    fp = nullptr;
    return ok;
}

//==============================================================================
TempFile::TempFile (const std::filesystem::path& t) : target (t)
{
    static std::atomic<unsigned> counter { 0 };
    const auto stamp = (unsigned long long) std::chrono::steady_clock::now().time_since_epoch().count();
    char tagText[48];
    std::snprintf (tagText, sizeof tagText, "_temp%llx%x", stamp & 0xffffffffull, counter.fetch_add (1));
    auto name = target.stem();
    name += tagText;
    name += target.extension();
    temp = target.parent_path() / name;
}

TempFile::~TempFile() { remove(); }

void TempFile::remove() noexcept
{
    std::error_code ec;
    if (! temp.empty()) std::filesystem::remove (temp, ec);
}

bool TempFile::replaceTarget()
{
    std::error_code ec;
    std::filesystem::rename (temp, target, ec);   // replaces an existing target (Windows: MoveFileEx REPLACE_EXISTING)
    if (ec) return false;
    temp.clear();
    return true;
}
} // namespace hem::pg
