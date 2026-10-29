// Mixer engine tests (carried over from 0.10.0): routing, faders, pan, mute / solo, buses, sends,
// inserts, BEFORE / AFTER, transport (loop, stop at end), rate conversion, sessions, undo, garbage,
// delay compensation, the sinc converter, media decoding, session files and offline export,
// and zero allocation on the audio thread.

#include "UnitTest.hpp"
#include "PlaygroundEngine.hpp"
#include "export/Exporter.hpp"
#include "export/Mp3Encoder.hpp"
#include "export/WavFile.hpp"
#include "media/MediaLoader.hpp"
#include "media/SincResampler.hpp"
#include "session/SessionFile.hpp"
#include "session/UndoHistory.hpp"

#include "dr_wav.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <random>

namespace hem::pg::test
{
using hemtest::Str;
namespace
{
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;
    using E = PlaygroundEngine;

    /** Stereo test audio: two channels of n samples. */
    struct Stereo
    {
        std::vector<float> l, r;
        int size() const { return (int) l.size(); }
        float magnitude (int start, int n) const
        {
            float m = 0.0f;
            for (int i = start; i < start + n; ++i) m = jmax (m, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
            return m;
        }
        float magnitude() const { return magnitude (0, size()); }
        float rms (int start, int n) const
        {
            double s = 0.0;
            for (int i = start; i < start + n; ++i) s += (double) l[(size_t) i] * l[(size_t) i];
            return (float) std::sqrt (s / jmax (1, n));
        }
    };

    SampleData::Ptr makeMedia (std::vector<std::vector<float>> ch, double rate, const std::string& path = {})
    {
        return std::make_shared<const SampleData> (std::move (ch), rate, SessionFile::toPath (path));
    }

    SampleData::Ptr constant (float l, float r, double seconds, double rate = kFs, const std::string& path = {})
    {
        const auto n = (size_t) (seconds * rate);
        return makeMedia ({ std::vector<float> (n, l), std::vector<float> (n, r) }, rate, path);
    }

    SampleData::Ptr sine (double hz, float amp, double seconds, double rate)
    {
        const int n = (int) (seconds * rate);
        std::vector<float> b ((size_t) n);
        for (int i = 0; i < n; ++i) b[(size_t) i] = amp * (float) std::sin (MathConstants<double>::twoPi * hz * i / rate);
        return makeMedia ({ std::move (b) }, rate);
    }

    void set (E& e, const std::string& id, float value) { e.setMixer (id, value); }
    void set (Plugin& p, const std::string& id, float value) { p.setParameter (id, value); }
    float mix (const E& e, const std::string& id) { return e.getMixer (id); }

    Stereo render (E& e, double seconds, int block = kBlock)
    {
        const int total = (int) (seconds * e.getSampleRate());
        Stereo out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
        std::vector<float> l ((size_t) block), r ((size_t) block);
        for (int start = 0; start < total; start += block)
        {
            const int n = jmin (block, total - start);
            float* outs[2] { l.data(), r.data() };
            e.process (outs, 2, n);
            std::copy (l.begin(), l.begin() + n, out.l.begin() + start);
            std::copy (r.begin(), r.begin() + n, out.r.begin() + start);
        }
        return out;
    }

    float last (const Stereo& b, int ch) { return ch == 0 ? b.l.back() : b.r.back(); }

    std::unique_ptr<E> makeEngine()
    {
        auto e = std::make_unique<E>();
        e->prepare (kFs, kBlock);
        return e;
    }

    std::filesystem::path tempDir (const std::string& name)
    {
        std::random_device rd;
        auto d = std::filesystem::temp_directory_path() / ("pg-test-" + name + "-" + std::to_string (rd() & 0x3fffffff));
        std::filesystem::create_directories (d);
        return d;
    }

    bool writeWav (const std::filesystem::path& f, const std::vector<std::vector<float>>& audio, double rate, int bits = 24)
    {
        std::error_code ec;
        std::filesystem::create_directories (f.parent_path(), ec);
        WavWriter w;
        std::string err;
        if (! w.open (f, rate, (int) audio.size(), bits, err)) return false;
        const float* chans[2] { audio[0].data(), audio[audio.size() > 1 ? 1 : 0].data() };
        return w.write (chans, (int) audio[0].size()) && w.close();
    }

    Stereo readWav (const std::filesystem::path& f, double& rate)
    {
        drwav wav;
#if defined(_WIN32)
        if (! drwav_init_file_w (&wav, f.c_str(), nullptr)) return {};
#else
        if (! drwav_init_file (&wav, f.c_str(), nullptr)) return {};
#endif
        rate = wav.sampleRate;
        const auto n = (size_t) wav.totalPCMFrameCount;
        std::vector<float> inter (n * wav.channels);
        drwav_read_pcm_frames_f32 (&wav, n, inter.data());
        Stereo s { std::vector<float> (n), std::vector<float> (n) };
        for (size_t i = 0; i < n; ++i) { s.l[i] = inter[i * wav.channels]; s.r[i] = inter[i * wav.channels + (wav.channels > 1 ? 1 : 0)]; }
        drwav_uninit (&wav);
        return s;
    }

    bool contains (const StringArray& a, const std::string& s) { return std::find (a.begin(), a.end(), s) != a.end(); }
    bool fileExists (const std::filesystem::path& p) { std::error_code ec; return std::filesystem::exists (p, ec); }
}

//==============================================================================
class MixerTests final : public hemtest::UnitTest
{
public:
    MixerTests() : UnitTest ("Mixer Engine", "Playground") {}

    void runTest() override
    {
        beginTest ("a track plays at unity through the master; stopped = silence");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.5f, 0.25f, 2.0), "Kick");
            expectEquals (t, 0);
            expectEquals (e->getTrackName (t), std::string ("Kick"));
            auto silent = render (*e, 0.1);
            expectEquals (silent.magnitude(), 0.0f);
            e->play();
            auto out = render (*e, 0.2);
            expectWithinAbsoluteError (last (out, 0), 0.5f, 1.0e-5f);
            expectWithinAbsoluteError (last (out, 1), 0.25f, 1.0e-5f);
            expectWithinAbsoluteError (e->getPositionSeconds(), 0.2, 1.0e-3);
            expectWithinAbsoluteError (e->getSongLengthSeconds(), 2.0, 1.0e-6);
            expect (e->getPeak (t, 0) > 0.49f && e->getPeak (E::kMaster, 0) > 0.49f);
        }

        beginTest ("fader, pan, mute, solo");
        {
            auto e = makeEngine();
            const int a = e->addTrack (constant (0.5f, 0.5f, 5.0), "A");
            const int b = e->addTrack (constant (0.2f, 0.2f, 5.0), "B");
            e->play();
            set (*e, E::Ids::track (a, "vol"), -6.0f);
            auto out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.5f * params::dbToGain (-6.0f) + 0.2f, 1.0e-4f);

            set (*e, E::Ids::track (a, "vol"), 0.0f);
            set (*e, E::Ids::track (a, "pan"), -1.0f);   // hard left: right side of A gone
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.7f, 1.0e-4f);
            expectWithinAbsoluteError (last (out, 1), 0.2f, 1.0e-4f);
            set (*e, E::Ids::track (a, "pan"), 0.0f);

            set (*e, E::Ids::track (b, "mute"), 1.0f);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.5f, 1.0e-4f);
            set (*e, E::Ids::track (b, "mute"), 0.0f);

            set (*e, E::Ids::track (b, "solo"), 1.0f);   // solo B: A is silenced
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.2f, 1.0e-4f);
            set (*e, E::Ids::track (b, "solo"), 0.0f);

            set (*e, E::Ids::track (a, "vol"), E::kFaderOffDb);   // fader at the bottom = off
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.2f, 1.0e-4f);

            // fader moves are smoothed: no jump bigger than the 20 ms ramp allows
            set (*e, E::Ids::track (a, "vol"), 0.0f);
            out = render (*e, 0.05);
            float maxStep = 0.0f;
            for (int i = 1; i < out.size(); ++i) maxStep = jmax (maxStep, std::abs (out.l[(size_t) i] - out.l[(size_t) i - 1]));
            expectLessThan (maxStep, 0.01f, "fader zipper / click");
        }

        beginTest ("track output to a bus; bus fader and mute; master fader");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.4f, 0.4f, 5.0), "Snare");
            e->play();
            set (*e, E::Ids::track (t, "out"), 1.0f);              // Bus 1 (Drum Bus)
            set (*e, E::Ids::bus (0, "vol"), -6.0f);
            auto out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.4f * params::dbToGain (-6.0f), 1.0e-4f);
            expect (e->getPeak (E::busStrip (0), 0) > 0.19f);
            set (*e, E::Ids::bus (0, "mute"), 1.0f);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.0f, 1.0e-6f);
            set (*e, E::Ids::bus (0, "mute"), 0.0f);
            set (*e, E::Ids::bus (0, "vol"), 0.0f);
            set (*e, E::Ids::masterVol, -6.0f);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.4f * params::dbToGain (-6.0f), 1.0e-4f);
        }

        beginTest ("master fader drives Elite Master: +6 dB on a hot mix still never passes the ceiling");
        {
            auto e = makeEngine();
            e->addTrack (sine (220.0, 0.8f, 3.0, kFs), "Hot");
            expect (e->insertPlugin (E::kMaster, E::kSlots - 1, "elite-master") != nullptr);
            set (*e, E::Ids::masterVol, 6.0f);
            e->play();
            auto out = render (*e, 1.5);
            const float pk = out.magnitude ((int) kFs / 2, out.size() - (int) kFs / 2);
            expectLessThan (pk, params::dbToGain (-1.0f) + 1.0e-3f, "master fader pushed the export past Elite Master's ceiling");
            expectGreaterThan (pk, params::dbToGain (-1.5f), "the limiter should be working right up at the ceiling");
            expect (e->getPeak (E::kMaster, 0) <= params::dbToGain (-1.0f) + 1.0e-3f, "master meter shows the limited level");
        }

        beginTest ("master tap: the analyzer gets the master output (mono sum) only while it is switched on");
        {
            auto e = makeEngine();
            e->addTrack (constant (0.5f, 0.25f, 2.0), "Tap");
            e->play();
            std::vector<float> got ((size_t) E::kTapSize);
            render (*e, 0.05);
            expectEquals (e->readMasterTap (got.data(), (int) got.size()), 0);
            e->setMasterTapActive (true);
            render (*e, 0.1);
            const int n = e->readMasterTap (got.data(), (int) got.size());
            expectEquals (n, (int) (0.1 * kFs));
            expectWithinAbsoluteError (got[(size_t) n - 1], 0.375f, 1.0e-5f);
            std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
            float* outs[2] { l.data(), r.data() };
            int allocations = 0;
            for (int i = 0; i < 40; ++i)
            {
                hemtest::beginTracking();
                e->process (outs, 2, kBlock);
                allocations += hemtest::endTracking();
            }
            expectEquals (allocations, 0, "the tap must not allocate on the audio thread");
        }

        beginTest ("undo / redo: a fader move, then a removed track comes back with its audio");
        {
            auto e = makeEngine();
            const auto kickPath = SessionFile::toText (std::filesystem::temp_directory_path() / "pg-undo-kick.wav");
            const int t = e->addTrack (constant (0.5f, 0.5f, 2.0, kFs, kickPath), "Kick");
            UndoHistory h (*e, 2);
            h.reset();
            expect (! h.canUndo());
            set (*e, E::Ids::track (t, "vol"), -6.0f);
            for (int i = 0; i < 3; ++i) h.poll();
            expect (h.canUndo(), "a settled fader move is one undo step");
            expect (h.undo());
            expect (! h.lastRestoreWasStructural());
            expectWithinAbsoluteError (mix (*e, E::Ids::track (t, "vol")), 0.0f, 0.01f);
            expect (h.redo());
            expectWithinAbsoluteError (mix (*e, E::Ids::track (t, "vol")), -6.0f, 0.01f);

            e->removeTrack (t);
            for (int i = 0; i < 3; ++i) h.poll();
            expect (! e->isTrackUsed (t));
            expect (h.undo());
            expect (h.lastRestoreWasStructural());
            expect (e->isTrackUsed (t), "undo brings the removed track back");
            expect (e->getTrackMedia (t) != nullptr, "... with its audio, without reading the disk");
            expectEquals (e->getTrackName (t), std::string ("Kick"));
            expectWithinAbsoluteError (mix (*e, E::Ids::track (t, "vol")), -6.0f, 0.01f);
            h.undo();   // one more step back (fader to 0 dB) must also work
            expectWithinAbsoluteError (mix (*e, E::Ids::track (t, "vol")), 0.0f, 0.01f);
        }

        beginTest ("import: MP3 is in the open-file list");
        expect (MediaLoader::wildcard().find ("*.mp3") != std::string::npos, MediaLoader::wildcard());

        beginTest ("sends are post-fader and follow mute; they add to the dry path");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.3f, 0.3f, 5.0), "Vox");
            e->play();
            set (*e, E::Ids::track (t, "sa"), 0.0f);        // Send A at 0 dB -> Bus 4 (Reverb), empty bus = passes through
            auto out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.6f, 1.0e-4f);
            expect (e->getPeak (E::busStrip (3), 0) > 0.29f);
            set (*e, E::Ids::track (t, "vol"), -6.0f);      // post-fader: both halve
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.6f * params::dbToGain (-6.0f), 1.0e-4f);
            set (*e, E::Ids::track (t, "mute"), 1.0f);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.0f, 1.0e-6f);
            set (*e, E::Ids::track (t, "mute"), 0.0f);
            set (*e, E::Ids::track (t, "vol"), 0.0f);
            set (*e, E::Ids::track (t, "sb"), 0.0f);        // Send B -> Bus 5 as well
            set (*e, E::Ids::track (t, "sb_bus"), 4.0f);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.9f, 1.0e-4f);
        }

        beginTest ("inserts process the track; Elite Master is refused outside the master");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.5f, 0.5f, 5.0), "Bass");
            auto* eq = e->insertPlugin (t, 0, "elite-eq");
            expect (eq != nullptr);
            set (*eq, "output", -6.0f);
            e->play();
            auto out = render (*e, 0.2);
            expectWithinAbsoluteError (last (out, 0), 0.5f * params::dbToGain (-6.0f), 2.0e-3f);
            expect (e->insertPlugin (t, 1, "elite-master") == nullptr);
            expect (e->insertPlugin (E::busStrip (0), 0, "elite-master") == nullptr);
            expect (e->insertPlugin (E::kMaster, 7, "elite-master") != nullptr);
            expect (e->insertPlugin (t, 1, "no-such-plugin") == nullptr);
            expect (! e->canHost (t, "elite-master"));
            expect (e->canHost (E::kMaster, "elite-master"));

            beginTest ("swap and remove inserts");
            auto* comp = e->insertPlugin (t, 3, "elite-comp");
            e->swapSlots (t, 0, 3);
            expect (e->getPlugin (t, 0) == comp);
            expect (e->getPlugin (t, 3) == eq);
            e->removePlugin (t, 3);
            expect (e->getPlugin (t, 3) == nullptr);
            render (*e, 0.05);
        }

        beginTest ("BEFORE mode bypasses every insert (crossfaded), AFTER brings them back");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.5f, 0.5f, 5.0), "Mix");
            set (*e->insertPlugin (t, 0, "elite-eq"), "output", -12.0f);
            e->play();
            auto out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.5f * params::dbToGain (-12.0f), 2.0e-3f);
            e->setBeforeMode (true);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.5f, 1.0e-4f);
            float maxStep = 0.0f;
            for (int i = 1; i < out.size(); ++i) maxStep = jmax (maxStep, std::abs (out.l[(size_t) i] - out.l[(size_t) i - 1]));
            expectLessThan (maxStep, 0.01f, "BEFORE/AFTER click");
            e->setBeforeMode (false);
            out = render (*e, 0.1);
            expectWithinAbsoluteError (last (out, 0), 0.5f * params::dbToGain (-12.0f), 2.0e-3f);
        }

        beginTest ("a 44.1 kHz file plays at the right pitch and length on a 48 kHz engine");
        {
            auto e = makeEngine();
            e->addTrack (sine (1000.0, 0.5f, 1.0, 44100.0), "Sine");
            expectWithinAbsoluteError (e->getSongLengthSeconds(), 1.0, 1.0e-6);
            e->play();
            auto out = render (*e, 0.7);   // 0.10.0's test counted past the end of a 0.5 s render (it read the other channel)
            int crossings = 0;
            for (int i = 4800; i < 4800 + 24000; ++i)
                if (out.l[(size_t) i - 1] < 0.0f && out.l[(size_t) i] >= 0.0f) ++crossings;
            expectWithinAbsoluteError (crossings, 500, 1);   // 1 kHz over 0.5 s
            double err = 0.0;
            for (int i = 4800; i < 9600; ++i)
            {
                const double ideal = 0.5 * std::sin (MathConstants<double>::twoPi * 1000.0 * i / kFs);
                err = jmax (err, std::abs (ideal - (double) out.l[(size_t) i]));
            }
            expectLessThan (err, 2.0e-3);
        }

        beginTest ("loop region repeats; playback stops by itself at the song end");
        {
            auto e = makeEngine();
            std::vector<float> ramp ((size_t) kFs * 2);
            for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) i / (float) ramp.size();
            e->addTrack (makeMedia ({ ramp }, kFs), "Ramp");
            e->setLoop (true, 0.5, 0.75);
            e->setPositionSeconds (0.5);
            e->play();
            auto out = render (*e, 1.0, 500);
            for (int i = 0; i < out.size(); ++i)
                if (out.l[(size_t) i] > 0.376f || (i > 2000 && out.l[(size_t) i] < 0.249f)) { expect (false, "outside loop at " + Str (i)); break; }
            expect (e->getPositionSeconds() >= 0.5 && e->getPositionSeconds() < 0.75);

            e->setLoop (false, 0.0, 0.0);
            e->setPositionSeconds (1.8);
            render (*e, 0.5);
            expect (! e->isPlaying(), "did not stop at the end");
            expectWithinAbsoluteError (e->getPositionSeconds(), 2.0, 0.02);
        }

        beginTest ("session round-trip: mixer, names, bus names, inserts with settings, files, loop");
        {
            auto e = makeEngine();
            auto kick = constant (0.1f, 0.1f, 1.0, kFs, "/songs/kick.wav");
            auto vox = constant (0.2f, 0.2f, 3.0, kFs, "/songs/vox.wav");
            const int t0 = e->addTrack (kick, "Kick");
            const int t1 = e->addTrack (vox, "Lead Vox");
            e->setTrackColour (t1, 0xffff0000);
            set (*e, E::Ids::track (t1, "vol"), -3.5f);
            set (*e, E::Ids::track (t0, "out"), 1.0f);
            set (*e, E::Ids::bpm, 93.0f);
            e->setBusName (0, "Drums");
            set (*e->insertPlugin (t1, 2, "elite-vox"), "squeeze", 14.0f);
            e->insertPlugin (E::kMaster, 7, "elite-master");
            e->setLoop (true, 1.0, 2.0);
            const auto tree = e->createSessionTree();

            const auto files = E::mediaFilesIn (tree);
            expect (contains (files, "/songs/kick.wav") && contains (files, "/songs/vox.wav"));

            auto f = makeEngine();
            StringArray missing;
            std::map<std::string, SampleData::Ptr> media { { "/songs/vox.wav", vox } };   // kick is "missing"
            std::map<std::string, SampleData::Ptr> allMedia { { "/songs/vox.wav", vox }, { "/songs/kick.wav", kick } };
            const auto reread = Tree::fromXml (tree.toXml());
            expect (reread.has_value() && f->applySessionTree (*reread, media, &missing));
            expect (contains (missing, "/songs/kick.wav"));
            expect (f->isTrackUsed (t0) && f->isTrackUsed (t1));
            expect (f->getTrackMedia (t0) == nullptr && f->getTrackMedia (t1) == vox);
            {
                // saving again keeps the path of the file that was not found (it can be found next time)
                const auto again = E::mediaFilesIn (f->createSessionTree());
                expect (contains (again, "/songs/kick.wav") && contains (again, "/songs/vox.wav"));
                // once the track gets real audio, the old missing path is gone
                auto g = makeEngine();
                StringArray missing2;
                expect (g->applySessionTree (*reread, media, &missing2));
                g->setTrackMedia (t0, vox);
                const auto relinked = E::mediaFilesIn (g->createSessionTree());
                expect (! contains (relinked, "/songs/kick.wav"));
                g->removeTrack (t0);
                expect (! contains (E::mediaFilesIn (g->createSessionTree()), "/songs/kick.wav"));
            }
            expectEquals (f->getTrackName (t1), std::string ("Lead Vox"));
            expect (f->getTrackColour (t1) == 0xffff0000);
            expectEquals (f->getBusName (0), std::string ("Drums"));
            expectWithinAbsoluteError (mix (*f, E::Ids::track (t1, "vol")), -3.5f, 0.06f);
            expectWithinAbsoluteError (mix (*f, E::Ids::track (t0, "out")), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (mix (*f, E::Ids::bpm), 93.0f, 0.05f);
            auto* v = f->getPlugin (t1, 2);
            expect (v != nullptr && std::string (v->getInfo().id) == "elite-vox");
            if (v != nullptr) expectWithinAbsoluteError (v->getParameter ("squeeze"), 14.0f, 0.06f);
            expect (f->getPlugin (E::kMaster, 7) != nullptr);
            expect (f->isLooping());
            expectWithinAbsoluteError (f->getLoopStart(), 1.0, 1.0e-9);
            expectWithinAbsoluteError (f->getSongLengthSeconds(), 3.0, 1.0e-6);
            expect (! f->applySessionTree (Tree ("NOT_A_SESSION"), {}));

            beginTest ("session round-trip keeps every mixer control and plug-in control exactly");
            // (A brand-new control can sit a hair off its snapped value, as in 0.10.0: Elite Master INPUT starts
            // at -0.00000095 dB and saves as 0 after one load. From the first load on, saving never changes it.)
            auto g = makeEngine();
            expect (g->applySessionTree (*reread, allMedia));
            const auto once = g->createSessionTree();
            auto h = makeEngine();
            expect (h->applySessionTree (*Tree::fromXml (once.toXml()), allMedia));
            expect (h->createSessionTree() == once, "a session read and saved again must not change");
            int differing = 0;
            for (const auto& ins : tree.children())
                if (ins.hasType ("INSERT"))
                    for (const auto& prm : ins.childAt (0).children())
                    {
                        const auto& other = once.childWith ("plugin", ins.get ("plugin")).childAt (0).childWith ("id", prm.get ("id"));
                        if (std::abs (other.getDouble ("value") - prm.getDouble ("value")) > 1.0e-5) ++differing;
                    }
            expectEquals (differing, 0, "plug-in controls that moved more than 0.00001 on the first load");
        }

        beginTest ("new session: Elite Verb on Reverb, Elite Delay on Delay, Elite Master on the master");
        {
            auto e = makeEngine();
            e->addTrack (constant (0.1f, 0.1f, 1.0), "x");
            e->newSession();
            expectEquals (e->getNumUsedTracks(), 0);
            expect (e->getPlugin (E::busStrip (3), 0) != nullptr && std::string (e->getPlugin (E::busStrip (3), 0)->getInfo().id) == "elite-verb");
            expect (e->getPlugin (E::busStrip (4), 0) != nullptr && std::string (e->getPlugin (E::busStrip (4), 0)->getInfo().id) == "elite-delay");
            expect (e->getPlugin (E::kMaster, E::kSlots - 1) != nullptr);
            expectEquals (e->getBusName (3), std::string ("Reverb"));
        }

        beginTest ("garbage: replaced plug-ins / media are freed only after the audio thread moved on");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.1f, 0.1f, 1.0), "x");
            e->insertPlugin (t, 0, "elite-comp");
            e->insertPlugin (t, 0, "elite-eq");     // replaces the comp
            e->setTrackMedia (t, constant (0.2f, 0.2f, 1.0));
            expectEquals (e->pendingGarbage(), 2);
            e->collectGarbage();
            expectEquals (e->pendingGarbage(), 2, "freed before any block finished");
            render (*e, 3.0 * kBlock / kFs);
            e->collectGarbage();
            expectEquals (e->pendingGarbage(), 0);
            e->release();                           // audio stopped: freed at once
            e->insertPlugin (t, 0, "elite-verb");
            e->collectGarbage();
            expectEquals (e->pendingGarbage(), 0);
        }

        beginTest ("full session (32 tracks, inserts everywhere, sends, loop) never allocates on the audio thread");
        {
            auto e = makeEngine();
            e->newSession();
            const StringArray ids { "elite-vox", "elite-eq", "elite-comp", "elite-bass", "elite-channel", "elite-verb", "elite-delay" };
            for (int t = 0; t < E::kMaxTracks; ++t)
            {
                expectEquals (e->addTrack (sine (100.0 + 10.0 * t, 0.05f, 4.0, t % 2 == 0 ? 44100.0 : kFs), "T" + std::to_string (t)), t);
                for (int s = 0; s < 3; ++s) e->insertPlugin (t, s, ids[(size_t) (t + s) % ids.size()]);
                set (*e, E::Ids::track (t, "sa"), -10.0f);
                set (*e, E::Ids::track (t, "sb"), -14.0f);
                set (*e, E::Ids::track (t, "out"), (float) (t % 4));
            }
            expectEquals (e->addTrack (sine (100.0, 0.1f, 1.0, kFs), "one too many"), -1);
            for (int b = 0; b < E::kNumBuses; ++b) e->insertPlugin (E::busStrip (b), 1, "elite-comp");
            e->setLoop (true, 0.25, 0.6);
            e->play();

            std::vector<float> l ((size_t) kBlock * 4), r ((size_t) kBlock * 4);
            float* outs[2] { l.data(), r.data() };
            int allocations = 0;
            bool finite = true;
            for (int i = 0; i < 120; ++i)
            {
                const int n = i % 3 == 0 ? 333 : kBlock;
                if (i == 40) e->setBeforeMode (true);
                if (i == 60) e->setBeforeMode (false);
                if (i == 80) e->setPositionSeconds (0.1);
                hemtest::beginTracking();
                e->process (outs, 2, n);
                allocations += hemtest::endTracking();
                for (int s = 0; s < n; ++s) finite = finite && std::isfinite (l[(size_t) s]) && std::isfinite (r[(size_t) s]);
            }
            expectEquals (allocations, 0, "allocations on the audio thread");
            expect (finite, "NaN / Inf in the mix");

            beginTest ("a host block bigger than prepared, and a mono output, are handled");
            hemtest::beginTracking();
            e->process (outs, 2, kBlock * 3 + 7);
            const int bigAllocs = hemtest::endTracking();   // read first: the message text below allocates
            expectEquals (bigAllocs, 0, "allocations with a block bigger than prepared");
            float* mono[1] { l.data() };
            hemtest::beginTracking();
            e->process (mono, 1, kBlock);
            const int monoAllocs = hemtest::endTracking();
            expectEquals (monoAllocs, 0, "allocations with a mono output");
            expect (std::isfinite (r[(size_t) kBlock * 3]) && std::isfinite (l[10]));
        }
    }
};
static MixerTests mixerTests;

//==============================================================================
class LatencyTests final : public hemtest::UnitTest
{
public:
    LatencyTests() : UnitTest ("Delay Compensation", "Playground") {}

    static SampleData::Ptr click (int at, double seconds)
    {
        const auto n = (size_t) (seconds * 48000.0);
        std::vector<float> b (n, 0.0f);
        b[(size_t) at] = 0.5f;
        return makeMedia ({ b, b }, 48000.0);
    }

    static int peakIndex (const Stereo& b, float& peak)
    {
        int at = -1; peak = 0.0f;
        for (int i = 0; i < b.size(); ++i)
            if (std::abs (b.l[(size_t) i]) > peak) { peak = std::abs (b.l[(size_t) i]); at = i; }
        return at;
    }

    void runTest() override
    {
        beginTest ("a latent plug-in on one track: both tracks still line up (one clean double-height click)");
        {
            E e;
            e.prepare (48000.0, 512);
            const int a = e.addTrack (click (4800, 1.0), "A");
            e.addTrack (click (4800, 1.0), "B");
            auto* sw = e.insertPlugin (a, 0, "elite-swirl");
            sw->parameters().find ("amount")->setNormalised (0.0f);
            expectEquals (e.getTotalLatencySamples(), 2048);
            e.play();
            auto out = render (e, 0.5);
            float pk = 0.0f;
            const int at = peakIndex (out, pk);
            expectEquals (at, 4800 + 2048);
            expectWithinAbsoluteError (pk, 1.0f, 2.0e-3f);   // 0.5 + 0.5 on the same sample
        }

        beginTest ("latent plug-in on a bus: direct tracks wait for the bus path");
        {
            E e;
            e.prepare (48000.0, 512);
            const int a = e.addTrack (click (4800, 1.0), "A");   // to master
            const int b = e.addTrack (click (4800, 1.0), "B");   // to Bus 1
            set (e, E::Ids::track (b, "out"), 1.0f);
            auto* sw = e.insertPlugin (E::busStrip (0), 0, "elite-swirl");
            sw->parameters().find ("amount")->setNormalised (0.0f);
            e.play();
            auto out = render (e, 0.5);
            float pk = 0.0f;
            const int at = peakIndex (out, pk);
            expectEquals (at, 4800 + 2048);
            expectWithinAbsoluteError (pk, 1.0f, 2.0e-3f);
            expectEquals (e.getTotalLatencySamples(), 2048);

            beginTest ("track + bus + master latencies add up; removing the plug-in brings it back to 0");
            e.insertPlugin (a, 0, "elite-swirl");
            e.insertPlugin (E::kMaster, 7, "elite-master");
            expectEquals (e.getTotalLatencySamples(), 2048 + 2048 + e.getMasterLatencySamples());
            e.removePlugin (a, 0);
            e.removePlugin (E::busStrip (0), 0);
            e.removePlugin (E::kMaster, 7);
            expectEquals (e.getTotalLatencySamples(), 0);
        }
    }
};
static LatencyTests latencyTests;

//==============================================================================
class SincResamplerTests final : public hemtest::UnitTest
{
public:
    SincResamplerTests() : UnitTest ("Sinc resampler", "Playground") {}

    static double toneError (std::uint32_t inRate, std::uint32_t outRate, double hz)
    {
        const int n = (int) inRate;   // one second
        std::vector<float> in ((size_t) n);
        for (int i = 0; i < n; ++i) in[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * hz * i / inRate);
        SincResampler rs (inRate, outRate);
        std::vector<float> out ((size_t) rs.outputLength ((std::uint64_t) n));
        rs.process (in.data(), (std::uint64_t) n, out.data());
        double err = 0.0;
        for (size_t o = (size_t) outRate / 10; o < out.size() - (size_t) outRate / 10; ++o)
            err = std::max (err, std::abs ((double) out[o] - std::sin (2.0 * MathConstants<double>::pi * hz * (double) o / outRate)));
        return err;
    }

    void runTest() override
    {
        beginTest ("44.1 kHz -> 48 kHz: tones come out exactly where they should (better than -80 dB)");
        for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
            expectLessThan (toneError (44100, 48000, hz), 1.0e-4, Str (hz) + " Hz");

        beginTest ("48 kHz -> 44.1 kHz and 96 kHz -> 48 kHz keep the audio clean");
        expectLessThan (toneError (48000, 44100, 1000.0), 1.0e-4);
        expectLessThan (toneError (96000, 48000, 5000.0), 1.0e-4);

        beginTest ("Going down a rate, sound above the new limit is removed (no aliasing)");
        {
            const int n = 96000;
            std::vector<float> in ((size_t) n);
            for (int i = 0; i < n; ++i) in[(size_t) i] = (float) std::sin (2.0 * MathConstants<double>::pi * 30000.0 * i / 96000.0);
            SincResampler rs (96000, 48000);
            std::vector<float> out ((size_t) rs.outputLength ((std::uint64_t) n));
            rs.process (in.data(), (std::uint64_t) n, out.data());
            double peak = 0.0;
            for (size_t o = 4800; o < out.size() - 4800; ++o) peak = std::max (peak, (double) std::abs (out[o]));
            expectLessThan (peak, 1.0e-4);
        }

        beginTest ("Same rate is a straight copy; length is exact");
        SincResampler same (48000, 48000);
        expect (same.passthrough());
        SincResampler up (44100, 48000);
        expectEquals ((long long) up.outputLength (44100), (long long) 48000);
    }
};
static SincResamplerTests sincResamplerTests;

//==============================================================================
class SessionExportTests final : public hemtest::UnitTest
{
public:
    SessionExportTests() : UnitTest ("Session + Export", "Playground") {}

    void runTest() override
    {
        const auto dir = tempDir ("session");
        std::string err;

        beginTest ("decode WAV (mono and stereo, native rate); clear errors for bad files");
        {
            const auto f = dir / "stems" / "kick.wav";
            expect (writeWav (f, { std::vector<float> (44100, 0.25f) }, 44100.0));
            const double savedTarget = MediaLoader::getTargetRate();
            MediaLoader::setTargetRate (0.0);   // keep native rate
            auto s = MediaLoader::decode (f, err);
            expect (s != nullptr, err);
            if (s != nullptr)
            {
                expectEquals (s->getNumFrames(), 44100);
                expectEquals (s->getSourceRate(), 44100.0);
                expectWithinAbsoluteError (s->channel (0)[1000], 0.25f, 1.0e-5f);
                expect (s->getFile() == f);
            }
            MediaLoader::setTargetRate (48000.0);   // engine at 48 kHz: converted once, on load
            auto c = MediaLoader::decode (f, err);
            expect (c != nullptr, err);
            if (c != nullptr)
            {
                expectEquals (c->getNumFrames(), 48000);
                expectEquals (c->getSourceRate(), 48000.0);
                expectWithinAbsoluteError (c->channel (0)[24000], 0.25f, 1.0e-4f);
            }
            MediaLoader::setTargetRate (savedTarget);
            const auto junk = dir / "notes.wav";
            { std::ofstream (junk) << "not audio"; }
            expect (MediaLoader::decode (junk, err) == nullptr && ! err.empty());
            expect (MediaLoader::decode (dir / "missing.wav", err) == nullptr && err.find ("not found") != std::string::npos);
            expect (MediaLoader::isAudioFile ("/x/a.WAV") && ! MediaLoader::isAudioFile ("/x/a.txt"));
            std::vector<std::string> failed;
            auto all = MediaLoader::decodeAll ({ SessionFile::toText (f), SessionFile::toText (f), SessionFile::toText (junk) }, failed);
            expectEquals ((int) all.size(), 1);
            expectEquals ((int) failed.size(), 1);
        }

        beginTest ("session file: save, read back, and find the audio after the song folder moved");
        {
            const auto song = dir / "My Song";
            const auto kick = song / "stems" / "kick.wav";
            expect (writeWav (kick, { std::vector<float> (48000, 0.1f), std::vector<float> (48000, 0.1f) }, 48000.0));
            E e;
            e.prepare (48000.0, 512);
            const int t = e.addTrack (MediaLoader::decode (kick, err), "Kick");
            e.setBusName (2, "Keys");
            const auto sessionFile = song / "My Song.hemplay";
            expect (SessionFile::save (e, sessionFile, err), err);
            expect (fileExists (sessionFile));

            const auto moved = dir / "Moved Song";
            std::error_code ec;
            std::filesystem::rename (song, moved, ec);
            expect (! ec, "moving the song folder");
            auto tree = SessionFile::read (moved / "My Song.hemplay", err);
            expect (tree.has_value(), err);
            if (tree)
            {
                const auto files = E::mediaFilesIn (*tree);
                expectEquals ((int) files.size(), 1);
                if (! files.empty())
                    expect (SessionFile::toPath (files[0]) == (moved / "stems" / "kick.wav").lexically_normal(), files[0]);

                std::vector<std::string> failed;
                auto media = MediaLoader::decodeAll (files, failed);
                E f;
                f.prepare (48000.0, 512);
                StringArray missing;
                expect (f.applySessionTree (*tree, media, &missing));
                expect (missing.empty() && f.getTrackMedia (t) != nullptr);
                expectEquals (f.getBusName (2), std::string ("Keys"));
            }

            const auto bad = dir / "bad.hemplay";
            { std::ofstream (bad) << "<NOPE/>"; }
            expect (! SessionFile::read (bad, err).has_value() && ! err.empty());
        }

        beginTest ("export: exact length (song + tail), latency trimmed, ceiling respected, first sample intact");
        {
            E e;
            e.prepare (44100.0, 256);
            std::vector<float> src (44100);
            for (int i = 0; i < 44100; ++i) src[(size_t) i] = (i % 4410 == 0) ? 0.9f : 0.3f * (float) std::sin (0.05 * i);   // spikes every 100 ms
            e.addTrack (makeMedia ({ src, src }, 44100.0), "Beat");
            set (*e.insertPlugin (E::kMaster, 7, "elite-master"), "input", 9.0f);
            const auto session = e.createSessionTree();

            ExportOptions o;
            o.file = dir / "export" / "mix.wav";
            o.sampleRate = 48000.0;
            o.bitDepth = 24;
            o.tailSeconds = 0.5;
            OfflineExport ex (session, {}, o);
            // the track's file is not in the media map: nothing to export
            expect (! ex.render ({}, {}, err) && ! err.empty());

            const auto stem = dir / "export" / "beat.wav";
            expect (writeWav (stem, { src, src }, 44100.0, 32));
            E e2;
            e2.prepare (44100.0, 256);
            e2.addTrack (MediaLoader::decode (stem, err), "Beat");
            set (*e2.insertPlugin (E::kMaster, 7, "elite-master"), "input", 9.0f);
            std::vector<std::string> failed;
            const auto session2 = e2.createSessionTree();
            auto media = MediaLoader::decodeAll (E::mediaFilesIn (session2), failed);
            OfflineExport ex2 (session2, media, o);
            double lastProgress = 0.0;
            expect (ex2.render ([&] (double p) { lastProgress = p; }, {}, err), err);
            expectWithinAbsoluteError (lastProgress, 1.0, 1.0e-9);

            double rate = 0.0;
            auto out = readWav (o.file, rate);
            expectEquals (rate, 48000.0);
            expectEquals (out.size(), 48000 + 24000);
            if (out.size() == 72000)
            {
                expectLessThan (out.magnitude(), params::dbToGain (-1.0f) + 1.0e-3f);
                expectLessThan (ex2.getPeak(), params::dbToGain (-1.0f) + 1.0e-3f);
                int firstLoud = -1;
                for (int i = 0; i < 200; ++i) if (std::abs (out.l[(size_t) i]) > 0.5f) { firstLoud = i; break; }
                expect (firstLoud >= 0 && firstLoud < 8, "first transient at " + Str (firstLoud));
                expectLessThan (out.magnitude (48000 + 2000, 20000), 1.0e-3f);
            }

            beginTest ("export: 16-bit, BEFORE version, cancel leaves no file");
            ExportOptions o16 = o;
            o16.file = dir / "export" / "mix16.wav";
            o16.bitDepth = 16;
            o16.beforeMode = true;   // limiter bypassed: the +9 dB input is not applied
            OfflineExport ex3 (session2, media, o16);
            expect (ex3.render ({}, {}, err), err);
            auto out16 = readWav (o16.file, rate);
            expectEquals (out16.size(), 72000);
            if (out16.size() > 0) expectWithinAbsoluteError (out16.magnitude(), 0.9f, 0.02f);

            beginTest ("export: CLEAN skips the master inserts and keeps peaks at -3 dBFS (turned down, never up)");
            ExportOptions ocl = o;
            ocl.file = dir / "export" / "clean.wav";
            ocl.clean = true;
            OfflineExport ex5 (session2, media, ocl);
            expect (ex5.render ({}, {}, err), err);
            auto clean = readWav (ocl.file, rate);
            expectEquals (clean.size(), 72000);
            const float limit = params::dbToGain (ExportOptions::kCleanPeakDb);
            if (clean.size() == 72000 && out16.size() == 72000)
            {
                expectWithinAbsoluteError (clean.magnitude(), limit, 0.003f);
                expectWithinAbsoluteError (ex5.getCleanGain(), limit / 0.9f, 0.01f, "the 0.9 spikes set the gain");
                expectWithinAbsoluteError (clean.rms (1000, 3000) / jmax (1.0e-6f, out16.rms (1000, 3000)), limit / 0.9f, 0.02f);
            }
            {
                // a quiet mix is left alone
                E e3;
                e3.prepare (44100.0, 256);
                const auto quietFile = dir / "export" / "quiet.wav";
                expect (writeWav (quietFile, { std::vector<float> (22050, 0.1f), std::vector<float> (22050, 0.1f) }, 44100.0, 32));
                e3.addTrack (MediaLoader::decode (quietFile, err), "Quiet");
                e3.insertPlugin (E::kMaster, 7, "elite-master");
                const auto session3 = e3.createSessionTree();
                auto media3 = MediaLoader::decodeAll (E::mediaFilesIn (session3), failed);
                ExportOptions oq = ocl;
                oq.file = dir / "export" / "quiet-clean.wav";
                OfflineExport ex6 (session3, media3, oq);
                expect (ex6.render ({}, {}, err), err);
                expectWithinAbsoluteError (ex6.getCleanGain(), 1.0f, 1.0e-6f);
                auto qOut = readWav (oq.file, rate);
                if (qOut.size() > 10000) expectWithinAbsoluteError (qOut.l[10000], 0.1f, 0.002f);
            }

            beginTest ("export: MP3 gets exactly the 16-bit samples (0.10.0 made them 0.0003 dB quieter)");
            {
                std::vector<std::int16_t> in;
                for (int v = -32768; v <= 32767; ++v) { in.push_back ((std::int16_t) v); in.push_back ((std::int16_t) -std::max (v, -32767)); in.push_back (7); }
                const int frames = (int) in.size() / 3;
                std::vector<std::int16_t> now ((size_t) frames * 2), old ((size_t) frames * 2);
                Mp3Encoder::toEncoderSamples (in.data(), frames, 3, 2, now.data(), false);
                Mp3Encoder::toEncoderSamples (in.data(), frames, 3, 2, old.data(), true);
                int same = 0, moved = 0;
                for (int i = 0; i < frames; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        same += now[(size_t) (i * 2 + ch)] == in[(size_t) (i * 3 + ch)] ? 1 : 0;
                        moved += old[(size_t) (i * 2 + ch)] != in[(size_t) (i * 3 + ch)] ? 1 : 0;
                    }
                expectEquals (same, frames * 2);          // every value, both channels, third channel left out
                expect (moved > 0, "the old way should move some samples");
                expect (! Mp3Encoder::legacySamplesForCheck(), "the app must encode the new way");
            }

#if defined(_WIN32)
            beginTest ("export: MP3 (Windows' encoder): right length, right level, about 320 kbps, no WAV left behind");
            {
                ExportOptions om = o;
                om.file = dir / "export" / "mix.mp3";
                om.mp3Kbps = 320;
                OfflineExport exm (session2, media, om);
                double lastP = 0.0;
                const bool ok = exm.render ([&] (double p) { lastP = p; }, {}, err);
                expect (ok, "MP3 export failed: " + err);
                if (ok)
                {
                    const auto mp3 = exm.getTargetFile();
                    expect (fileExists (mp3));
                    expectWithinAbsoluteError (lastP, 1.0, 1.0e-9);
                    expect (! fileExists (dir / "export" / "mix (making mp3).wav"), "temporary WAV left behind");
                    std::error_code ec;
                    const auto bytes = (long long) std::filesystem::file_size (mp3, ec);
                    expect (bytes > 45000 && bytes < 90000, "MP3 size " + Str (bytes));
                    const double savedTarget = MediaLoader::getTargetRate();
                    MediaLoader::setTargetRate (0.0);
                    auto back = MediaLoader::decode (mp3, err);
                    MediaLoader::setTargetRate (savedTarget);
                    expect (back != nullptr, "the MP3 cannot be read back: " + err);
                    if (back != nullptr)
                    {
                        expectEquals (back->getSourceRate(), 48000.0);
                        expectWithinAbsoluteError ((double) back->getNumFrames() / back->getSourceRate(), 1.5, 0.1, "MP3 length");
                        double sw = 0.0, sm = 0.0;
                        for (int i = 0; i < out.size(); ++i) sw += (double) out.l[(size_t) i] * out.l[(size_t) i];
                        for (int i = 0; i < back->getNumFrames(); ++i) sm += (double) back->channel (0)[i] * back->channel (0)[i];
                        const double wavRms = std::sqrt (sw / jmax (1, out.size())), mp3Rms = std::sqrt (sm / jmax (1, back->getNumFrames()));
                        expectWithinAbsoluteError (20.0 * std::log10 (mp3Rms), 20.0 * std::log10 (wavRms), 0.5, "MP3 level vs WAV");
                    }
                }
                ExportOptions om96 = om;
                om96.sampleRate = 96000.0;
                om96.file = dir / "export" / "mix96.mp3";
                OfflineExport exb (session2, media, om96);
                expect (! exb.render ({}, {}, err) && err.find ("44.1 or 48") != std::string::npos, "96 kHz MP3 must be refused with a clear message");
            }
#endif

            ExportOptions oc = o;
            oc.file = dir / "export" / "cancelled.wav";
            OfflineExport ex4 (session2, media, oc);
            int calls = 0;
            expect (! ex4.render ({}, [&] { return ++calls > 3; }, err));
            expect (! fileExists (oc.file));
        }

        std::error_code ec;
        std::filesystem::remove_all (dir, ec);
    }
};
static SessionExportTests sessionExportTests;
} // namespace hem::pg::test
