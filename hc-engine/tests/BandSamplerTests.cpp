// BAND FORGE sound pack: zone choice (key, velocity, round robin), pitch shifting, loops,
// hi-hat choke, drum fallback, and the real HEM Sound Pack when one is installed.

#include "UnitTest.hpp"
#include "BandComposer.hpp"
#include "BandRender.hpp"
#include "Loudness.hpp"
#include "export/WavFile.hpp"
#include "hem/pg/Files.hpp"

#include "dr_wav.h"

#include <cmath>
#include <cstdlib>

namespace hem::pg::test
{
using hemtest::Str;
namespace
{
using namespace hem::pg::band;
namespace fs = std::filesystem;
constexpr double kPackSr = 48000.0;

/** The test pack's samples: 24-bit stereo WAV (0.10.0's test wrote 24-bit FLAC; same samples). */
void writeSample (const fs::path& f, const std::vector<float>& mono)
{
    std::error_code ec;
    fs::create_directories (f.parent_path(), ec);
    WavWriter w;
    std::string err;
    if (! w.open (f, kPackSr, 2, 24, err)) return;
    const float* chans[2] { mono.data(), mono.data() };
    w.write (chans, (int) mono.size());
    w.close();
}

std::vector<float> sine (double hz, int frames, float amp)
{
    std::vector<float> x ((size_t) frames);
    for (int i = 0; i < frames; ++i) x[(size_t) i] = amp * (float) std::sin (2.0 * MathConstants<double>::pi * hz * i / kPackSr);
    return x;
}

/** A tiny pack: piano (2 velocity layers, 2 round robins on the loud one), looped strings, drums. */
fs::path makeTestPack (const fs::path& dir)
{
    std::error_code ec;
    fs::remove_all (dir, ec);
    fs::create_directories (dir, ec);
    writeSample (dir / "piano" / "soft.wav", sine (261.6256, 96000, 0.2f));
    writeSample (dir / "piano" / "loud1.wav", sine (261.6256, 96000, 0.8f));
    writeSample (dir / "piano" / "loud2.wav", sine (261.6256, 96000, 0.6f));
    // 440 Hz has a period of exactly 1200/11 samples at 48 kHz, so a 24000-sample loop is seamless
    writeSample (dir / "strings" / "a4.wav", sine (440.0, 36000, 0.5f));
    std::vector<float> click (4800, 0.0f), hat (48000), closed (480);
    for (int i = 0; i < 200; ++i) click[(size_t) i] = 0.9f * (1.0f - i / 200.0f);
    hemtest::Random r (3);
    for (auto& s : hat) s = 0.3f * (r.nextFloat() * 2.0f - 1.0f);
    for (auto& s : closed) s = 0.3f * (r.nextFloat() * 2.0f - 1.0f);
    writeSample (dir / "drums" / "kick.wav", click);
    writeSample (dir / "drums" / "open.wav", hat);
    writeSample (dir / "drums" / "closed.wav", closed);
    files::writeTextAtomic (dir / "pack.json", R"({
 "name": "test", "version": 1, "sampleRate": 48000,
 "instruments": {
  "piano": {"type": "pitched", "release": 0.05, "zones": [
    {"file": "piano/soft.wav",  "root": 60, "lo": 0, "hi": 127, "velLo": 0.0, "velHi": 0.5, "rr": 1, "loopStart": -1, "loopEnd": -1},
    {"file": "piano/loud1.wav", "root": 60, "lo": 0, "hi": 127, "velLo": 0.5, "velHi": 1.0, "rr": 1, "loopStart": -1, "loopEnd": -1},
    {"file": "piano/loud2.wav", "root": 60, "lo": 0, "hi": 127, "velLo": 0.5, "velHi": 1.0, "rr": 2, "loopStart": -1, "loopEnd": -1}]},
  "strings": {"type": "pitched", "release": 0.2, "zones": [
    {"file": "strings/a4.wav", "root": 69, "lo": 0, "hi": 127, "velLo": 0.0, "velHi": 1.0, "rr": 1, "loopStart": 12000, "loopEnd": 36000}]},
  "drums": {"type": "drums", "zones": [
    {"file": "drums/kick.wav", "note": 36, "velLo": 0.0, "velHi": 1.0, "rr": 1},
    {"file": "drums/open.wav", "note": 46, "velLo": 0.0, "velHi": 1.0, "rr": 1},
    {"file": "drums/closed.wav", "note": 42, "velLo": 0.0, "velHi": 1.0, "rr": 1}]}
 }})");
    return dir;
}

Song oneNoteSong (Instrument inst, std::vector<Note> notes)
{
    Song s;
    s.bpm = 60.0;           // 1 beat = 1 second
    s.totalBars = 4;
    Part p;
    p.instrument = inst;
    p.notes = std::move (notes);
    s.parts.push_back (p);
    return s;
}

Note note (double start, double len, int pitch, float vel) { Note n; n.start = start; n.length = len; n.pitch = pitch; n.velocity = vel; return n; }

double hzOf (const AudioBuffer& b, double sr, double t0, double t1, double expect)
{
    const int s0 = (int) (t0 * sr), n = (int) ((t1 - t0) * sr);
    std::vector<double> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = b.getSample (0, s0 + i) + b.getSample (1, s0 + i);
    auto corr = [&] (int lag)
    {
        double num = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { num += x[(size_t) i] * x[(size_t) (i + lag)]; e1 += x[(size_t) i] * x[(size_t) i]; e2 += x[(size_t) (i + lag)] * x[(size_t) (i + lag)]; }
        return num / std::sqrt (e1 * e2 + 1e-20);
    };
    const int lo = (int) (sr / (expect * 1.12)), hi = (int) (sr / (expect / 1.12)) + 1;
    int best = lo; double bc = -2;
    for (int lag = lo; lag <= hi; ++lag) { const double c = corr (lag); if (c > bc) { bc = c; best = lag; } }
    const double cm = corr (best - 1), cp = corr (best + 1), den = cm - 2 * bc + cp;
    return sr / (best + (std::abs (den) > 1e-12 ? 0.5 * (cm - cp) / den : 0.0));
}

double rms (const AudioBuffer& b, double sr, double t0, double t1)
{
    const int s0 = (int) (t0 * sr), n = jmax (1, (int) ((t1 - t0) * sr));
    double sum = 0;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) sum += (double) b.getSample (ch, s0 + i) * b.getSample (ch, s0 + i);
    return std::sqrt (sum / (2.0 * n));
}

double peakAbs (const AudioBuffer& b, double sr, double t0, double t1)
{
    return b.getMagnitude ((int) (t0 * sr), (int) ((t1 - t0) * sr));
}

AudioBuffer render (const SoundPack& pack, const Song& s, double sr, std::vector<Note>& left, bool& ok)
{
    AudioBuffer b (2, (int) (s.lengthSeconds() * sr));
    b.clear();
    std::string err;
    ok = pack.renderPart (s, s.parts[0], sr, b, left, nullptr, err);
    return b;
}
} // namespace

class BandSamplerTests final : public hemtest::UnitTest
{
public:
    BandSamplerTests() : UnitTest ("Band Forge sound pack", "Playground") {}

    void runTest() override
    {
        const auto dir = makeTestPack (fs::temp_directory_path() / "hem-test-pack");
        SoundPack pack;
        std::string err;

        beginTest ("pack.json opens and lists its instruments");
        expect (pack.open (dir, err), err);
        expect (pack.has (Instrument::piano) && pack.has (Instrument::strings) && pack.has (Instrument::drums));
        expect (! pack.has (Instrument::banjo));
        SoundPack empty;
        expect (! empty.open (dir / "nope", err));

        beginTest ("notes are pitch-shifted from the nearest sample, in tune at 48 kHz and 44.1 kHz");
        for (double sr : { 48000.0, 44100.0 })
            for (int p : { 55, 60, 64, 67 })
            {
                std::vector<Note> left; bool ok = false;
                const auto b = render (pack, oneNoteSong (Instrument::piano, { note (0, 1.5, p, 0.9f) }), sr, left, ok);
                expect (ok);
                const double want = 440.0 * std::pow (2.0, (p - 69) / 12.0);
                const double c = 1200.0 * std::log2 (hzOf (b, sr, 0.2, 1.0, want) / want);
                expectLessThan (std::abs (c), 2.0, "pitch " + Str (p) + " at " + Str (sr) + ": " + Str (c, 2) + " cents");
            }

        beginTest ("velocity picks the layer; loud round robins alternate");
        {
            std::vector<Note> left; bool ok = false;
            const auto b = render (pack, oneNoteSong (Instrument::piano, { note (0, 0.5, 60, 0.3f), note (1, 0.5, 60, 1.0f), note (2, 0.5, 60, 1.0f) }), kPackSr, left, ok);
            const double soft = rms (b, kPackSr, 0.1, 0.4), loud1 = rms (b, kPackSr, 1.1, 1.4), loud2 = rms (b, kPackSr, 2.1, 2.4);
            expect (ok);
            expectGreaterThan (loud1 / soft, 4.0 * 0.8, "soft layer is the quiet sample");
            expectWithinAbsoluteError (loud2 / loud1, 0.75, 0.03, "second loud note uses the second round robin");
        }

        beginTest ("note off fades out over the release; nothing after it");
        {
            std::vector<Note> left; bool ok = false;
            const auto b = render (pack, oneNoteSong (Instrument::piano, { note (0, 0.5, 60, 1.0f) }), kPackSr, left, ok);
            expectLessThan (peakAbs (b, kPackSr, 0.56, 1.5), 1.0e-6);
            expectGreaterThan (rms (b, kPackSr, 0.3, 0.45), 0.1);
        }

        beginTest ("a held note longer than its sample keeps playing through the loop, seamlessly");
        {
            std::vector<Note> left; bool ok = false;
            const auto b = render (pack, oneNoteSong (Instrument::strings, { note (0, 3.0, 69, 1.0f) }), kPackSr, left, ok);
            const double early = rms (b, kPackSr, 0.1, 0.6), late = rms (b, kPackSr, 2.3, 2.9);
            expectWithinAbsoluteError (late / early, 1.0, 0.02, "level holds after the sample's end (0.75 s)");
            // a jump at the loop seam would show as a big sample-to-sample step; a 440 Hz sine's largest step is ~0.029 * amp
            float maxStep = 0.0f;
            const auto* d = b.getReadPointer (0);
            for (int i = 1; i < (int) (2.95 * kPackSr); ++i) maxStep = jmax (maxStep, std::abs (d[i] - d[i - 1]));
            const float amp = (float) (early * std::sqrt (2.0));
            expectLessThan (maxStep, amp * 0.06f, "no click at the loop seam");
        }

        beginTest ("drum notes the pack lacks go to the modeled kit; the stem is marked recorded");
        {
            std::vector<Note> left; bool ok = false;
            render (pack, oneNoteSong (Instrument::drums, { note (0, 0.25, drum::kick, 1.0f), note (1, 0.25, drum::clap, 1.0f) }), kPackSr, left, ok);
            expect (ok);
            expectEquals ((int) left.size(), 1);
            if (! left.empty()) expectEquals (left[0].pitch, drum::clap);

            std::vector<Stem> stems;
            expect (renderSong (oneNoteSong (Instrument::drums, { note (0, 0.25, drum::kick, 1.0f), note (1, 0.25, drum::clap, 1.0f) }), kPackSr, stems, {}, nullptr, &pack));
            expect (stems.size() == 1 && stems[0].recorded);
            if (! stems.empty())
            {
                expectGreaterThan (peakAbs (stems[0].audio, kPackSr, 0.0, 0.05), 0.01, "sampled kick");
                expectGreaterThan (peakAbs (stems[0].audio, kPackSr, 1.0, 1.1), 0.01, "modeled clap");
            }
        }

        beginTest ("a closed hi-hat chokes the open one");
        {
            std::vector<Note> left; bool ok = false;
            const auto b = render (pack, oneNoteSong (Instrument::drums, { note (0, 0.25, drum::hatOpen, 1.0f), note (0.5, 0.25, drum::hatClosed, 1.0f) }), kPackSr, left, ok);
            expectGreaterThan (rms (b, kPackSr, 0.1, 0.4), 0.05, "open hat rings");
            expectLessThan (peakAbs (b, kPackSr, 0.55, 0.95), 1.0e-6, "silent once choked (the closed hat is 10 ms)");
        }

        beginTest ("instruments not in the pack, or whose samples are missing, use the modeled sound");
        {
            std::vector<Stem> stems;
            expect (renderSong (oneNoteSong (Instrument::banjo, { note (0, 0.5, 60, 0.9f) }), kPackSr, stems, {}, nullptr, &pack));
            expect (stems.size() == 1 && ! stems[0].recorded && stems[0].audio.getMagnitude (0, stems[0].audio.getNumSamples()) > 0.01f);
            { std::error_code ec; fs::remove (dir / "strings" / "a4.wav", ec); }
            stems.clear();
            expect (renderSong (oneNoteSong (Instrument::strings, { note (0, 1.0, 69, 0.9f) }), kPackSr, stems, {}, nullptr, &pack));
            expect (stems.size() == 1 && ! stems[0].recorded && stems[0].audio.getMagnitude (0, stems[0].audio.getNumSamples()) > 0.01f);
        }
        { std::error_code ec; fs::remove_all (dir, ec); }

        // The real pack: set HEM_SOUND_PACK to its folder, or install it where the app looks.
        auto real = std::make_shared<SoundPack>();
        const char* env = std::getenv ("HEM_SOUND_PACK");
        if (env != nullptr && *env != 0) expect (real->open (files::toPath (env), err), err);
        else real = std::const_pointer_cast<SoundPack> (SoundPack::findInstalled());
        if (! real->isOpen())
        {
            logMessage ("HEM Sound Pack not installed here: real-recording checks skipped");
            return;
        }
        logMessage ("HEM Sound Pack: " + real->describe() + " (" + files::toText (real->getFolder()) + ")");

        beginTest ("HEM Sound Pack: recorded instruments play in tune across their range (within 30 cents)");
        struct P { Instrument inst; std::vector<int> notes; };
        for (const auto& [inst, notes] : std::vector<P> { { Instrument::piano, { 36, 48, 60, 72, 84 } }, { Instrument::fiddle, { 57, 64, 69, 76, 83 } },
                                                          { Instrument::strings, { 40, 50, 60, 67, 79 } }, { Instrument::uprightBass, { 31, 36, 43, 48 } } })
        {
            if (! real->has (inst)) continue;
            for (int p : notes)
            {
                std::vector<Note> left; bool ok = false;
                const auto b = render (*real, oneNoteSong (inst, { note (0, 1.6, p, 0.8f) }), kPackSr, left, ok);
                expect (ok, instrumentName (inst));
                const double want = 440.0 * std::pow (2.0, (p - 69) / 12.0);
                const double c = 1200.0 * std::log2 (hzOf (b, kPackSr, 0.35, 1.2, want) / want);
                expectLessThan (std::abs (c), 30.0, Str (instrumentName (inst)) + " note " + Str (p) + ": " + Str (c, 1) + " cents");
            }
        }

        beginTest ("HEM Sound Pack: a full bluegrass song renders from the recordings, finite and levelled");
        {
            Request r; r.genre = Genre::bluegrass; r.fullSong = false; r.seed = 11;
            r.instruments = { Instrument::fiddle, Instrument::uprightBass, Instrument::drums, Instrument::banjo };
            std::vector<Stem> stems;
            expect (renderSong (compose (r), kPackSr, stems, {}, nullptr, real.get()));
            for (auto& s : stems)
            {
                expectEquals ((int) s.recorded, (int) real->has (s.instrument), instrumentName (s.instrument));
                bool finite = true;
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < s.audio.getNumSamples(); ++i) finite = finite && std::isfinite (s.audio.getSample (ch, i));
                expect (finite);
                expect (s.audio.getMagnitude (0, s.audio.getNumSamples()) <= 0.892f, "peak under -1 dBFS");
            }
        }
    }
};

class BandMixTests final : public hemtest::UnitTest
{
public:
    BandMixTests() : UnitTest ("Band Forge hook lift and mix file", "Playground") {}

    static Song formSong()
    {
        Song s;
        s.bpm = 120.0;   // 1 bar = 2 s
        s.totalBars = 16;
        auto add = [&s] (Section k, int start, int bars, float e) { SectionPlan p; p.kind = k; p.startBar = start; p.bars = bars; p.energy = e; s.sections.push_back (p); };
        add (Section::verse, 0, 4, 0.55f);
        add (Section::chorus, 4, 4, 0.9f);
        add (Section::bridge, 8, 4, 0.5f);
        add (Section::chorus, 12, 4, 1.0f);
        return s;
    }

    void runTest() override
    {
        const Song song = formSong();

        beginTest ("the biggest section keeps its level; verses sit under the hook; pads move more than drums");
        expectWithinAbsoluteError (sectionGainDb (song, Instrument::piano, 3), 0.0f, 1.0e-6f);
        expectLessThan (sectionGainDb (song, Instrument::piano, 0), sectionGainDb (song, Instrument::piano, 1) - 2.0f);
        expectLessThan (sectionGainDb (song, Instrument::strings, 0), sectionGainDb (song, Instrument::drums, 0));

        beginTest ("the lift is applied to the stem, with a swell into the hook and no steps");
        {
            Stem st;
            st.instrument = Instrument::piano;
            const double sr = 48000.0;
            st.audio.setSize (2, (int) (34.0 * sr));
            for (int i = 0; i < st.audio.getNumSamples(); ++i)
            {
                const float v = 0.5f * (float) std::sin (2.0 * MathConstants<double>::pi * 220.0 * i / sr);
                st.audio.setSample (0, i, v); st.audio.setSample (1, i, v);
            }
            applySectionDynamics (song, st, sr);
            auto db = [&] (double t0, double t1) { return 20.0 * std::log10 (rms (st.audio, sr, t0, t1) / (0.5 / std::sqrt (2.0))); };
            expectWithinAbsoluteError (db (1.0, 5.0), (double) sectionGainDb (song, Instrument::piano, 0), 0.05);
            expectWithinAbsoluteError (db (9.0, 15.0), (double) sectionGainDb (song, Instrument::piano, 1), 0.05);
            expectWithinAbsoluteError (db (25.0, 31.0), 0.0, 0.05);
            // last beat of the verse is already swelling toward the chorus
            expectGreaterThan (db (7.6, 7.95), sectionGainDb (song, Instrument::piano, 0) + 0.5);
            // largest sample-to-sample change stays a sine's (no gain jumps)
            float maxStep = 0.0f;
            const auto* d = st.audio.getReadPointer (0);
            for (int i = 1; i < st.audio.getNumSamples(); ++i) maxStep = jmax (maxStep, std::abs (d[i] - d[i - 1]));
            expectLessThan (maxStep, 0.5f * 2.0f * MathConstants<float>::pi * 220.0f / 48000.0f * 1.05f);
        }

        beginTest ("mix file: CLEAN peaks at -3 dBFS (only turned down); MASTERED lands on -14 LUFS under -1 dBFS");
        {
            Request r; r.genre = Genre::outlawCountry; r.fullSong = false; r.seed = 3;
            const Song s = compose (r);
            std::vector<Stem> stems;
            expect (renderSong (s, kPackSr, stems));
            const auto dir = fs::temp_directory_path() / "hem-band-mix";
            std::error_code ec;
            fs::create_directories (dir, ec);
            std::string info, err;
            auto read = [] (const fs::path& f)
            {
                drwav w;
#if defined(_WIN32)
                const bool opened = drwav_init_file_w (&w, f.c_str(), nullptr);
#else
                const bool opened = drwav_init_file (&w, f.c_str(), nullptr);
#endif
                AudioBuffer b (2, opened ? (int) w.totalPCMFrameCount : 0);
                if (opened)
                {
                    std::vector<float> inter ((size_t) w.totalPCMFrameCount * w.channels);
                    drwav_read_pcm_frames_f32 (&w, w.totalPCMFrameCount, inter.data());
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        for (int c = 0; c < 2; ++c) b.setSample (c, i, inter[(size_t) i * w.channels + (size_t) (c < (int) w.channels ? c : 0)]);
                    drwav_uninit (&w);
                }
                return b;
            };
            const auto cleanFile = dir / "clean.wav", masterFile = dir / "master.wav";
            expect (writeMix (stems, kPackSr, MixFinish::clean, cleanFile, info, err), err);
            const auto clean = read (cleanFile);
            expectEquals (clean.getNumSamples(), stems[0].audio.getNumSamples());
            expectWithinAbsoluteError (clean.getMagnitude (0, clean.getNumSamples()), params::dbToGain (-3.0f), 0.002f);

            expect (writeMix (stems, kPackSr, MixFinish::mastered, masterFile, info, err), err);
            logMessage ("mastered mix: " + info);
            const auto mastered = read (masterFile);
            expectEquals (mastered.getNumSamples(), stems[0].audio.getNumSamples());
            expectLessThan (mastered.getMagnitude (0, mastered.getNumSamples()), params::dbToGain (-0.9f));
            dsp::LoudnessMeter lm;
            lm.prepare (kPackSr);
            lm.process (mastered.getReadPointer (0), mastered.getReadPointer (1), mastered.getNumSamples());
            expectWithinAbsoluteError (lm.getIntegrated(), -14.0f, 1.0f, "integrated " + Str (lm.getIntegrated(), 2) + " LUFS");
            expect (! writeMix (stems, kPackSr, MixFinish::none, dir / "none.wav", info, err) || ! fs::exists (dir / "none.wav", ec));
            fs::remove_all (dir, ec);
        }
    }
};

static BandSamplerTests bandSamplerTests;
static BandMixTests bandMixTests;
} // namespace hem::pg::test
