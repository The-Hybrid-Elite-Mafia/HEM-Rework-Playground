// BAND FORGE: composer rules, instrument tuning / decay, full-song rendering and file output.

#include "UnitTest.hpp"
#include "BandComposer.hpp"
#include "BandRender.hpp"
#include "hem/pg/Files.hpp"

#include "dr_wav.h"

#include <cmath>
#include <fstream>
#include <random>
#include <set>

namespace hem::pg::test
{
using hemtest::Str;
namespace
{
using namespace hem::pg::band;
constexpr double kSr = 48000.0;

/** Pitch (Hz) of a stereo buffer between t0 and t1 by normalised autocorrelation, searched near `expectHz`. */
double estimateHz (const AudioBuffer& b, double t0, double t1, double expectHz)
{
    const int s0 = (int) (t0 * kSr), n = (int) ((t1 - t0) * kSr);
    std::vector<double> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = b.getSample (0, s0 + i) + b.getSample (1, s0 + i);
    const int lagLo = (int) (kSr / (expectHz * 1.12)), lagHi = (int) (kSr / (expectHz / 1.12)) + 1;
    auto corr = [&] (int lag)
    {
        double num = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { num += x[(size_t) i] * x[(size_t) (i + lag)]; e1 += x[(size_t) i] * x[(size_t) i]; e2 += x[(size_t) (i + lag)] * x[(size_t) (i + lag)]; }
        return num / std::sqrt (e1 * e2 + 1e-20);
    };
    int best = lagLo; double bestC = -2;
    for (int lag = lagLo; lag <= lagHi; ++lag) { const double c = corr (lag); if (c > bestC) { bestC = c; best = lag; } }
    const double cm = corr (best - 1), c0 = bestC, cp = corr (best + 1);
    const double denom = cm - 2 * c0 + cp;
    const double frac = std::abs (denom) > 1e-12 ? 0.5 * (cm - cp) / denom : 0.0;
    return kSr / (best + frac);
}

double cents (double hz, double ref) { return 1200.0 * std::log2 (hz / ref); }

double rmsDb (const AudioBuffer& b, double t0, double t1)
{
    const int s0 = (int) (t0 * kSr), n = (int) ((t1 - t0) * kSr);
    double sum = 0;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) sum += (double) b.getSample (ch, s0 + i) * b.getSample (ch, s0 + i);
    return 10.0 * std::log10 (sum / (2.0 * n) + 1e-20);
}

bool allFinite (const AudioBuffer& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}
} // namespace

class BandComposerTests final : public hemtest::UnitTest
{
public:
    BandComposerTests() : UnitTest ("Band Forge composer", "Playground") {}

    void runTest() override
    {
        beginTest ("every genre composes a full song with its default band, notes inside the song and in range");
        for (int gi = 0; gi < kNumGenres; ++gi)
        {
            Request r;
            r.genre = (Genre) gi;
            r.minor = r.genre == Genre::trap || r.genre == Genre::cinematic;
            r.seed = 11;
            const auto song = compose (r);
            expect (song.totalBars >= 40, Str (genreName (r.genre)) + " bars " + Str (song.totalBars));
            expectEquals ((int) song.parts.size(), (int) defaultInstruments (r.genre).size());
            for (auto& p : song.parts)
            {
                expect (! p.notes.empty(), Str (genreName (r.genre)) + ": " + instrumentName (p.instrument) + " has no notes");
                for (auto& n : p.notes)
                {
                    expect (n.start >= 0.0 && n.start < song.lengthBeats() + 1.0);
                    expect (n.length > 0.0 && n.velocity > 0.0f && n.velocity <= 1.0f);
                    expect (n.pitch >= 20 && n.pitch <= 100, Str (instrumentName (p.instrument)) + " pitch " + Str (n.pitch));
                }
            }
        }

        beginTest ("same seed = same song, different seed = different song");
        {
            Request r; r.genre = Genre::bluegrass; r.seed = 42;
            const auto a = compose (r), b = compose (r);
            r.seed = 43;
            const auto c = compose (r);
            bool same = a.parts.size() == b.parts.size(), differs = false;
            for (size_t i = 0; i < a.parts.size() && same; ++i)
            {
                same &= a.parts[i].notes.size() == b.parts[i].notes.size();
                for (size_t k = 0; same && k < a.parts[i].notes.size(); ++k)
                    same &= a.parts[i].notes[k].pitch == b.parts[i].notes[k].pitch && a.parts[i].notes[k].start == b.parts[i].notes[k].start;
            }
            for (size_t i = 0; i < a.parts.size() && i < c.parts.size(); ++i)
                differs |= a.parts[i].notes.size() != c.parts[i].notes.size()
                           || (! a.parts[i].notes.empty() && ! c.parts[i].notes.empty() && a.parts[i].notes[3 % a.parts[i].notes.size()].pitch != c.parts[i].notes[3 % c.parts[i].notes.size()].pitch);
            expect (same, "deterministic");
            expect (differs, "a new seed changes something");
        }

        beginTest ("country rhythm guitar plays the chords of the key (no wrong notes)");
        {
            Request r; r.genre = Genre::outlawCountry; r.keyRoot = 7; r.seed = 3;   // G major
            r.instruments = { Instrument::acousticGuitar, Instrument::bass, Instrument::banjo, Instrument::mandolin };
            const auto song = compose (r);
            for (auto& p : song.parts)
            {
                int outside = 0;
                for (auto& n : p.notes) if (! theory::inScale (7, false, n.pitch % 12)) ++outside;
                // basses may walk chromatically into a chord: allow a few
                const int allowed = p.instrument == Instrument::bass ? (int) p.notes.size() / 6 : 0;
                expect (outside <= allowed, Str (instrumentName (p.instrument)) + " notes outside G major: " + Str (outside));
            }
        }

        beginTest ("blues is a 12-bar form with a shuffle; trap follows the kick with the 808");
        {
            Request r; r.genre = Genre::blues; r.seed = 9;
            const auto blues = compose (r);
            expectEquals (blues.sections[1].bars, 12);
            expectEquals (blues.swing, 1.0);
            Request t; t.genre = Genre::trap; t.minor = true; t.seed = 9;
            const auto trap = compose (t);
            const Part* drums = nullptr; const Part* b808 = nullptr;
            for (auto& p : trap.parts) { if (p.instrument == Instrument::trapDrums) drums = &p; if (p.instrument == Instrument::bass808) b808 = &p; }
            expect (drums != nullptr && b808 != nullptr);
            if (drums && b808)
            {
                std::set<long> kicks;
                for (auto& n : drums->notes) if (n.pitch == drum::kick) kicks.insert (std::lround (n.start * 100));
                int matched = 0;
                for (auto& n : b808->notes) if (kicks.count (std::lround (n.start * 100)) > 0) ++matched;
                expect (matched >= (int) b808->notes.size() * 9 / 10, "808 on the kicks: " + Str (matched) + "/" + Str ((int) b808->notes.size()));
            }
        }

        beginTest ("short song is about a minute; custom tempo is used");
        {
            Request r; r.genre = Genre::folk; r.fullSong = false; r.bpm = 120.0; r.seed = 1;
            const auto s = compose (r);
            expectEquals (s.bpm, 120.0);
            expect (s.lengthSeconds (0.0) > 30.0 && s.lengthSeconds (0.0) < 80.0, Str (s.lengthSeconds (0.0)));
        }
    }
};

class BandInstrumentTests final : public hemtest::UnitTest
{
public:
    BandInstrumentTests() : UnitTest ("Band Forge instruments", "Playground") {}

    void runTest() override
    {
        beginTest ("plucked strings are in tune (within 4 cents) across the range");
        for (auto inst : { Instrument::acousticGuitar, Instrument::electricGuitar, Instrument::banjo, Instrument::bass, Instrument::uprightBass, Instrument::piano })
            for (int pitch : { 40, 52, 57, 64, 69, 76 })
            {
                if ((inst == Instrument::bass || inst == Instrument::uprightBass) && pitch > 57) continue;
                if (inst == Instrument::banjo && pitch < 52) continue;
                const auto b = renderSingleNote (inst, pitch, 0.8f, 1.0, kSr);
                const double ref = midiToHz (pitch);
                const double hz = estimateHz (b, 0.1, 0.5, ref);
                expect (std::abs (cents (hz, ref)) < 4.0, Str (instrumentName (inst)) + " note " + Str (pitch) + ": " + Str (cents (hz, ref), 2) + " cents");
                expect (allFinite (b));
            }

        beginTest ("fiddle and harmonica are in tune once the attack / bend has settled (within 12 cents, vibrato averaged)");
        for (auto inst : { Instrument::fiddle, Instrument::harmonica })
            for (int pitch : { 62, 69, 76 })
            {
                const auto b = renderSingleNote (inst, pitch, 0.8f, 1.5, kSr);
                const double ref = midiToHz (pitch);
                const double hz = estimateHz (b, 0.5, 1.4, ref);
                expect (std::abs (cents (hz, ref)) < 12.0, Str (instrumentName (inst)) + " " + Str (pitch) + ": " + Str (cents (hz, ref), 2) + " cents");
            }

        beginTest ("banjo dies away much faster than guitar; muted notes stop quickly");
        {
            const auto gtr = renderSingleNote (Instrument::acousticGuitar, 55, 0.8f, 3.0, kSr);
            const auto bjo = renderSingleNote (Instrument::banjo, 55, 0.8f, 3.0, kSr);
            const double gDrop = rmsDb (gtr, 0.05, 0.15) - rmsDb (gtr, 1.5, 1.7);
            const double bDrop = rmsDb (bjo, 0.05, 0.15) - rmsDb (bjo, 1.5, 1.7);
            expect (bDrop > gDrop + 10.0, "banjo drop " + Str (bDrop, 1) + " dB, guitar " + Str (gDrop, 1) + " dB");
            const auto muted = renderSingleNote (Instrument::electricGuitar, 52, 0.8f, 0.2, kSr, art::muted);
            expect (rmsDb (muted, 0.6, 0.8) < rmsDb (muted, 0.0, 0.1) - 50.0, "muted note rings on");
        }

        beginTest ("a harmonica bend starts flat and rises to the note");
        {
            const auto b = renderSingleNote (Instrument::harmonica, 69, 0.8f, 1.0, kSr, art::bend);
            const double early = estimateHz (b, 0.02, 0.07, midiToHz (68.3));
            expect (cents (early, midiToHz (69)) < -30.0, "early " + Str (cents (early, midiToHz (69)), 1) + " cents");
        }

        beginTest ("every instrument and drum renders finite, non-silent audio");
        for (int i = 0; i < kNumInstruments; ++i)
        {
            const auto inst = (Instrument) i;
            const int pitch = inst == Instrument::drums || inst == Instrument::trapDrums ? drum::snare : (inst == Instrument::bass808 ? 36 : 60);
            const auto b = renderSingleNote (inst, pitch, 0.9f, 0.5, kSr);
            expect (allFinite (b), instrumentName (inst));
            expect (b.getMagnitude (0, b.getNumSamples()) > 1.0e-3f, Str (instrumentName (inst)) + " is silent");
        }
        for (int p : { drum::kick, drum::snare, drum::clap, drum::rim, drum::brush, drum::hatClosed, drum::hatOpen, drum::ride, drum::crash, drum::tomLow, drum::tomMid, drum::tomHigh, drum::tamb, drum::shaker })
        {
            const auto b = renderSingleNote (Instrument::drums, p, 0.9f, 0.3, kSr);
            expect (allFinite (b) && b.getMagnitude (0, b.getNumSamples()) > 1.0e-3f, "drum " + Str (p));
        }
    }
};

class BandRenderTests final : public hemtest::UnitTest
{
public:
    BandRenderTests() : UnitTest ("Band Forge render", "Playground") {}

    void runTest() override
    {
        beginTest ("a short bluegrass song renders levelled stems (target loudness, peaks under -1 dBFS) and writes WAV + MIDI");
        {
            Request r; r.genre = Genre::bluegrass; r.fullSong = false; r.seed = 7;
            const auto song = compose (r);
            std::vector<Stem> stems;
            float lastProgress = 0.0f;
            expect (renderSong (song, kSr, stems, [&] (float p) { lastProgress = p; }));
            expectEquals ((int) stems.size(), (int) song.parts.size());
            expectWithinAbsoluteError (lastProgress, 1.0f, 1.0e-6f);
            for (auto& s : stems)
            {
                expect (allFinite (s.audio));
                const float peak = s.audio.getMagnitude (0, s.audio.getNumSamples());
                expect (peak <= std::pow (10.0f, -1.0f / 20.0f) + 1.0e-4f, Str (instrumentName (s.instrument)) + " peak " + Str (params::gainToDb (peak), 2));
                expect (peak > 0.05f, Str (instrumentName (s.instrument)) + " too quiet");
                expectEquals (s.audio.getNumSamples(), (int) std::ceil (song.lengthSeconds() * kSr));
            }

            std::random_device rd;
            const auto dir = std::filesystem::temp_directory_path() / ("pg-band-" + std::to_string (rd() & 0x3fffffff));
            std::string err;
            const auto files = writeStems (song, stems, kSr, dir, err);
            expect (err.empty(), err);
            expectEquals ((int) files.size(), (int) stems.size());
            if (! files.empty())
            {
                drwav w;
#if defined(_WIN32)
                const bool opened = drwav_init_file_w (&w, files[0].c_str(), nullptr);
#else
                const bool opened = drwav_init_file (&w, files[0].c_str(), nullptr);
#endif
                expect (opened && w.channels == 2 && w.bitsPerSample == 24 && w.sampleRate == 48000);
                if (opened) drwav_uninit (&w);
                expect (files::toText (files[0].filename()).rfind ("01 ", 0) == 0);
            }
            // MIDI: header + one track per part + the tempo track
            const auto mid = files::readText (dir / "Band Forge.mid");
            expect (mid.has_value() && mid->size() > 14 && mid->compare (0, 4, "MThd") == 0);
            if (mid && mid->size() > 14)
            {
                const int declared = ((unsigned char) (*mid)[10] << 8) | (unsigned char) (*mid)[11];
                int chunks = 0;
                for (size_t pos = 0; (pos = mid->find ("MTrk", pos)) != std::string::npos; pos += 4) ++chunks;
                expectEquals (declared, (int) song.parts.size() + 1);
                expectEquals (chunks, (int) song.parts.size() + 1);
            }
            std::error_code ec;
            std::filesystem::remove_all (dir, ec);
        }

        beginTest ("cancel stops rendering");
        {
            Request r; r.genre = Genre::outlawCountry; r.seed = 2;
            const auto song = compose (r);
            std::vector<Stem> stems;
            std::atomic<bool> cancel { true };
            expect (! renderSong (song, kSr, stems, {}, &cancel));
        }
    }
};

static BandComposerTests bandComposerTests;
static BandInstrumentTests bandInstrumentTests;
static BandRenderTests bandRenderTests;
} // namespace hem::pg::test
