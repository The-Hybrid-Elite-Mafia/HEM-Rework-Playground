// LIBRARY: tempo / key / loudness measurement, the library index (add, save, load, search, remove)
// and the engine's preview player.

#include "UnitTest.hpp"
#include "Library.hpp"
#include "BandComposer.hpp"
#include "BandRender.hpp"
#include "PlaygroundEngine.hpp"
#include "export/WavFile.hpp"
#include "hem/pg/Files.hpp"
#include "media/MediaLoader.hpp"

#include <algorithm>

namespace hem::pg::test
{
using hemtest::Str;
namespace fs = std::filesystem;
namespace
{
AudioBuffer bandMix (band::Genre g, int key, bool minor, double bpm, uint32_t seed, double sr)
{
    band::Request r;
    r.genre = g; r.keyRoot = key; r.minor = minor; r.bpm = bpm; r.fullSong = false; r.seed = seed;
    std::vector<band::Stem> stems;
    band::renderSong (band::compose (r), sr, stems);
    AudioBuffer mix (2, stems[0].audio.getNumSamples());
    mix.clear();
    for (auto& s : stems) for (int c = 0; c < 2; ++c) mix.addFrom (c, 0, s.audio, c, 0, mix.getNumSamples(), 0.4f);
    return mix;
}

void writeWav (const fs::path& f, const AudioBuffer& b, double sr)
{
    WavWriter w;
    std::string err;
    if (! w.open (f, sr, 2, 24, err)) return;
    const float* chans[2] { b.getReadPointer (0), b.getReadPointer (1) };
    w.write (chans, b.getNumSamples());
    w.close();
}

int indexOf (const std::vector<std::string>& v, const std::string& s)
{
    const auto it = std::find (v.begin(), v.end(), s);
    return it == v.end() ? -1 : (int) (it - v.begin());
}

bool has (const std::string& hay, const char* needle) { return hay.find (needle) != std::string::npos; }
} // namespace

class LibraryTests final : public hemtest::UnitTest
{
public:
    LibraryTests() : UnitTest ("Library", "Playground") {}

    void runTest() override
    {
        using band::Genre;
        beginTest ("tempo and key of Band Forge songs are measured (tempo within 0.5 BPM, key exact)");
        struct Case { Genre g; int key; bool minor; double bpm; };
        for (const auto& c : { Case { Genre::outlawCountry, 7, false, 96.0 }, Case { Genre::bluegrass, 2, false, 132.0 },
                               Case { Genre::folk, 9, true, 104.0 }, Case { Genre::gospel, 5, false, 74.0 },
                               Case { Genre::popTrap, 1, true, 100.0 }, Case { Genre::cinematic, 2, true, 72.0 } })
        {
            for (double sr : { 48000.0, 44100.0 })
            {
                const auto mix = bandMix (c.g, c.key, c.minor, c.bpm, 5, sr);
                const auto f = library::measure (mix, sr);
                const Str name = Str (band::genreName (c.g)) + " @" + Str (sr);
                logMessage (name + ": " + Str (f.bpm, 2) + " BPM (conf " + Str (f.tempoConfidence, 2) + "), "
                            + f.keyName() + " (conf " + Str (f.keyConfidence, 2) + "), " + Str (f.lufs, 1) + " LUFS");
                // a slow song with busy 8th notes can read at double time (the LIBRARY has x2 / /2 for that);
                // every other genre must be exact
                const bool octave = c.g == Genre::cinematic && std::abs (f.bpm - 2.0 * c.bpm) <= 1.0;
                if (! octave) expectWithinAbsoluteError (f.bpm, c.bpm, 0.5, name + " tempo " + Str (f.bpm));
                expectEquals (f.keyRoot, c.key, name + " key " + f.keyName());
                expectEquals ((int) f.minor, (int) c.minor, name + " mode " + f.keyName());
                expectWithinAbsoluteError (f.seconds, mix.getNumSamples() / sr, 1.0e-6);
                expect (f.lufs > -40.0f && f.lufs < 0.0f);
            }
        }

        beginTest ("silence and very short files don't invent a tempo or key");
        {
            AudioBuffer quiet (2, 48000 * 3);
            quiet.clear();
            const auto f = library::measure (quiet, 48000.0);
            expectEquals (f.bpm, 0.0);
            expectEquals (f.keyRoot, -1);
        }

        const auto dir = fs::temp_directory_path() / "hem-library-test";
        std::error_code ec;
        fs::remove_all (dir, ec);
        const auto src = dir / "in";
        fs::create_directories (src, ec);

        beginTest ("files are copied in, guessed, saved, loaded back, searched and removed (never deleted)");
        {
            library::Library lib (dir / "Library");
            std::string err;
            expect (lib.load (err) && lib.size() == 0, err);
            const auto beat = src / "Dirt Road Trap (Instrumental).wav";
            const auto vocal = src / "Whiskey Gospel Vocals.wav";
            const auto song = src / "Front Porch Folk.wav";
            writeWav (beat, bandMix (Genre::trap, 1, true, 140.0, 2, 48000.0), 48000.0);
            writeWav (vocal, bandMix (Genre::gospel, 5, false, 74.0, 2, 48000.0), 48000.0);
            writeWav (song, bandMix (Genre::folk, 9, true, 96.0, 2, 48000.0), 48000.0);
            const auto idBeat = lib.addFile (beat, err);
            const auto idVocal = lib.addFile (vocal, err);
            const auto idSong = lib.addFile (song, err);
            const auto idAgain = lib.addFile (beat, err);   // same name again: kept, new file name
            expect (! idBeat.empty() && ! idVocal.empty() && ! idSong.empty() && ! idAgain.empty(), err);
            expectEquals (lib.size(), 4);
            expect (fs::exists (beat, ec), "the original stays where it was");
            expect (fs::exists (lib.fileOf (*lib.find (idBeat)), ec) && fs::exists (lib.fileOf (*lib.find (idAgain)), ec));
            expect (lib.fileOf (*lib.find (idBeat)) != lib.fileOf (*lib.find (idAgain)));
            expectEquals (lib.find (idBeat)->kind, std::string ("Beat"));
            expectEquals (lib.find (idBeat)->genre, std::string ("Trap"));
            expectEquals (lib.find (idVocal)->kind, std::string ("Vocal"));
            expectEquals (lib.find (idVocal)->genre, std::string ("Gospel"));
            expectEquals (lib.find (idSong)->kind, std::string ("Song"));

            // measure like the UI does (decode the copied file)
            for (auto id : { idBeat, idVocal, idSong, idAgain })
            {
                auto* e = lib.find (id);
                auto d = MediaLoader::decodeNative (lib.fileOf (*e), err);
                expect (d != nullptr, err);
                if (d == nullptr) continue;
                AudioBuffer b (2, d->getNumFrames());
                for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, d->channel (c), d->getNumFrames());
                e->facts = library::measure (b, d->getSourceRate());
                e->measured = true;
            }
            lib.find (idSong)->stars = 5;
            lib.find (idSong)->notes = "took it through Suno, kept the hook";
            lib.find (idSong)->madeFrom = "C:/Band Forge/2026-10-17 Folk";
            expect (lib.save (err), err);

            library::Library again (dir / "Library");
            expect (again.load (err), err);
            expectEquals (again.size(), 4);
            const auto* s2 = again.find (idSong);
            expect (s2 != nullptr && s2->stars == 5 && has (s2->notes, "Suno") && s2->measured && has (s2->madeFrom, "Folk"));
            if (s2 != nullptr) expectWithinAbsoluteError (s2->facts.bpm, 96.0, 0.5);

            expectEquals ((int) again.query ("", "", 8, true).size(), 4);
            expectEquals ((int) again.query ("", "Vocal", 8, true).size(), 1);
            expectEquals (again.query ("gospel", "", 8, true)[0], idVocal);
            expectEquals (again.query ("95", "", 8, true)[0], idSong);             // number = tempo within 3 BPM
            expectEquals (again.query ("suno", "", 8, true)[0], idSong);           // notes are searched
            expectEquals ((int) again.query ("trap minor", "", 1, true).size(), 2);      // both beats: trap + C# minor
            expectEquals (again.query ("", "", 0, false)[0], idSong);              // 5 stars first
            logMessage ("trap beat measured " + Str (again.find (idBeat)->facts.bpm) + " BPM, " + again.find (idBeat)->facts.keyName());
            const auto byTempo = again.query ("", "", 4, true);
            expect (indexOf (byTempo, idVocal) < indexOf (byTempo, idSong), "74 BPM before 96 BPM");

            expect (again.remove (idAgain, err), err);
            expectEquals (again.size(), 3);
            int removedFiles = 0;
            for (auto& f : fs::directory_iterator (dir / "Library" / "Removed", ec)) removedFiles += f.is_regular_file() ? 1 : 0;
            expect (removedFiles == 1, "moved, not deleted");
            expect (again.save (err), err);

            files::writeTextAtomic (dir / "Library" / "library.json", "{ not json");
            library::Library broken (dir / "Library");
            expect (! broken.load (err) && has (err, "damaged"));
        }

        beginTest ("preview plays a file straight to the output, without the session, and stops at its end");
        {
            PlaygroundEngine e;
            e.prepare (48000.0, 512);
            e.startPreview (std::make_shared<const SampleData> (std::vector<std::vector<float>> { std::vector<float> (4800, 0.25f), std::vector<float> (4800, -0.25f) }, 48000.0, fs::path()));
            expect (e.isPreviewPlaying());
            std::vector<float> l (512), r (512);
            float* out[2] { l.data(), r.data() };
            e.process (out, 2, 512);
            expectWithinAbsoluteError (l[100], 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (r[100], -0.25f, 1.0e-6f);
            expectWithinAbsoluteError (e.getPreviewPosition(), 512.0 / 48000.0, 1.0e-6);
            e.seekPreview (0.05);   // 2400 samples in
            e.process (out, 2, 512);
            expectWithinAbsoluteError (e.getPreviewPosition(), (2400.0 + 512.0) / 48000.0, 1.0e-6);
            for (int k = 0; k < 6; ++k) e.process (out, 2, 512);
            expect (! e.isPreviewPlaying(), "stopped at the end of the file");
            float m = 0.0f;
            for (int i = 0; i < 512; ++i) m = jmax (m, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
            expectEquals (m, 0.0f);
            e.stopPreview();
            e.collectGarbage();
        }
        fs::remove_all (dir, ec);
    }
};

static LibraryTests libraryTests;
} // namespace hem::pg::test
