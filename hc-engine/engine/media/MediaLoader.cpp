#include "MediaLoader.hpp"
#include "SincResampler.hpp"

#include "dr_wav.h"
#include "dr_flac.h"
#include "dr_mp3.h"

extern "C" {
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <set>

namespace hem::pg
{
namespace
{
    std::atomic<double> targetRate { 0.0 };
    std::atomic<int> mp3Start { (int) MediaLoader::Mp3Start::legacy };

    std::string lower (std::string s)
    {
        for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
        return s;
    }

    std::string displayName (const std::filesystem::path& p)
    {
        const auto u8 = p.filename().u8string();
        return std::string (u8.begin(), u8.end());
    }

    std::string displayPath (const std::filesystem::path& p)
    {
        const auto u8 = p.u8string();
        return std::string (u8.begin(), u8.end());
    }

    /** Interleaved float frames + format facts, straight from a decoder. */
    struct Decoded
    {
        std::vector<float> interleaved;
        unsigned channels = 0;
        double rate = 0.0;
        std::uint64_t frames = 0;
    };

    enum class Result { ok, notAudio, tooLong, noMemory, readFailed };

    std::uint64_t maxFramesFor (double rate) { return (std::uint64_t) (MediaLoader::kMaxMinutes * 60.0 * rate); }

    bool reserveFrames (Decoded& d, std::uint64_t frames)
    {
        try { d.interleaved.resize ((std::size_t) (frames * d.channels)); }
        catch (const std::bad_alloc&) { return false; }
        catch (const std::length_error&) { return false; }
        return true;
    }

    //================================================================================ WAV / AIFF
    Result readWav (const std::filesystem::path& file, Decoded& d)
    {
        drwav wav;
#if defined(_WIN32)
        if (! drwav_init_file_w (&wav, file.c_str(), nullptr)) return Result::notAudio;
#else
        if (! drwav_init_file (&wav, file.c_str(), nullptr)) return Result::notAudio;
#endif
        struct Closer { drwav* w; ~Closer() { drwav_uninit (w); } } closer { &wav };
        d.channels = wav.channels;
        d.rate = (double) wav.sampleRate;
        d.frames = wav.totalPCMFrameCount;
        if (d.channels == 0 || d.rate <= 0.0) return Result::notAudio;
        if (d.frames == 0 || d.frames > maxFramesFor (d.rate)) return Result::tooLong;
        if (! reserveFrames (d, d.frames)) return Result::noMemory;
        const auto got = drwav_read_pcm_frames_f32 (&wav, d.frames, d.interleaved.data());
        if (got != d.frames) return Result::readFailed;
        return Result::ok;
    }

    //================================================================================ FLAC
    Result readFlac (const std::filesystem::path& file, Decoded& d)
    {
#if defined(_WIN32)
        drflac* flac = drflac_open_file_w (file.c_str(), nullptr);
#else
        drflac* flac = drflac_open_file (file.c_str(), nullptr);
#endif
        if (flac == nullptr) return Result::notAudio;
        struct Closer { drflac* f; ~Closer() { drflac_close (f); } } closer { flac };
        d.channels = flac->channels;
        d.rate = (double) flac->sampleRate;
        d.frames = flac->totalPCMFrameCount;
        if (d.channels == 0 || d.rate <= 0.0) return Result::notAudio;
        if (d.frames == 0 || d.frames > maxFramesFor (d.rate)) return Result::tooLong;
        if (! reserveFrames (d, d.frames)) return Result::noMemory;
        const auto got = drflac_read_pcm_frames_f32 (flac, d.frames, d.interleaved.data());
        if (got != d.frames) return Result::readFailed;
        return Result::ok;
    }

    //================================================================================ MP3
    Result readMp3 (const std::filesystem::path& file, Decoded& d)
    {
        drmp3 mp3;
#if defined(_WIN32)
        if (! drmp3_init_file_w (&mp3, file.c_str(), nullptr)) return Result::notAudio;
#else
        if (! drmp3_init_file (&mp3, file.c_str(), nullptr)) return Result::notAudio;
#endif
        struct Closer { drmp3* m; ~Closer() { drmp3_uninit (m); } } closer { &mp3 };
        if (MediaLoader::getMp3Start() == MediaLoader::Mp3Start::legacy)
        {
            mp3.delayInPCMFrames = 0;
            mp3.paddingInPCMFrames = 0;
        }
        d.channels = mp3.channels;
        d.rate = (double) mp3.sampleRate;
        if (d.channels == 0 || d.rate <= 0.0) return Result::notAudio;
        const auto limit = maxFramesFor (d.rate);
        // read in pieces: the frame count of an MP3 is only known after decoding it
        constexpr std::uint64_t kChunk = 65536;
        std::vector<float> chunk ((std::size_t) (kChunk * d.channels));
        for (;;)
        {
            const auto got = drmp3_read_pcm_frames_f32 (&mp3, kChunk, chunk.data());
            if (got == 0) break;
            if (d.frames + got > limit) return Result::tooLong;
            try { d.interleaved.insert (d.interleaved.end(), chunk.begin(), chunk.begin() + (std::ptrdiff_t) (got * d.channels)); }
            catch (const std::bad_alloc&) { return Result::noMemory; }
            d.frames += got;
        }
        if (d.frames == 0) return Result::tooLong;
        return Result::ok;
    }

    //================================================================================ Ogg Vorbis
    Result readOgg (const std::filesystem::path& file, Decoded& d)
    {
#if defined(_WIN32)
        FILE* f = _wfopen (file.c_str(), L"rb");
#else
        FILE* f = std::fopen (file.c_str(), "rb");
#endif
        if (f == nullptr) return Result::notAudio;
        int err = 0;
        stb_vorbis* v = stb_vorbis_open_file (f, 1, &err, nullptr);   // closes f when freed
        if (v == nullptr) return Result::notAudio;   // stb_vorbis has closed the file already
        struct Closer { stb_vorbis* v; ~Closer() { stb_vorbis_close (v); } } closer { v };
        const auto info = stb_vorbis_get_info (v);
        d.channels = (unsigned) info.channels;
        d.rate = (double) info.sample_rate;
        if (d.channels == 0 || d.rate <= 0.0) return Result::notAudio;
        const auto total = (std::uint64_t) stb_vorbis_stream_length_in_samples (v);
        if (total == 0 || total > maxFramesFor (d.rate)) return Result::tooLong;
        if (! reserveFrames (d, total)) return Result::noMemory;
        std::uint64_t done = 0;
        while (done < total)
        {
            const int want = (int) std::min<std::uint64_t> (65536, total - done);
            const int got = stb_vorbis_get_samples_float_interleaved (v, (int) d.channels, d.interleaved.data() + done * d.channels, want * (int) d.channels);
            if (got <= 0) break;
            done += (std::uint64_t) got;
        }
        if (done == 0) return Result::readFailed;
        d.frames = done;
        d.interleaved.resize ((std::size_t) (done * d.channels));
        return Result::ok;
    }

    const std::set<std::string>& extensions()
    {
        static const std::set<std::string> e { ".wav", ".bwf", ".aiff", ".aif", ".flac", ".ogg", ".mp3" };
        return e;
    }
}

void MediaLoader::setTargetRate (double rate) noexcept { targetRate.store (rate); }
double MediaLoader::getTargetRate() noexcept { return targetRate.load(); }
void MediaLoader::setMp3Start (Mp3Start s) noexcept { mp3Start.store ((int) s); }
MediaLoader::Mp3Start MediaLoader::getMp3Start() noexcept { return (Mp3Start) mp3Start.load(); }

std::string MediaLoader::wildcard() { return "*.wav;*.bwf;*.aiff;*.aif;*.flac;*.ogg;*.mp3"; }

bool MediaLoader::isAudioFile (const std::filesystem::path& f)
{
    const auto u8 = f.extension().u8string();
    return extensions().count (lower (std::string (u8.begin(), u8.end()))) > 0;
}

SampleData::Ptr MediaLoader::decode (const std::filesystem::path& file, std::string& error)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (file, ec))
    {
        error = "File not found: " + displayPath (file);
        return nullptr;
    }

    const auto extU8 = file.extension().u8string();
    const auto ext = lower (std::string (extU8.begin(), extU8.end()));
    Decoded d;
    Result r = Result::notAudio;
    // Try the format the name says first, then the others (a misnamed file still opens).
    using Reader = Result (*) (const std::filesystem::path&, Decoded&);
    std::vector<Reader> order;
    if (ext == ".wav" || ext == ".bwf" || ext == ".aif" || ext == ".aiff") order = { readWav, readFlac, readOgg, readMp3 };
    else if (ext == ".flac") order = { readFlac, readWav, readOgg, readMp3 };
    else if (ext == ".ogg")  order = { readOgg, readWav, readFlac, readMp3 };
    else if (ext == ".mp3")  order = { readMp3, readWav, readFlac, readOgg };
    else order = { readWav, readFlac, readOgg };   // unknown names: never guess MP3 (it decodes almost any bytes)
    for (auto* reader : order)
    {
        d = {};
        r = reader (file, d);
        if (r != Result::notAudio) break;
    }

    switch (r)
    {
        case Result::ok: break;
        case Result::notAudio: error = "Not an audio file this app can read: " + displayName (file); return nullptr;
        case Result::tooLong: error = displayName (file) + " is empty or longer than " + std::to_string ((int) kMaxMinutes) + " minutes."; return nullptr;
        case Result::noMemory: error = "Not enough memory for " + displayName (file); return nullptr;
        case Result::readFailed: error = "Could not read " + displayName (file); return nullptr;
    }
    if (d.rate < 8000.0 || d.rate > 384000.0)
    {
        error = "Unsupported sample rate in " + displayName (file);
        return nullptr;
    }
    if (d.frames > (std::uint64_t) std::numeric_limits<int>::max())
    {
        error = displayName (file) + " is empty or longer than " + std::to_string ((int) kMaxMinutes) + " minutes.";
        return nullptr;
    }

    // first two channels, like 0.10.0 (left, and right if the file has one)
    const int channels = d.channels > 1 ? 2 : 1;
    const auto frames = (std::size_t) d.frames;
    std::vector<std::vector<float>> audio;
    try
    {
        audio.resize ((std::size_t) channels);
        for (int c = 0; c < channels; ++c)
        {
            audio[(std::size_t) c].resize (frames);
            float* dst = audio[(std::size_t) c].data();
            const float* src = d.interleaved.data() + c;
            for (std::size_t i = 0; i < frames; ++i) dst[i] = src[i * d.channels];
        }
    }
    catch (const std::bad_alloc&) { error = "Not enough memory for " + displayName (file); return nullptr; }
    d.interleaved = {};

    const double want = targetRate.load();
    const auto inRate = (std::uint32_t) std::llround (d.rate);
    const auto outRate = (std::uint32_t) std::llround (want);
    if (want >= 8000.0 && inRate != outRate && std::abs (d.rate - (double) inRate) < 1.0e-6)
    {
        const SincResampler rs (inRate, outRate);
        const auto outFrames = rs.outputLength ((std::uint64_t) frames);
        if (outFrames > 0 && outFrames < (std::uint64_t) std::numeric_limits<int>::max())
        {
            std::vector<std::vector<float>> converted;
            try
            {
                converted.resize ((std::size_t) channels);
                for (int c = 0; c < channels; ++c)
                {
                    converted[(std::size_t) c].resize ((std::size_t) outFrames);
                    rs.process (audio[(std::size_t) c].data(), (std::uint64_t) frames, converted[(std::size_t) c].data());
                }
            }
            catch (const std::bad_alloc&) { error = "Not enough memory for " + displayName (file); return nullptr; }
            return std::make_shared<const SampleData> (std::move (converted), (double) outRate, file);
        }
    }
    return std::make_shared<const SampleData> (std::move (audio), d.rate, file);
}

std::map<std::string, SampleData::Ptr> MediaLoader::decodeAll (const std::vector<std::string>& paths, std::vector<std::string>& failed,
                                                               const std::function<bool()>& shouldCancel)
{
    std::map<std::string, SampleData::Ptr> out;
    for (const auto& p : paths)
    {
        if (shouldCancel && shouldCancel()) break;
        if (p.empty() || out.count (p) > 0) continue;
        std::string err;
        const std::filesystem::path path (std::u8string (p.begin(), p.end()));
        if (auto s = decode (path, err)) out[p] = s;
        else failed.push_back (err);
    }
    return out;
}
} // namespace hem::pg
