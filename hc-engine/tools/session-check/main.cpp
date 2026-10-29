// =============================================================================
//  hem_pg_session_check — opens the songs that 0.10.0 saved (legacy-render --sessions) with the
//  new engine and checks they come out the same:
//    M lines: every stem decoded by the new loader vs 0.10.0 (exact formats must be bit-identical;
//             Ogg / MP3 use other decoders, so the tool measures how far apart they are)
//    E lines: every export rendered again from the 0.10.0 session file; the file bytes must match.
//  Usage: hem_pg_session_check <dir>        Exit code 0 = everything that must match does.
// =============================================================================
#include "export/Exporter.hpp"
#include "BandComposer.hpp"
#include "BandRender.hpp"
#include "AudioFacts.hpp"
#include "hem/pg/Files.hpp"
#include <map>
#include "media/MediaLoader.hpp"
#include "session/SessionFile.hpp"

#include "dr_wav.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace hem::pg;

namespace
{
unsigned long long fnvBytes (const void* data, size_t n, unsigned long long h = 1469598103934665603ull)
{
    auto* b = static_cast<const unsigned char*> (data);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

std::vector<unsigned char> loadFile (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::vector<unsigned char> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::stringstream ss (s);
    std::string part;
    while (std::getline (ss, part, sep)) out.push_back (part);
    return out;
}

double toDb (double v) { return v <= 0.0 ? -999.0 : 20.0 * std::log10 (v); }

/** Interleaved float samples of a WAV (any bit depth), for comparing two exports sample by sample. */
std::vector<float> wavSamples (const std::filesystem::path& p, unsigned& channels)
{
    drwav w;
#if defined(_WIN32)
    if (! drwav_init_file_w (&w, p.c_str(), nullptr)) return {};
#else
    if (! drwav_init_file (&w, p.c_str(), nullptr)) return {};
#endif
    channels = w.channels;
    std::vector<float> v ((size_t) w.totalPCMFrameCount * w.channels);
    drwav_read_pcm_frames_f32 (&w, w.totalPCMFrameCount, v.data());
    drwav_uninit (&w);
    return v;
}
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: hem_pg_session_check <dir>\n"); return 2; }
    const std::filesystem::path dir = SessionFile::toPath (argv[1]);
    std::ifstream list (dir / "sessions.txt");
    if (! list) { std::printf ("ERROR no sessions.txt in %s\n", argv[1]); return 2; }

    // Band Forge: the same songs rendered by the new engine, once per song name
    struct BandJob { const char* name; band::Genre genre; int key; bool minor; double bpm; std::uint32_t seed; double sr; };
    const BandJob bandJobs[] { { "band-bluegrass-48k", band::Genre::bluegrass, 2, false, 0.0, 7, 48000.0 },
                               { "band-trap-44k", band::Genre::trap, 1, true, 140.0, 3, 44100.0 },
                               { "band-gospel-48k", band::Genre::gospel, 5, false, 74.0, 11, 48000.0 } };
    std::map<std::string, std::filesystem::path> bandFolders;
    std::map<std::string, library::AudioFacts> bandFacts;
    auto renderBand = [&] (const std::string& name) -> std::filesystem::path
    {
        if (auto it = bandFolders.find (name); it != bandFolders.end()) return it->second;
        for (auto& bj : bandJobs)
        {
            if (name != bj.name) continue;
            band::Request r;
            r.genre = bj.genre; r.keyRoot = bj.key; r.minor = bj.minor; r.bpm = bj.bpm; r.fullSong = false; r.seed = bj.seed;
            const auto song = band::compose (r);
            std::vector<band::Stem> stems;
            band::renderSong (song, bj.sr, stems);
            const auto folder = dir / ("new-" + name);
            std::error_code ec;
            std::filesystem::remove_all (folder, ec);
            std::string err, info;
            band::writeStems (song, stems, bj.sr, folder, err);
            band::writeMix (stems, bj.sr, band::MixFinish::clean, folder / "mix-clean.wav", info, err);
            band::writeMix (stems, bj.sr, band::MixFinish::mastered, folder / "mix-mastered.wav", info, err);
            AudioBuffer mix (2, stems[0].audio.getNumSamples());
            mix.clear();
            for (auto& st : stems) for (int c = 0; c < 2; ++c) mix.addFrom (c, 0, st.audio, c, 0, mix.getNumSamples(), 0.4f);
            bandFacts[name] = library::measure (mix, bj.sr);
            bandFolders[name] = folder;
            return folder;
        }
        return {};
    };
    int bandSame = 0, bandDiff = 0;

    int exactExportsDiff = 0, mediaSame = 0, mediaExactDiff = 0, mediaLossy = 0, exportsSame = 0, exportsDiff = 0, errors = 0;
    MediaLoader::setTargetRate (48000.0);
    std::string line;
    while (std::getline (list, line))
    {
        if (line.size() < 3) continue;
        const auto f = split (line.substr (2), '|');
        if (line[0] == 'M')
        {
            if (f.size() < 6) { std::printf ("MEDIA %s: 0.10.0 could not read it (%s)\n", f[0].c_str(), f.size() > 1 ? f[1].c_str() : ""); continue; }
            const auto& name = f[0];
            const bool exact = f[1] == "exact";
            std::string err;
            auto d = MediaLoader::decode (dir / "stems" / SessionFile::toPath (name), err);
            if (d == nullptr) { std::printf ("ERROR media %s: %s\n", name.c_str(), err.c_str()); ++errors; continue; }
            unsigned long long h = 1469598103934665603ull;
            for (int c = 0; c < d->getNumChannels(); ++c) h = fnvBytes (d->channel (c), sizeof (float) * (size_t) d->getNumFrames(), h);
            char hs[32];
            std::snprintf (hs, sizeof hs, "%016llx", h);
            const int oldFrames = std::stoi (f[2]), oldCh = std::stoi (f[3]);
            if (hs == f[5] && oldFrames == d->getNumFrames() && oldCh == d->getNumChannels())
            {
                std::printf ("SAME media %s (%d frames, %d ch)\n", name.c_str(), oldFrames, oldCh);
                ++mediaSame;
                continue;
            }
            // measure: line the two decodes up (best offset within +/- 4000 samples), then the largest difference
            const auto raw = loadFile (dir / ("old-decoded-" + name + ".f32"));
            std::vector<float> old (raw.size() / 4);
            if (! raw.empty()) std::memcpy (old.data(), raw.data(), old.size() * 4);
            const int n = jmin (oldFrames, d->getNumFrames());
            const float* now = d->channel (0);
            int bestLag = 0;
            double bestErr = 1.0e300;
            for (int lag = -4000; lag <= 4000; ++lag)
            {
                double e = 0.0;
                for (int i = 6000; i < jmin (n, 30000); i += 3)
                {
                    const int j = i + lag;
                    if (j < 0 || j >= n) { e = 1.0e300; break; }
                    const double x = (double) old[(size_t) i] - (double) now[j];
                    e += x * x;
                }
                if (e < bestErr) { bestErr = e; bestLag = lag; }
            }
            double maxDiff = 0.0, peak = 0.0;
            for (int i = 4000; i < n - 4000; ++i)
            {
                const int j = i + bestLag;
                if (j < 0 || j >= d->getNumFrames()) continue;
                maxDiff = jmax (maxDiff, std::abs ((double) old[(size_t) i] - (double) now[j]));
                peak = jmax (peak, std::abs ((double) old[(size_t) i]));
            }
            std::printf ("%s media %s: 0.10.0 %d frames / new %d frames, start offset %+d samples (%.2f ms), largest difference %.1f dB below the peak\n",
                         exact ? "DIFFERS" : "LOSSY", name.c_str(), oldFrames, d->getNumFrames(), bestLag, bestLag / 48.0, toDb (peak) - toDb (maxDiff));
            if (exact) ++mediaExactDiff; else ++mediaLossy;
        }
        else if (line[0] == 'B')
        {
            if (f.size() < 3) { std::printf ("ERROR band %s: 0.10.0 could not render it\n", f[0].c_str()); ++errors; continue; }
            const auto folder = renderBand (f[0]);
            const auto bytes = loadFile (folder / files::toPath (f[1]));
            char hs[32];
            std::snprintf (hs, sizeof hs, "%016llx", fnvBytes (bytes.data(), bytes.size()));
            if (hs == f[2]) { std::printf ("SAME band %s %s (%zu bytes)\n", f[0].c_str(), f[1].c_str(), bytes.size()); ++bandSame; }
            else
            {
                ++bandDiff;
                if (f[1].ends_with (".wav"))
                {
                    unsigned chA = 0, chB = 0;
                    const auto a = wavSamples (dir / ("old-" + f[0]) / files::toPath (f[1]), chA);
                    const auto b = wavSamples (folder / files::toPath (f[1]), chB);
                    double maxDiff = 0.0, peak = 0.0;
                    for (size_t i = 0; i < jmin (a.size(), b.size()); ++i) { maxDiff = jmax (maxDiff, std::abs ((double) a[i] - (double) b[i])); peak = jmax (peak, std::abs ((double) a[i])); }
                    std::printf ("DIFFERS band %s %s: %zu vs %zu samples, largest difference %.1f dB below the peak\n", f[0].c_str(), f[1].c_str(), a.size(), b.size(), toDb (peak) - toDb (maxDiff));
                }
                else std::printf ("DIFFERS band %s %s: %zu bytes\n", f[0].c_str(), f[1].c_str(), bytes.size());
            }
        }
        else if (line[0] == 'F')
        {
            if (f.size() < 8) continue;
            renderBand (f[0]);
            const auto& nf = bandFacts[f[0]];
            const bool same = std::abs (nf.bpm - std::stod (f[1])) < 1.0e-9 && nf.keyRoot == std::stoi (f[3]) && (nf.minor ? 1 : 0) == std::stoi (f[4])
                              && std::abs (nf.lufs - std::stof (f[6])) < 1.0e-4f && std::abs (nf.peakDb - std::stof (f[7])) < 1.0e-4f;
            std::printf ("%s library facts %s: 0.10.0 %.3f BPM key %s/%s %.2f LUFS, new %.3f BPM key %d/%d %.2f LUFS\n", same ? "SAME" : "DIFFERS", f[0].c_str(),
                         std::stod (f[1]), f[3].c_str(), f[4].c_str(), std::stod (f[6]), nf.bpm, nf.keyRoot, nf.minor ? 1 : 0, (double) nf.lufs);
            if (same) ++bandSame; else ++bandDiff;
        }
        else if (line[0] == 'E')
        {
            if (f.size() < 9) { std::printf ("EXPORT %s %s: 0.10.0 could not export it (%s)\n", f[0].c_str(), f[1].c_str(), f.size() > 2 ? f[2].c_str() : ""); continue; }
            const auto& session = f[0];
            const auto& label = f[1];
            std::string err;
            auto tree = SessionFile::read (dir / (session + ".hemplay"), err);
            if (! tree) { std::printf ("ERROR session %s: %s\n", session.c_str(), err.c_str()); ++errors; continue; }
            std::vector<std::string> failed;
            auto media = MediaLoader::decodeAll (PlaygroundEngine::mediaFilesIn (*tree), failed);
            for (auto& fl : failed) std::printf ("ERROR media in %s: %s\n", session.c_str(), fl.c_str());
            ExportOptions o;
            const int mp3 = std::stoi (f[6]);
            o.file = dir / ("new-" + session + "-" + label + (mp3 > 0 ? ".mp3" : ".wav"));
            o.bitDepth = std::stoi (f[2]);
            o.sampleRate = std::stod (f[3]);
            o.clean = f[4] == "1";
            o.beforeMode = f[5] == "1";
            o.mp3Kbps = mp3;
            o.tailSeconds = 2.0;
            OfflineExport ex (*tree, media, o);
            if (! ex.render ({}, {}, err)) { std::printf ("ERROR export %s %s: %s\n", session.c_str(), label.c_str(), err.c_str()); ++errors; continue; }
            const auto bytes = loadFile (ex.getTargetFile());
            char hs[32];
            std::snprintf (hs, sizeof hs, "%016llx", fnvBytes (bytes.data(), bytes.size()));
            if (hs == f[7] && (long long) bytes.size() == std::stoll (f[8]))
            {
                std::printf ("SAME export %s %s (%zu bytes, byte for byte)\n", session.c_str(), label.c_str(), bytes.size());
                ++exportsSame;
                continue;
            }
            if (o.bitDepth == 16 || mp3 > 0)
            {
                // 16-bit: try the other order of the two dither random numbers (see Exporter.hpp)
                const bool normal = OfflineExport::ditherDrawOrderSecondFirst();
                OfflineExport::setDitherDrawOrderForCheck (! normal);
                OfflineExport ex2 (*tree, media, o);
                std::string err2;
                const bool ok2 = ex2.render ({}, {}, err2);
                OfflineExport::setDitherDrawOrderForCheck (normal);
                const auto bytes2 = ok2 ? loadFile (ex2.getTargetFile()) : std::vector<unsigned char>();
                char hs2[32];
                std::snprintf (hs2, sizeof hs2, "%016llx", fnvBytes (bytes2.data(), bytes2.size()));
                std::printf ("%s export %s %s with the dither numbers drawn the other way round\n", hs2 == f[7] ? "SAME" : "STILL DIFFERS", session.c_str(), label.c_str());
            }
            ++exportsDiff;
            if (session == "song-exact") ++exactExportsDiff;   // only WAV / AIFF / FLAC stems: must be byte for byte
            if (mp3 > 0) { std::printf ("DIFFERS export %s %s: %zu bytes vs %s\n", session.c_str(), label.c_str(), bytes.size(), f[8].c_str()); continue; }
            unsigned chA = 0, chB = 0;
            const auto a = wavSamples (dir / ("old-" + session + "-" + label + ".wav"), chA);
            const auto b = wavSamples (ex.getTargetFile(), chB);
            double maxDiff = 0.0, peak = 0.0;
            size_t firstDiff = (size_t) -1;
            for (size_t i = 0; i < jmin (a.size(), b.size()); ++i)
            {
                const double x = std::abs ((double) a[i] - (double) b[i]);
                if (x > 0.0 && firstDiff == (size_t) -1) firstDiff = i;
                maxDiff = jmax (maxDiff, x);
                peak = jmax (peak, std::abs ((double) a[i]));
            }
            std::printf ("DIFFERS export %s %s: %zu vs %zu samples, first different sample %lld, largest difference %.1f dB below the peak\n",
                         session.c_str(), label.c_str(), a.size(), b.size(), firstDiff == (size_t) -1 ? -1ll : (long long) (firstDiff / jmax (1u, chA)),
                         toDb (peak) - toDb (maxDiff));
        }
    }
    std::printf ("HEM SESSION SUMMARY: media identical %d, exact-format media different %d, lossy media measured %d, exports identical %d, exports different %d, Band Forge files identical %d, different %d, errors %d\n",
                 mediaSame, mediaExactDiff, mediaLossy, exportsSame, exportsDiff, bandSame, bandDiff, errors);
    return (mediaExactDiff == 0 && exactExportsDiff == 0 && bandDiff == 0 && errors == 0) ? 0 : 1;
}
