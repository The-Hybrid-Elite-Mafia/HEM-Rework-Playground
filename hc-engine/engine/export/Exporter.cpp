#include "Exporter.hpp"
#include "Mp3Encoder.hpp"
#include "WavFile.hpp"

#include <atomic>
#include <cmath>
#include <limits>

namespace hem::pg
{
namespace
{
    /** The 0.10.0 random numbers (the 16-bit dither must be the same noise to give the same file). */
    class LegacyRandom
    {
    public:
        explicit LegacyRandom (int64 s) : seed (s) {}
        int nextInt() noexcept
        {
            seed = (int64) ((((std::uint64_t) seed) * 0x5deece66dULL + 11) & 0xffffffffffffULL);
            return (int) (seed >> 16);
        }
        float nextFloat() noexcept
        {
            const float r = (float) (std::uint32_t) nextInt() / ((float) std::numeric_limits<std::uint32_t>::max() + 1.0f);
            return std::min (r, 1.0f - std::numeric_limits<float>::epsilon());
        }
    private:
        int64 seed;
    };

    // 0.10.0 wrote (rng.nextFloat() - rng.nextFloat()); C++ leaves the order of the two calls to the
    // compiler. Measured on the Windows build (MSVC): the second-written call ran first. GCC / Clang: in order.
#if defined(_MSC_VER)
    constexpr bool kLegacySecondFirst = true;
#else
    constexpr bool kLegacySecondFirst = false;
#endif
    std::atomic<bool> ditherSecondFirst { kLegacySecondFirst };

    std::filesystem::path withExtension (std::filesystem::path p, const char* ext) { p.replace_extension (ext); return p; }
}

void OfflineExport::setDitherDrawOrderForCheck (bool secondFirst) noexcept { ditherSecondFirst.store (secondFirst); }
bool OfflineExport::ditherDrawOrderSecondFirst() noexcept { return ditherSecondFirst.load(); }

OfflineExport::OfflineExport (const Tree& session, const std::map<std::string, SampleData::Ptr>& media, ExportOptions o)
    : options (std::move (o))
{
    engine.prepare (options.sampleRate, kRenderBlock);
    engine.applySessionTree (session, media);
    engine.release();          // not a live device: garbage is freed at once
    engine.collectGarbage();
    engine.prepare (options.sampleRate, kRenderBlock);
    engine.setLoop (false, 0.0, 0.0);
    engine.setStopAtEnd (false);
    engine.setBeforeMode (options.beforeMode);
    engine.setMasterInsertsBypassed (options.clean);
    engine.setPositionSeconds (0.0);
    lengthSamples = (int64) std::ceil ((engine.getSongLengthSeconds() + jmax (0.0, options.tailSeconds)) * options.sampleRate);
    latency = engine.getTotalLatencySamples();
}

std::filesystem::path OfflineExport::getTargetFile() const { return withExtension (options.file, options.mp3Kbps > 0 ? ".mp3" : ".wav"); }

bool OfflineExport::render (const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, std::string& error)
{
    if (options.mp3Kbps <= 0)
        return renderWav (withExtension (options.file, ".wav"), progress, shouldCancel, error);

    // MP3: 16-bit WAV next to the target first (90 % of the bar), then Windows encodes it (last 10 %)
    const double rate = options.sampleRate;
    if (std::abs (rate - 44100.0) > 0.5 && std::abs (rate - 48000.0) > 0.5) { error = "MP3 needs 44.1 or 48 kHz."; return false; }
    const auto mp3 = getTargetFile();
    auto wavName = mp3.stem();
    wavName += " (making mp3).wav";
    const auto wav = mp3.parent_path() / wavName;
    const int bits = options.bitDepth;
    options.bitDepth = 16;
    const bool rendered = renderWav (wav, [&progress] (double p) { if (progress) progress (p * 0.9); }, shouldCancel, error);
    options.bitDepth = bits;
    std::error_code ec;
    if (! rendered) { std::filesystem::remove (wav, ec); return false; }
    const bool ok = Mp3Encoder::encode (wav, mp3, options.mp3Kbps, error,
                                        [&progress] (double p) { if (progress) progress (0.9 + 0.1 * p); }, shouldCancel);
    std::filesystem::remove (wav, ec);
    return ok;
}

bool OfflineExport::renderWav (const std::filesystem::path& target, const std::function<void (double)>& progress, const std::function<bool()>& shouldCancel, std::string& error)
{
    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32) { error = "Unsupported bit depth."; return false; }
    if (engine.getSongLengthSeconds() <= 0.0) { error = "Nothing to export: load some tracks first."; return false; }

    std::error_code ec;
    std::filesystem::create_directories (target.parent_path(), ec);
    if (! target.parent_path().empty() && ! std::filesystem::is_directory (target.parent_path(), ec))
    {
        error = "Cannot create " + pathText (target.parent_path());
        return false;
    }
    TempFile temp (target);
    WavWriter writer;
    if (! writer.open (temp.path(), options.sampleRate, 2, options.bitDepth, error))
    {
        if (error.rfind ("Cannot write", 0) == 0) error = "Cannot write to " + pathText (target);
        return false;
    }

    engine.play();
    std::vector<float> blockL ((size_t) kRenderBlock), blockR ((size_t) kRenderBlock);
    LegacyRandom rng (0x48454D);
    const float lsb = 1.0f / 32768.0f;
    const int64 total = lengthSamples + latency;
    int64 done = 0;
    peak = 0.0f;
    cleanGain = 1.0f;

    auto fail = [&] (const std::string& e) { writer.close(); temp.remove(); error = e; return false; };
    auto write = [&] (float* l, float* r, int count)
    {
        if (options.bitDepth == 16)
        {
            const bool swap = ditherSecondFirst.load();
            for (float* d : { l, r })
                for (int i = 0; i < count; ++i)
                {
                    const float first = rng.nextFloat();
                    const float second = rng.nextFloat();
                    d[i] += (swap ? second - first : first - second) * lsb;
                }
        }
        const float* chans[2] { l, r };
        return writer.write (chans, count);
    };
    auto magnitude = [] (const float* l, const float* r, int count)
    {
        float m = 0.0f;
        for (int i = 0; i < count; ++i) m = jmax (m, std::abs (l[i]), std::abs (r[i]));
        return m;
    };

    // CLEAN: the whole mix is held (float) until its peak is known, then turned down if needed
    std::vector<float> wholeL, wholeR;
    int64 held = 0;
    if (options.clean)
    {
        if (lengthSamples > (int64) std::numeric_limits<int>::max()) return fail ("Song too long for a clean export.");
        try { wholeL.assign ((size_t) lengthSamples, 0.0f); wholeR.assign ((size_t) lengthSamples, 0.0f); }
        catch (const std::bad_alloc&) { return fail ("Not enough memory for a clean export of this song."); }
    }

    while (done < total)
    {
        if (shouldCancel && shouldCancel()) return fail ("Export cancelled.");
        const int n = (int) jmin ((int64) kRenderBlock, total - done);
        float* outs[2] { blockL.data(), blockR.data() };
        engine.process (outs, 2, n);

        const int skip = (int) jlimit ((int64) 0, (int64) n, (int64) latency - done);   // limiter look-ahead
        const int count = (int) jmin ((int64) (n - skip), lengthSamples - held);
        if (count > 0)
        {
            if (options.clean)
            {
                std::copy (blockL.begin() + skip, blockL.begin() + skip + count, wholeL.begin() + held);
                std::copy (blockR.begin() + skip, blockR.begin() + skip + count, wholeR.begin() + held);
            }
            else
            {
                peak = jmax (peak, magnitude (blockL.data() + skip, blockR.data() + skip, count));
                if (! write (blockL.data() + skip, blockR.data() + skip, count)) return fail ("Disk write failed (disk full?).");
            }
            held += count;
        }
        done += n;
        if (progress) progress ((double) done / (double) total * (options.clean ? 0.9 : 1.0));
    }

    if (options.clean)
    {
        const float rawPeak = magnitude (wholeL.data(), wholeR.data(), (int) held);
        const float limit = params::dbToGain (ExportOptions::kCleanPeakDb);
        cleanGain = rawPeak > limit ? limit / rawPeak : 1.0f;
        for (int64 i = 0; i < held; ++i) { wholeL[(size_t) i] *= cleanGain; wholeR[(size_t) i] *= cleanGain; }
        peak = rawPeak * cleanGain;
        for (int64 st = 0; st < held; st += 65536)
        {
            if (shouldCancel && shouldCancel()) return fail ("Export cancelled.");
            const int count = (int) jmin ((int64) 65536, held - st);
            if (! write (wholeL.data() + st, wholeR.data() + st, count)) return fail ("Disk write failed (disk full?).");
        }
        if (progress) progress (1.0);
    }

    if (! writer.close()) return fail ("Disk write failed (disk full?).");
    if (! temp.replaceTarget()) { error = "Cannot replace " + pathText (target) + " (is it open in another program?)"; return false; }
    return true;
}
} // namespace hem::pg
