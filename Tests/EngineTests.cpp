// Mixer engine tests: routing, faders, pan, mute/solo, buses, sends, inserts, BEFORE/AFTER,
// transport (loop, stop at end), sample-rate conversion, session round-trip, garbage collection
// and zero allocation on the audio thread.

#include "Engine/PlaygroundEngine.h"
#include "Session/UndoHistory.h"
#include "Analysis/Waterfall.h"
#include "Engine/MediaLoader.h"
#include <juce_core/juce_core.h>

namespace hemtest { void beginTracking(); int endTracking(); }

namespace pg::test
{
namespace
{
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;
    using E = PlaygroundEngine;

    SampleData::Ptr constant (float l, float r, double seconds, double rate = kFs, const juce::String& path = {})
    {
        const int n = (int) (seconds * rate);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { b.setSample (0, i, l); b.setSample (1, i, r); }
        return new SampleData (std::move (b), rate, path.isEmpty() ? juce::File() : juce::File (path));
    }

    SampleData::Ptr sine (double hz, float amp, double seconds, double rate)
    {
        const int n = (int) (seconds * rate);
        juce::AudioBuffer<float> b (1, n);
        for (int i = 0; i < n; ++i) b.setSample (0, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
        return new SampleData (std::move (b), rate, {});
    }

    template <typename Proc>
    void set (Proc& e, const juce::String& id, float value)
    {
        auto* p = e.getAPVTS().getParameter (id);
        jassert (p != nullptr);
        if (p != nullptr) p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** Renders `seconds` and returns the stereo output. */
    juce::AudioBuffer<float> render (E& e, double seconds, int block = kBlock)
    {
        const int total = (int) (seconds * e.getEngineSampleRate());
        juce::AudioBuffer<float> out (2, total);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int start = 0; start < total; start += block)
        {
            const int n = juce::jmin (block, total - start);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            e.processBlock (view, midi);
            out.copyFrom (0, start, buf, 0, 0, n);
            out.copyFrom (1, start, buf, 1, 0, n);
        }
        return out;
    }

    float last (const juce::AudioBuffer<float>& b, int ch) { return b.getSample (ch, b.getNumSamples() - 1); }

    std::unique_ptr<E> makeEngine()
    {
        auto e = std::make_unique<E>();
        e->prepareToPlay (kFs, kBlock);
        return e;
    }
}

class EngineTests final : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("Mixer Engine", "Playground") {}

    void runTest() override
    {
        beginTest ("a track plays at unity through the master; stopped = silence");
        {
            auto e = makeEngine();
            const int t = e->addTrack (constant (0.5f, 0.25f, 2.0), "Kick");
            expectEquals (t, 0);
            expectEquals (e->getTrackName (t), juce::String ("Kick"));
            auto silent = render (*e, 0.1);
            expectEquals (silent.getMagnitude (0, silent.getNumSamples()), 0.0f);
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
            for (int i = 1; i < out.getNumSamples(); ++i) maxStep = juce::jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
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
            const float pk = out.getMagnitude (0, (int) kFs / 2, out.getNumSamples() - (int) kFs / 2);
            expectLessThan (pk, params::dbToGain (-1.0f) + 1.0e-3f, "master fader pushed the export past Elite Master's ceiling");
            expectGreaterThan (pk, params::dbToGain (-1.5f), "the limiter should be working right up at the ceiling");
            expect (e->getPeak (E::kMaster, 0) <= params::dbToGain (-1.0f) + 1.0e-3f, "master meter shows the limited level");
        }

        beginTest ("master tap: the 3D meter gets the master output (mono sum) only while it is switched on");
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
            juce::AudioBuffer<float> buf (2, kBlock);
            juce::MidiBuffer midi;
            int allocations = 0;
            for (int i = 0; i < 40; ++i)
            {
                hemtest::beginTracking();
                e->processBlock (buf, midi);
                allocations += hemtest::endTracking();
            }
            expectEquals (allocations, 0, "the tap must not allocate on the audio thread");
        }

        beginTest ("undo / redo: a fader move, then a removed track comes back with its audio");
        {
            auto e = makeEngine();
            const auto kickPath = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pg-undo-kick.wav").getFullPathName();
            const int t = e->addTrack (constant (0.5f, 0.5f, 2.0, kFs, kickPath), "Kick");
            UndoHistory h (*e, 2);
            h.reset();
            expect (! h.canUndo());
            set (*e, E::Ids::track (t, "vol"), -6.0f);
            for (int i = 0; i < 3; ++i) h.poll();
            expect (h.canUndo(), "a settled fader move is one undo step");
            expect (h.undo());
            expect (! h.lastRestoreWasStructural());
            expectWithinAbsoluteError (e->getAPVTS().getRawParameterValue (E::Ids::track (t, "vol"))->load(), 0.0f, 0.01f);
            expect (h.redo());
            expectWithinAbsoluteError (e->getAPVTS().getRawParameterValue (E::Ids::track (t, "vol"))->load(), -6.0f, 0.01f);

            e->removeTrack (t);
            for (int i = 0; i < 3; ++i) h.poll();
            expect (! e->isTrackUsed (t));
            expect (h.undo());
            expect (h.lastRestoreWasStructural());
            expect (e->isTrackUsed (t), "undo brings the removed track back");
            expect (e->getTrackMedia (t) != nullptr, "... with its audio, without reading the disk");
            expectEquals (e->getTrackName (t), juce::String ("Kick"));
            expectWithinAbsoluteError (e->getAPVTS().getRawParameterValue (E::Ids::track (t, "vol"))->load(), -6.0f, 0.01f);
            h.undo();   // one more step back (fader to 0 dB) must also work
            expectWithinAbsoluteError (e->getAPVTS().getRawParameterValue (E::Ids::track (t, "vol"))->load(), 0.0f, 0.01f);
        }

        beginTest ("3D spectrum: a full-scale 1 kHz sine peaks at about 0 dB at 1 kHz, far below it elsewhere");
        {
            analysis::Waterfall w;
            w.prepare (kFs);
            std::vector<float> x ((size_t) kFs);
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) i / kFs);
            w.add (x.data(), (int) x.size());
            expectGreaterThan ((int) w.rowsMade(), 20);
            const float* r = w.row (0);
            int best = 0;
            for (int i = 1; i < analysis::Waterfall::kPoints; ++i) if (r[i] > r[best]) best = i;
            expectWithinAbsoluteError (analysis::Waterfall::pointHz (best), 1000.0f, 70.0f);
            expectWithinAbsoluteError (r[best], 0.0f, 1.5f);
            int far = 0;
            for (int i = 0; i < analysis::Waterfall::kPoints; ++i) if (analysis::Waterfall::pointHz (i) < 300.0f && r[i] > -60.0f) ++far;
            expectEquals (far, 0, "energy below 300 Hz from a 1 kHz tone");
        }

        beginTest ("import: MP3 is in the open-file list");
        expect (MediaLoader::wildcard().containsIgnoreCase ("*.mp3"), MediaLoader::wildcard());

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
            for (int i = 1; i < out.getNumSamples(); ++i) maxStep = juce::jmax (maxStep, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
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
            auto out = render (*e, 0.5);
            int crossings = 0;
            for (int i = 4800; i < 4800 + 24000; ++i)
                if (out.getSample (0, i - 1) < 0.0f && out.getSample (0, i) >= 0.0f) ++crossings;
            expectWithinAbsoluteError (crossings, 500, 1);   // 1 kHz over 0.5 s
            // interpolation quality: no sample far off the ideal sine
            double err = 0.0;
            for (int i = 4800; i < 9600; ++i)
            {
                const double ideal = 0.5 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / kFs);
                err = juce::jmax (err, std::abs (ideal - (double) out.getSample (0, i)));
            }
            expectLessThan (err, 2.0e-3);
        }

        beginTest ("loop region repeats; playback stops by itself at the song end");
        {
            auto e = makeEngine();
            juce::AudioBuffer<float> ramp (1, (int) kFs * 2);
            for (int i = 0; i < ramp.getNumSamples(); ++i) ramp.setSample (0, i, (float) i / (float) ramp.getNumSamples());
            e->addTrack (new SampleData (std::move (ramp), kFs, {}), "Ramp");
            e->setLoop (true, 0.5, 0.75);
            e->setPositionSeconds (0.5);
            e->play();
            auto out = render (*e, 1.0, 500);
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (out.getSample (0, i) > 0.376f || (i > 2000 && out.getSample (0, i) < 0.249f)) { expect (false, "outside loop at " + juce::String (i)); break; }
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
            e->setTrackColour (t1, juce::Colours::red);
            set (*e, E::Ids::track (t1, "vol"), -3.5f);
            set (*e, E::Ids::track (t0, "out"), 1.0f);
            set (*e, E::Ids::bpm, 93.0f);
            e->setBusName (0, "Drums");
            set (*e->insertPlugin (t1, 2, "elite-vox"), "squeeze", 14.0f);
            e->insertPlugin (E::kMaster, 7, "elite-master");
            e->setLoop (true, 1.0, 2.0);
            const auto tree = e->createSessionTree();

            const auto files = E::mediaFilesIn (tree);
            expect (files.contains (juce::File ("/songs/kick.wav").getFullPathName()) && files.contains (juce::File ("/songs/vox.wav").getFullPathName()));

            auto f = makeEngine();
            juce::StringArray missing;
            std::map<juce::String, SampleData::Ptr> media { { juce::File ("/songs/vox.wav").getFullPathName(), vox } };   // kick is "missing"
            expect (f->applySessionTree (juce::ValueTree::fromXml (*tree.createXml()), media, &missing));
            expect (missing.contains (juce::File ("/songs/kick.wav").getFullPathName()));
            expect (f->isTrackUsed (t0) && f->isTrackUsed (t1));
            expect (f->getTrackMedia (t0) == nullptr && f->getTrackMedia (t1) == vox);
            expectEquals (f->getTrackName (t1), juce::String ("Lead Vox"));
            expect (f->getTrackColour (t1) == juce::Colours::red);
            expectEquals (f->getBusName (0), juce::String ("Drums"));
            expectWithinAbsoluteError (f->getAPVTS().getRawParameterValue (E::Ids::track (t1, "vol"))->load(), -3.5f, 0.06f);
            expectWithinAbsoluteError (f->getAPVTS().getRawParameterValue (E::Ids::track (t0, "out"))->load(), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (f->getAPVTS().getRawParameterValue (E::Ids::bpm)->load(), 93.0f, 0.05f);
            auto* v = f->getPlugin (t1, 2);
            expect (v != nullptr && juce::String (v->getInfo().id) == "elite-vox");
            if (v != nullptr) expectWithinAbsoluteError (v->getAPVTS().getRawParameterValue ("squeeze")->load(), 14.0f, 0.06f);
            expect (f->getPlugin (E::kMaster, 7) != nullptr);
            expect (f->isLooping());
            expectWithinAbsoluteError (f->getLoopStart(), 1.0, 1.0e-9);
            expectWithinAbsoluteError (f->getSongLengthSeconds(), 3.0, 1.0e-6);
            expect (! f->applySessionTree (juce::ValueTree ("NOT_A_SESSION"), {}));
        }

        beginTest ("new session: Elite Verb on Reverb, Elite Delay on Delay, Elite Master on the master");
        {
            auto e = makeEngine();
            e->addTrack (constant (0.1f, 0.1f, 1.0), "x");
            e->newSession();
            expectEquals (e->getNumUsedTracks(), 0);
            expect (e->getPlugin (E::busStrip (3), 0) != nullptr && juce::String (e->getPlugin (E::busStrip (3), 0)->getInfo().id) == "elite-verb");
            expect (e->getPlugin (E::busStrip (4), 0) != nullptr && juce::String (e->getPlugin (E::busStrip (4), 0)->getInfo().id) == "elite-delay");
            expect (e->getPlugin (E::kMaster, E::kSlots - 1) != nullptr);
            expectEquals (e->getBusName (3), juce::String ("Reverb"));
        }

        beginTest ("garbage: replaced plugins / media are freed only after the audio thread moved on");
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
            e->releaseResources();                  // audio stopped: freed at once
            e->insertPlugin (t, 0, "elite-verb");
            e->collectGarbage();
            expectEquals (e->pendingGarbage(), 0);
        }

        beginTest ("full session (32 tracks, inserts everywhere, sends, loop) never allocates on the audio thread");
        {
            auto e = makeEngine();
            e->newSession();
            const juce::StringArray ids { "elite-vox", "elite-eq", "elite-comp", "elite-bass", "elite-channel", "elite-verb", "elite-delay" };
            for (int t = 0; t < E::kMaxTracks; ++t)
            {
                expectEquals (e->addTrack (sine (100.0 + 10.0 * t, 0.05f, 4.0, t % 2 == 0 ? 44100.0 : kFs), "T" + juce::String (t)), t);
                for (int s = 0; s < 3; ++s) e->insertPlugin (t, s, ids[(t + s) % ids.size()]);
                set (*e, E::Ids::track (t, "sa"), -10.0f);
                set (*e, E::Ids::track (t, "sb"), -14.0f);
                set (*e, E::Ids::track (t, "out"), (float) (t % 4));
            }
            expectEquals (e->addTrack (sine (100.0, 0.1f, 1.0, kFs), "one too many"), -1);
            for (int b = 0; b < E::kNumBuses; ++b) e->insertPlugin (E::busStrip (b), 1, "elite-comp");
            e->setLoop (true, 0.25, 0.6);
            e->play();

            juce::AudioBuffer<float> buf (2, kBlock);
            juce::MidiBuffer midi;
            int allocations = 0;
            bool finite = true;
            for (int i = 0; i < 120; ++i)
            {
                const int n = i % 3 == 0 ? 333 : kBlock;
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
                if (i == 40) e->setBeforeMode (true);
                if (i == 60) e->setBeforeMode (false);
                if (i == 80) e->setPositionSeconds (0.1);
                hemtest::beginTracking();
                e->processBlock (view, midi);
                allocations += hemtest::endTracking();
                for (int s = 0; s < n; ++s) finite = finite && std::isfinite (buf.getSample (0, s)) && std::isfinite (buf.getSample (1, s));
            }
            expectEquals (allocations, 0, "allocations on the audio thread");
            expect (finite, "NaN / Inf in the mix");

            beginTest ("a host block bigger than prepared, and a mono output, are handled");
            juce::AudioBuffer<float> big (2, kBlock * 3 + 7);
            e->processBlock (big, midi);
            juce::AudioBuffer<float> mono (1, kBlock);
            e->processBlock (mono, midi);
            expect (std::isfinite (big.getSample (0, kBlock * 3)) && std::isfinite (mono.getSample (0, 10)));
        }
    }
};

static EngineTests engineTests;
} // namespace pg::test

namespace pg::test
{
class LatencyTests final : public juce::UnitTest
{
public:
    LatencyTests() : juce::UnitTest ("Delay Compensation", "Playground") {}

    static SampleData::Ptr click (int at, double seconds)
    {
        const int n = (int) (seconds * 48000.0);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        b.setSample (0, at, 0.5f); b.setSample (1, at, 0.5f);
        return new SampleData (std::move (b), 48000.0, {});
    }

    static int peakIndex (const juce::AudioBuffer<float>& b, float& peak)
    {
        int at = -1; peak = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::abs (b.getSample (0, i)) > peak) { peak = std::abs (b.getSample (0, i)); at = i; }
        return at;
    }

    juce::AudioBuffer<float> renderPlay (PlaygroundEngine& e, double seconds)
    {
        e.play();
        const int total = (int) (seconds * 48000.0);
        juce::AudioBuffer<float> out (2, total), buf (2, 512);
        juce::MidiBuffer midi;
        for (int s = 0; s < total; s += 512)
        {
            const int n = juce::jmin (512, total - s);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            e.processBlock (view, midi);
            out.copyFrom (0, s, buf, 0, 0, n);
            out.copyFrom (1, s, buf, 1, 0, n);
        }
        return out;
    }

    void runTest() override
    {
        beginTest ("a latent plugin on one track: both tracks still line up (one clean double-height click)");
        {
            PlaygroundEngine e;
            e.prepareToPlay (48000.0, 512);
            const int a = e.addTrack (click (4800, 1.0), "A");
            e.addTrack (click (4800, 1.0), "B");
            auto* sw = e.insertPlugin (a, 0, "elite-swirl");
            sw->getAPVTS().getParameter ("amount")->setValueNotifyingHost (0.0f);
            expectEquals (e.getTotalLatencySamples(), 2048);
            auto out = renderPlay (e, 0.5);
            float pk = 0.0f;
            const int at = peakIndex (out, pk);
            expectEquals (at, 4800 + 2048);
            expectWithinAbsoluteError (pk, 1.0f, 2.0e-3f);   // 0.5 + 0.5 on the same sample
        }

        beginTest ("latent plugin on a bus: direct tracks wait for the bus path");
        {
            PlaygroundEngine e;
            e.prepareToPlay (48000.0, 512);
            const int a = e.addTrack (click (4800, 1.0), "A");   // to master
            const int b = e.addTrack (click (4800, 1.0), "B");   // to Bus 1
            auto* p = e.getAPVTS().getParameter (PlaygroundEngine::Ids::track (b, "out"));
            p->setValueNotifyingHost (p->convertTo0to1 (1.0f));
            auto* sw = e.insertPlugin (PlaygroundEngine::busStrip (0), 0, "elite-swirl");
            sw->getAPVTS().getParameter ("amount")->setValueNotifyingHost (0.0f);
            juce::ignoreUnused (a);
            auto out = renderPlay (e, 0.5);
            float pk = 0.0f;
            const int at = peakIndex (out, pk);
            expectEquals (at, 4800 + 2048);
            expectWithinAbsoluteError (pk, 1.0f, 2.0e-3f);
            expectEquals (e.getTotalLatencySamples(), 2048);

            beginTest ("track + bus + master latencies add up; removing the plugin brings it back to 0");
            e.insertPlugin (a, 0, "elite-swirl");
            e.insertPlugin (PlaygroundEngine::kMaster, 7, "elite-master");
            expectEquals (e.getTotalLatencySamples(), 2048 + 2048 + e.getMasterLatencySamples());
            e.removePlugin (a, 0);
            e.removePlugin (PlaygroundEngine::busStrip (0), 0);
            e.removePlugin (PlaygroundEngine::kMaster, 7);
            expectEquals (e.getTotalLatencySamples(), 0);
        }
    }
};

static LatencyTests latencyTests;
} // namespace pg::test

//==============================================================================
#include "Engine/SincResampler.h"

class SincResamplerTests final : public juce::UnitTest
{
public:
    SincResamplerTests() : juce::UnitTest ("Sinc resampler (from HybridCore)", "Playground") {}

    static double toneError (std::uint32_t inRate, std::uint32_t outRate, double hz)
    {
        const int n = (int) inRate;   // one second
        std::vector<float> in ((size_t) n);
        for (int i = 0; i < n; ++i) in[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / inRate);
        pg::SincResampler rs (inRate, outRate);
        std::vector<float> out ((size_t) rs.outputLength ((std::uint64_t) n));
        rs.process (in.data(), (std::uint64_t) n, out.data());
        double err = 0.0;
        for (size_t o = (size_t) outRate / 10; o < out.size() - (size_t) outRate / 10; ++o)   // skip the file edges
            err = std::max (err, std::abs ((double) out[o] - std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) o / outRate)));
        return err;
    }

    void runTest() override
    {
        beginTest ("44.1 kHz -> 48 kHz: tones come out exactly where they should (better than -80 dB)");
        for (double hz : { 100.0, 1000.0, 10000.0, 18000.0 })
            expectLessThan (toneError (44100, 48000, hz), 1.0e-4, juce::String (hz) + " Hz");

        beginTest ("48 kHz -> 44.1 kHz and 96 kHz -> 48 kHz keep the audio clean");
        expectLessThan (toneError (48000, 44100, 1000.0), 1.0e-4);
        expectLessThan (toneError (96000, 48000, 5000.0), 1.0e-4);

        beginTest ("Going down a rate, sound above the new limit is removed (no aliasing)");
        {
            const int n = 96000;
            std::vector<float> in ((size_t) n);
            for (int i = 0; i < n; ++i) in[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 30000.0 * i / 96000.0);
            pg::SincResampler rs (96000, 48000);
            std::vector<float> out ((size_t) rs.outputLength ((std::uint64_t) n));
            rs.process (in.data(), (std::uint64_t) n, out.data());
            double peak = 0.0;
            for (size_t o = 4800; o < out.size() - 4800; ++o) peak = std::max (peak, (double) std::abs (out[o]));
            expectLessThan (peak, 1.0e-4);   // a 30 kHz tone would fold back to 18 kHz without the filter
        }

        beginTest ("Same rate is a straight copy; length is exact");
        pg::SincResampler same (48000, 48000);
        expect (same.passthrough());
        pg::SincResampler up (44100, 48000);
        expectEquals ((juce::int64) up.outputLength (44100), (juce::int64) 48000);
    }
};
static SincResamplerTests sincResamplerTests;
