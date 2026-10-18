// Elite plugin tests: registry, presets, real-time safety, bypass, and per-plugin DSP behaviour.

#include "Plugins/PluginRegistry.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace hemtest { void beginTracking(); int endTracking(); }

namespace pg::test
{
namespace
{
    constexpr double kFs = 48000.0;
    constexpr int kBlock = 512;

    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (int n) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}
        int size() const { return (int) l.size(); }
    };

    Stereo sine (int n, double hz, float amp, double fs = kFs)
    {
        Stereo s (n);
        for (int i = 0; i < n; ++i) s.l[(size_t) i] = s.r[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / fs);
        return s;
    }

    Stereo noise (int n, float amp, int seed = 7)
    {
        juce::Random rnd (seed);
        Stereo s (n);
        for (int i = 0; i < n; ++i) { s.l[(size_t) i] = amp * (rnd.nextFloat() * 2.0f - 1.0f); s.r[(size_t) i] = amp * (rnd.nextFloat() * 2.0f - 1.0f); }
        return s;
    }

    void run (ElitePlugin& p, Stereo& s, int block = kBlock)
    {
        for (int start = 0; start < s.size(); start += block)
            p.processStereo (s.l.data() + start, s.r.data() + start, juce::jmin (block, s.size() - start));
    }

    double rms (const std::vector<float>& x, int from = 0, int to = -1)
    {
        if (to < 0) to = (int) x.size();
        double acc = 0.0;
        for (int i = from; i < to; ++i) acc += (double) x[(size_t) i] * x[(size_t) i];
        return std::sqrt (acc / juce::jmax (1, to - from));
    }

    double db (double lin) { return 20.0 * std::log10 (juce::jmax (1.0e-12, lin)); }

    float peak (const std::vector<float>& x, int from = 0)
    {
        float m = 0.0f;
        for (size_t i = (size_t) from; i < x.size(); ++i) m = juce::jmax (m, std::abs (x[i]));
        return m;
    }

    bool allFinite (const Stereo& s)
    {
        for (int i = 0; i < s.size(); ++i)
            if (! std::isfinite (s.l[(size_t) i]) || ! std::isfinite (s.r[(size_t) i])) return false;
        return true;
    }

    void set (ElitePlugin& p, const char* id, float value)
    {
        auto* rp = p.getAPVTS().getParameter (id);
        jassert (rp != nullptr);
        if (rp != nullptr) rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    }

    std::unique_ptr<ElitePlugin> make (const char* id, double fs = kFs, int block = kBlock)
    {
        auto p = PluginRegistry::create (id);
        p->prepareToPlay (fs, block);
        return p;
    }

    /** Sine level response of a plugin at one frequency (dB), measured after settling. */
    double gainAt (ElitePlugin& p, double hz)
    {
        p.reset();
        auto s = sine ((int) kFs, hz, 0.1f);
        run (p, s);
        return db (rms (s.l, (int) kFs / 2) / (0.1 / std::sqrt (2.0)));
    }
}

//==============================================================================
class RegistryTests final : public juce::UnitTest
{
public:
    RegistryTests() : juce::UnitTest ("Plugin Registry", "Playground") {}

    void runTest() override
    {
        beginTest ("nineteen plugins, unique ids, only Elite Master is master-only");
        expectEquals ((int) PluginRegistry::all().size(), 19);
        juce::StringArray ids;
        for (auto* info : PluginRegistry::all())
        {
            expect (! ids.contains (info->id), juce::String ("duplicate id ") + info->id);
            ids.add (info->id);
            expect (PluginRegistry::find (info->id) == info);
            auto p = PluginRegistry::create (info->id);
            expect (p != nullptr);
            expectEquals (p->getName(), juce::String (info->name));
            expect (info->masterOnly == (juce::String (info->id) == "elite-master"));
        }
        expect (PluginRegistry::create ("nope") == nullptr);
        expect (PluginRegistry::find ("nope") == nullptr);

        beginTest ("every factory preset names only real parameters, inside their ranges");
        for (auto* info : PluginRegistry::all())
        {
            auto p = PluginRegistry::create (info->id);
            const auto& presets = Presets::factory (info->id);
            expect (presets.size() >= 2, juce::String (info->name) + " has too few presets");
            juce::StringArray names;
            for (const auto& preset : presets)
            {
                expect (! names.contains (preset.name), "duplicate preset name " + preset.name);
                names.add (preset.name);
                for (const auto& [id, value] : preset.values)
                {
                    auto* rp = p->getAPVTS().getParameter (id);
                    expect (rp != nullptr, juce::String (info->name) + " / " + preset.name + ": unknown parameter " + id);
                    if (rp == nullptr) continue;
                    const auto range = rp->getNormalisableRange();
                    expect (value >= range.start - 1.0e-4f && value <= range.end + 1.0e-4f,
                            juce::String (info->name) + " / " + preset.name + ": " + id + " out of range");
                }
            }
        }

        beginTest ("applying a preset sets listed values, resets the rest, keeps bypass");
        {
            auto p = make ("elite-comp");
            set (*p, "bypass", 1.0f);
            set (*p, "mix", 40.0f);
            const auto& preset = Presets::factory ("elite-comp")[0];   // Drum Bus Glue (no mix)
            Presets::apply (*p, preset);
            expectWithinAbsoluteError (p->getAPVTS().getRawParameterValue ("threshold")->load(), -18.0f, 0.05f);
            expectWithinAbsoluteError (p->getAPVTS().getRawParameterValue ("mix")->load(), 100.0f, 0.05f);
            expect (p->getAPVTS().getRawParameterValue ("bypass")->load() > 0.5f);
        }

        beginTest ("user presets: save, list, load, delete");
        {
            auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pg-preset-test-" + juce::String (juce::Random::getSystemRandom().nextInt()));
            Presets::setUserRootForTesting (root);
            auto p = make ("elite-verb");
            set (*p, "decay", 4.2f);
            set (*p, "type", 2.0f);
            expect (Presets::saveUser (*p, "My Big Hall"));
            expect (! Presets::saveUser (*p, "   "));
            expect (Presets::userPresetNames ("elite-verb").contains ("My Big Hall"));
            p->resetToDefaults();
            expectWithinAbsoluteError (p->getAPVTS().getRawParameterValue ("decay")->load(), 1.8f, 0.01f);
            set (*p, "bypass", 1.0f);
            expect (Presets::loadUser (*p, "My Big Hall"));
            expectWithinAbsoluteError (p->getAPVTS().getRawParameterValue ("decay")->load(), 4.2f, 0.02f);
            expectWithinAbsoluteError (p->getAPVTS().getRawParameterValue ("type")->load(), 2.0f, 0.01f);
            expect (p->getAPVTS().getRawParameterValue ("bypass")->load() > 0.5f, "loading a preset changed bypass");
            expect (! Presets::loadUser (*p, "missing"));
            expect (Presets::deleteUser ("elite-verb", "My Big Hall"));
            expect (Presets::userPresetNames ("elite-verb").isEmpty());
            root.deleteRecursively();
            Presets::setUserRootForTesting ({});
        }
    }
};

//==============================================================================
class RealtimeTests final : public juce::UnitTest
{
public:
    RealtimeTests() : juce::UnitTest ("Plugin Realtime Safety", "Playground") {}

    void runTest() override
    {
        for (auto* info : PluginRegistry::all())
        {
            beginTest (juce::String (info->name) + ": no allocation, finite output, every preset, odd block sizes");
            auto p = make (info->id);
            std::atomic<bool> global { false };
            std::atomic<float> bpm { 93.0f };
            p->setGlobalBypass (&global);
            p->setTempoSource (&bpm);
            p->setAnalyzerActive (true);

            auto s = noise (kBlock, 0.5f);
            run (*p, s);   // warm-up (first block may touch nothing new, but be safe)

            int allocations = 0;
            auto presets = Presets::factory (info->id);
            presets.insert (presets.begin(), FactoryPreset { "defaults", {} });
            bool finite = true;
            for (const auto& preset : presets)
            {
                Presets::apply (*p, preset);
                for (int rep = 0; rep < 40; ++rep)
                {
                    auto block = noise (kBlock, rep % 7 == 0 ? 2.0f : 0.3f, rep);
                    const int n = rep % 3 == 0 ? 37 : (rep % 3 == 1 ? kBlock : 256);
                    global.store (rep == 20);
                    hemtest::beginTracking();
                    p->processStereo (block.l.data(), block.r.data(), n);
                    allocations += hemtest::endTracking();
                    finite = finite && allFinite (block);
                }
            }
            // a block larger than prepared is split, not overrun
            {
                auto big = noise (kBlock * 3 + 5, 0.3f);
                hemtest::beginTracking();
                p->processStereo (big.l.data(), big.r.data(), big.size());
                allocations += hemtest::endTracking();
                finite = finite && allFinite (big);
            }
            float tmp[kBlock];
            hemtest::beginTracking();
            p->readAnalyzer (tmp, kBlock);
            allocations += hemtest::endTracking();

            expectEquals (allocations, 0, "allocations on the audio thread");
            expect (finite, "NaN / Inf in output");

            beginTest (juce::String (info->name) + ": sample-rate changes (44.1k, 96k, 192k)");
            for (double fs : { 44100.0, 96000.0, 192000.0 })
            {
                p->prepareToPlay (fs, 1024);
                auto x = noise ((int) fs / 2, 0.4f);
                run (*p, x, 1024);
                expect (allFinite (x), "non-finite at " + juce::String (fs));
            }

            beginTest (juce::String (info->name) + ": bypass passes audio through untouched (latency-aligned)");
            {
                auto q = make (info->id);
                set (*q, "bypass", 1.0f);
                q->prepareToPlay (kFs, kBlock);   // starts fully bypassed
                const int lat = q->getLatencySamples();
                auto in = noise (kBlock * 8, 0.5f);
                auto out = in;
                run (*q, out);
                double err = 0.0;
                for (int i = lat; i < in.size(); ++i)
                    err = juce::jmax (err, (double) std::abs (out.l[(size_t) i] - in.l[(size_t) (i - lat)]));
                expectLessThan (err, 1.0e-6, "bypass is not transparent");
            }

            beginTest (juce::String (info->name) + ": state round-trip");
            {
                auto a = make (info->id);
                const auto& presets2 = Presets::factory (info->id);
                Presets::apply (*a, presets2.back());
                juce::MemoryBlock mb;
                a->getStateInformation (mb);
                auto b = make (info->id);
                b->setStateInformation (mb.getData(), (int) mb.getSize());
                for (auto* prm : a->getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                        expectWithinAbsoluteError (b->getAPVTS().getParameter (rp->getParameterID())->getValue(), rp->getValue(), 1.0e-5f,
                                                   rp->getParameterID());
            }
        }
    }
};

//==============================================================================
class DspTests final : public juce::UnitTest
{
public:
    DspTests() : juce::UnitTest ("Plugin DSP", "Playground") {}

    void runTest() override
    {
        beginTest ("Elite EQ: defaults are flat; a +6 dB bell at 1 kHz gives +6 dB there and ~0 dB far away");
        {
            auto p = make ("elite-eq");
            for (double hz : { 50.0, 1000.0, 8000.0 })
                expectWithinAbsoluteError (gainAt (*p, hz), 0.0, 0.1, "not flat at " + juce::String (hz));
            set (*p, "b4_freq", 1000.0f); set (*p, "b4_gain", 6.0f); set (*p, "b4_q", 1.0f);
            expectWithinAbsoluteError (gainAt (*p, 1000.0), 6.0, 0.3);
            expectWithinAbsoluteError (gainAt (*p, 60.0), 0.0, 0.3);
            set (*p, "b1_type", 0.0f); set (*p, "b1_freq", 100.0f);   // 24 dB/oct low cut
            expectLessThan (gainAt (*p, 25.0), -40.0);
            BiquadCoeffs c[2];
            expectEquals (static_cast<EliteEq&> (*p).bandCoeffs (0, kFs, c), 2);
        }

        beginTest ("Elite EQ: the curve drawn in the window matches what the EQ really does");
        {
            auto p = make ("elite-eq");
            auto& eq = dynamic_cast<EliteEq&> (*p);
            set (*p, EliteEq::id (2, "gain").toRawUTF8(), 6.0f); set (*p, EliteEq::id (2, "freq").toRawUTF8(), 1000.0f);
            set (*p, EliteEq::id (4, "type").toRawUTF8(), 2.0f); set (*p, EliteEq::id (4, "gain").toRawUTF8(), -4.0f); set (*p, EliteEq::id (4, "freq").toRawUTF8(), 6000.0f);
            set (*p, EliteEq::id (0, "type").toRawUTF8(), 0.0f); set (*p, EliteEq::id (0, "freq").toRawUTF8(), 80.0f);   // low cut
            set (*p, "output", -2.0f);
            for (double hz : { 50.0, 200.0, 1000.0, 3000.0, 9000.0 })
            {
                auto s = sine ((int) kFs, hz, 0.25f);
                run (*p, s);
                const double measured = db (rms (s.l, (int) kFs / 2) * std::sqrt (2.0) / 0.25);
                expectWithinAbsoluteError ((double) eq.responseDb (kFs, hz), measured, 0.3, "curve vs audio at " + juce::String (hz) + " Hz");
            }
        }

        beginTest ("Elite Comp: reduces a loud signal by about (1 - 1/ratio) of the overshoot, MIX blends");
        {
            auto p = make ("elite-comp");
            set (*p, "threshold", -20.0f); set (*p, "ratio", 4.0f); set (*p, "arc", 0.0f); set (*p, "attack", 1.0f);
            const double g = gainAt (*p, 1000.0);   // input -23 dBFS RMS (0.1 peak = -20 dBFS peak) -> right at threshold-ish
            expect (g > -3.0 && g <= 0.3);
            auto s = sine ((int) kFs, 1000.0, 1.0f);   // 0 dBFS peak, 20 dB above threshold
            run (*p, s);
            const double out = db (rms (s.l, (int) kFs / 2) * std::sqrt (2.0));
            expect (out < -8.0 && out > -20.0, "compressed level " + juce::String (out));
            expect (p->getReadout (EliteComp::gr) > 6.0f, "GR readout " + juce::String (p->getReadout (EliteComp::gr)));
            set (*p, "mix", 0.0f);
            auto d = sine ((int) kFs, 1000.0, 1.0f);
            run (*p, d);
            expectWithinAbsoluteError (db (rms (d.l, (int) kFs / 2) * std::sqrt (2.0)), 0.0, 0.2, "mix 0 should be dry");
        }

        beginTest ("Elite Vocalis: SQUEEZE evens out loud and quiet parts; output never clips past the safety limit");
        {
            auto p = make ("elite-vox");
            set (*p, "squeeze", 18.0f);
            auto s = sine ((int) kFs * 2, 300.0, 1.0f);
            for (int i = (int) kFs; i < s.size(); ++i) { s.l[(size_t) i] *= 0.1f; s.r[(size_t) i] *= 0.1f; }   // -20 dB quieter second half
            run (*p, s);
            const double loud = rms (s.l, (int) kFs / 2, (int) kFs), quiet = rms (s.l, (int) kFs * 3 / 2);
            expectLessThan (db (loud) - db (quiet), 14.0, "range not reduced");
            expectLessThan ((double) peak (s.l, (int) kFs / 4), 1.0);
        }

        beginTest ("Elite Vocalis: OUTPUT +12 dB still never goes past the built-in limiter (-0.3 dBFS)");
        {
            auto p = make ("elite-vox");
            set (*p, "output", 12.0f);
            set (*p, "squeeze", 6.0f);
            auto s = sine ((int) kFs, 300.0, 0.9f);
            run (*p, s);
            expectLessThan ((double) peak (s.l, (int) kFs / 4), (double) params::dbToGain (-0.29f), "Vocalis output past its limiter");
            expectGreaterThan (p->getReadout (EliteVox::limitGr), 3.0f, "LIMIT meter must show the limiting");
        }

        beginTest ("Elite Master: INPUT can go below 0 dB, so MATCH TARGET can also turn a hot mix down");
        {
            auto p = make ("elite-master");
            auto* in = p->getAPVTS().getParameter ("input");
            expectWithinAbsoluteError (in->convertFrom0to1 (0.0f), -12.0f, 1.0e-4f);
            set (*p, "input", -6.0f);
            expectWithinAbsoluteError (p->getAPVTS().getRawParameterValue ("input")->load(), -6.0f, 0.01f);
            // a mix at about -6 LUFS against the -14 LUFS target: SUGGESTED IN must be negative
            auto s = sine ((int) kFs * 4, 1000.0, 0.7f);
            set (*p, "input", 0.0f);
            run (*p, s);
            expectLessThan (p->getReadout (EliteMaster::suggestedInput), -3.0f, "suggestion should be to turn down");
            expect (dynamic_cast<EliteMaster&> (*p).matchTarget());
            expectLessThan (p->getAPVTS().getRawParameterValue ("input")->load(), -3.0f);
        }

        beginTest ("Elite Vocalis: gate silences noise below its threshold");
        {
            auto p = make ("elite-vox");
            set (*p, "gate", -30.0f);
            auto s = noise ((int) kFs, 0.003f);   // ~ -50 dBFS
            run (*p, s);
            expectLessThan (db (rms (s.l, (int) kFs / 2)), -70.0);
        }

        beginTest ("Elite Bass: adds harmonics above FREQ from a pure sub; ORIGINAL -60 removes the sub below FREQ");
        {
            auto p = make ("elite-bass");
            set (*p, "freq", 80.0f); set (*p, "intensity", 100.0f); set (*p, "original", -60.0f);
            auto s = sine ((int) kFs, 50.0, 0.5f);
            run (*p, s);
            // Goertzel at 50 Hz (fundamental) vs 100 / 150 Hz (harmonics)
            auto mag = [&] (double hz)
            {
                const int from = (int) kFs / 2, n = (int) kFs / 2;
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / kFs, c = 2.0 * std::cos (w);
                double s0 = 0, s1 = 0, s2 = 0;
                for (int i = 0; i < n; ++i) { s0 = s.l[(size_t) (from + i)] + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / n * 2.0;
            };
            const double fund = mag (50.0), h2 = mag (100.0), h3 = mag (150.0);
            expect (h2 + h3 > 0.02, "harmonics missing: " + juce::String (h2) + " / " + juce::String (h3));
            // 50 Hz is under an octave below the 80 Hz LR4 split, so ~-17 dB still comes through the high band
            expect (fund < 0.5 * 0.2, "original sub not removed: " + juce::String (fund));
        }

        beginTest ("Elite Channel: phase invert flips polarity; EQ band gain is applied");
        {
            auto p = make ("elite-channel");
            set (*p, "comp_on", 0.0f); set (*p, "phase", 1.0f);
            auto s = sine (kBlock * 8, 1000.0, 0.2f);
            auto ref = s;
            run (*p, s);
            double corr = 0.0;
            for (int i = kBlock * 4; i < s.size(); ++i) corr += (double) s.l[(size_t) i] * ref.l[(size_t) i];
            expectLessThan (corr, 0.0);
            set (*p, "phase", 0.0f);
            set (*p, "eq3_freq", 2500.0f); set (*p, "eq3_gain", 9.0f);
            expectWithinAbsoluteError (gainAt (*p, 2500.0), 9.0, 0.4);
        }

        beginTest ("Elite Verb: an impulse produces a decaying tail; 100% mix has no dry spike");
        {
            auto p = make ("elite-verb");
            set (*p, "mix", 100.0f); set (*p, "decay", 2.0f); set (*p, "predelay", 0.1f);
            p->prepareToPlay (kFs, kBlock);   // start at these settings (no smoothing from the defaults)
            Stereo s ((int) kFs * 3);
            s.l[0] = s.r[0] = 1.0f;
            run (*p, s);
            const double early = rms (s.l, (int) (kFs * 0.05), (int) (kFs * 0.3));
            const double late = rms (s.l, (int) (kFs * 2.0), (int) (kFs * 2.5));
            expect (early > 1.0e-4, "no reverb");
            expect (late < early * 0.3, "tail does not decay");
            expect (std::abs (s.l[0]) < 0.5f, "dry impulse leaked at 100% mix");
        }

        beginTest ("Elite Delay: 1/4 note at 120 BPM echoes 500 ms later");
        {
            auto p = make ("elite-delay");
            std::atomic<float> bpm { 120.0f };
            p->setTempoSource (&bpm);
            set (*p, "mix", 100.0f); set (*p, "feedback", 0.0f); set (*p, "lowcut", 20.0f); set (*p, "highcut", 20000.0f);
            p->prepareToPlay (kFs, kBlock);
            Stereo s ((int) kFs);
            s.l[100] = s.r[100] = 1.0f;
            run (*p, s);
            int at = 0; float best = 0.0f;
            for (int i = 0; i < s.size(); ++i) if (std::abs (s.l[(size_t) i]) > best) { best = std::abs (s.l[(size_t) i]); at = i; }
            expectWithinAbsoluteError (at - 100, 24000, 2);
            expectWithinAbsoluteError (p->getReadout (EliteDelay::delayMs), 500.0f, 1.0f);
            expectWithinAbsoluteError (EliteDelay::divisionBeats (8), 1.0, 1.0e-9);
        }

        beginTest ("Elite Delay: PING-PONG 100 bounces the echoes left, right, left");
        {
            auto p = make ("elite-delay");
            set (*p, "sync", 0.0f); set (*p, "time", 100.0f); set (*p, "pingpong", 100.0f);
            set (*p, "mix", 100.0f); set (*p, "feedback", 60.0f); set (*p, "lowcut", 20.0f); set (*p, "highcut", 20000.0f);
            p->prepareToPlay (kFs, kBlock);
            Stereo s ((int) kFs);
            s.l[100] = s.r[100] = 0.5f;
            run (*p, s);
            auto energy = [&s] (const std::vector<float>& x, int echo)
            {
                const int c = 100 + echo * (int) (0.1 * kFs);
                double e = 0.0;
                for (int i = c - 200; i < c + 200; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
                return e;
            };
            expectGreaterThan (energy (s.l, 1), 100.0 * energy (s.r, 1), "1st echo should be on the left");
            expectGreaterThan (energy (s.r, 2), 100.0 * energy (s.l, 2), "2nd echo should be on the right");
            expectGreaterThan (energy (s.l, 3), 100.0 * energy (s.r, 3), "3rd echo should be on the left");
        }

        beginTest ("Elite Delay: PING-PONG 0 keeps a stereo echo stereo (left stays left)");
        {
            auto p = make ("elite-delay");
            set (*p, "sync", 0.0f); set (*p, "time", 100.0f); set (*p, "pingpong", 0.0f);
            set (*p, "mix", 100.0f); set (*p, "feedback", 60.0f); set (*p, "lowcut", 20.0f); set (*p, "highcut", 20000.0f);
            p->prepareToPlay (kFs, kBlock);
            Stereo s ((int) kFs);
            s.l[100] = 0.5f;   // left only
            run (*p, s);
            double el = 0.0, er = 0.0;
            for (size_t i = 200; i < s.l.size(); ++i) { el += (double) s.l[i] * s.l[i]; er += (double) s.r[i] * s.r[i]; }
            expectGreaterThan (el, 0.01);
            expectLessThan (er, el * 1.0e-6, "nothing should leak to the right");
        }

        beginTest ("Elite Master: never exceeds the ceiling; LUFS of a -20 dBFS 1 kHz stereo sine reads about -20");
        {
            auto p = make ("elite-master");
            set (*p, "input", 18.0f); set (*p, "ceiling", -1.0f);
            auto s = noise ((int) kFs * 2, 0.9f);
            run (*p, s);
            expectLessThan ((double) peak (s.l, p->getLatencySamples() + 1), (double) params::dbToGain (-1.0f) + 1.0e-4);
            expect (p->getLatencySamples() > 0);

            auto m = make ("elite-master");
            auto t = sine ((int) kFs * 5, 1000.0, params::dbToGain (-20.0f));
            run (*m, t);
            expectWithinAbsoluteError (m->getReadout (EliteMaster::integrated), -20.0f, 0.5f);
            expectWithinAbsoluteError (m->getReadout (EliteMaster::truePeak), -20.0f, 0.3f);
            set (*m, "target", 0.0f);   // -14
            expect (static_cast<EliteMaster&> (*m).matchTarget());
            expectWithinAbsoluteError (m->getAPVTS().getRawParameterValue ("input")->load(), 6.0f, 0.6f);
        }

        beginTest ("Elite Master: true peak (inter-sample) stays at the ceiling, not just the samples");
        {
            // fs/4 sine at 45 degrees: every sample sits at 0.707 of the real peak, so a
            // sample-peak limiter lets ~+3 dB of inter-sample peak through.
            for (int which = 0; which < 2; ++which)
            {
                auto m = make ("elite-master");
                set (*m, "input", 12.0f); set (*m, "ceiling", -1.0f);
                Stereo s ((int) kFs * 3);
                if (which == 0)
                    for (int i = 0; i < s.size(); ++i)
                        s.l[(size_t) i] = s.r[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::halfPi * i + juce::MathConstants<double>::pi * 0.25);
                else
                    s = noise ((int) kFs * 3, 0.5f);
                run (*m, s);
                const float tp = m->getReadout (EliteMaster::truePeak);
                expectLessThan ((double) tp, -1.0 + 0.15, (which == 0 ? "fs/4 sine true peak " : "noise true peak ") + juce::String (tp));
                expectLessThan ((double) peak (s.l, m->getLatencySamples() + 1), (double) params::dbToGain (-1.0f) + 1.0e-4);
            }
        }

        beginTest ("Global BEFORE/AFTER switch bypasses with a click-free crossfade");
        {
            auto p = make ("elite-eq");
            std::atomic<bool> global { false };
            p->setGlobalBypass (&global);
            set (*p, "output", -12.0f);
            auto s = sine ((int) kFs, 200.0, 0.5f);
            run (*p, s);
            global.store (true);
            auto t = sine (kBlock * 4, 200.0, 0.5f);
            auto ref = t;
            run (*p, t);
            float maxStep = 0.0f;
            for (int i = 1; i < t.size(); ++i) maxStep = juce::jmax (maxStep, std::abs (t.l[(size_t) i] - t.l[(size_t) i - 1]));
            expectLessThan (maxStep, 0.05f, "crossfade clicks");
            expectWithinAbsoluteError (t.l.back(), ref.l.back(), 1.0e-6f);
        }
    }
};

//==============================================================================
class SwirlTests final : public juce::UnitTest
{
public:
    SwirlTests() : juce::UnitTest ("Elite Swirl Killer", "Playground") {}

    static double bandEnergy (const std::vector<float>& x, int from, double lowHz)
    {
        // energy above lowHz via a 4th-order high-pass (two RBJ stages)
        auto c = BiquadCoeffs::highPass (kFs, lowHz, 0.7071);
        BiquadState s1, s2;
        double e = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const float y = s2.process (c, s1.process (c, x[i]));
            if ((int) i >= from) e += (double) y * y;
        }
        return e;
    }

    void runTest() override
    {
        beginTest ("latency is one FFT frame (2048 at 48 kHz, 4096 at 96 kHz)");
        {
            auto p = make ("elite-swirl");
            expectEquals (p->getLatencySamples(), 2048);
            p->prepareToPlay (96000.0, 512);
            expectEquals (p->getLatencySamples(), 4096);
        }

        beginTest ("AMOUNT 0 is transparent (both modes): output = input delayed by the latency");
        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = make ("elite-swirl");
            set (*p, "amount", 0.0f); set (*p, "mode", (float) mode);
            p->prepareToPlay (kFs, kBlock);
            auto in = noise ((int) kFs, 0.3f, 11);
            auto out = in;
            run (*p, out);
            const int lat = p->getLatencySamples();
            double err = 0.0;
            for (int i = lat + 4096; i < in.size(); ++i)
                err = juce::jmax (err, (double) std::abs (out.l[(size_t) i] - in.l[(size_t) (i - lat)]),
                                       (double) std::abs (out.r[(size_t) i] - in.r[(size_t) (i - lat)]));
            expectLessThan (err, 1.0e-4, "not transparent, mode " + juce::String (mode));
        }

        beginTest ("a centred (mono) signal is never touched, even at AMOUNT 100");
        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = make ("elite-swirl");
            set (*p, "amount", 100.0f); set (*p, "protect", 0.0f); set (*p, "mode", (float) mode);
            p->prepareToPlay (kFs, kBlock);
            auto in = noise ((int) kFs, 0.3f, 5);
            in.r = in.l;   // identical L and R = pure Mid
            auto out = in;
            run (*p, out);
            const int lat = p->getLatencySamples();
            double err = 0.0;
            for (int i = lat + 4096; i < in.size(); ++i) err = juce::jmax (err, (double) std::abs (out.l[(size_t) i] - in.l[(size_t) (i - lat)]));
            expectLessThan (err, 1.0e-4);
        }

        beginTest ("swirl in the sides is removed, the centre above FROM is kept");
        for (int mode = 0; mode < 2; ++mode)
        {
            auto p = make ("elite-swirl");
            set (*p, "amount", 100.0f); set (*p, "from", 4000.0f); set (*p, "protect", 30.0f); set (*p, "mode", (float) mode);
            p->prepareToPlay (kFs, kBlock);
            const int n = (int) kFs * 3;
            auto mid = noise (n, 0.2f, 21);        // broadband centre (vocal breath, cymbal body...)
            auto side = noise (n, 0.12f, 99);      // independent "codec" noise in the sides: wandering L/R phase
            Stereo in (n);
            for (int i = 0; i < n; ++i)
            {
                in.l[(size_t) i] = mid.l[(size_t) i] + side.l[(size_t) i];
                in.r[(size_t) i] = mid.l[(size_t) i] - side.l[(size_t) i];
            }
            auto out = in;
            run (*p, out);
            const int lat = p->getLatencySamples();
            std::vector<float> mIn ((size_t) n), sIn ((size_t) n), mOut ((size_t) n), sOut ((size_t) n);
            for (int i = 0; i < n - lat; ++i)
            {
                mIn[(size_t) i] = 0.5f * (in.l[(size_t) i] + in.r[(size_t) i]);
                sIn[(size_t) i] = 0.5f * (in.l[(size_t) i] - in.r[(size_t) i]);
                mOut[(size_t) i] = 0.5f * (out.l[(size_t) (i + lat)] + out.r[(size_t) (i + lat)]);
                sOut[(size_t) i] = 0.5f * (out.l[(size_t) (i + lat)] - out.r[(size_t) (i + lat)]);
            }
            const int from = (int) kFs;
            const double sideCut = 10.0 * std::log10 (bandEnergy (sOut, from, 6000.0) / bandEnergy (sIn, from, 6000.0));
            const double midChange = 10.0 * std::log10 (bandEnergy (mOut, from, 6000.0) / bandEnergy (mIn, from, 6000.0));
            logMessage ("mode " + juce::String (mode) + ": side above 6 kHz " + juce::String (sideCut, 1) + " dB, mid " + juce::String (midChange, 2) + " dB");
            expectLessThan (sideCut, -6.0, "swirl not removed");
            expectWithinAbsoluteError (midChange, 0.0, 0.3, "centre changed");
            expect (p->getReadout (EliteSwirl::swirlPercent) > 20.0f);
            expect (p->getReadout (EliteSwirl::removedDb) < -3.0f);
        }

        beginTest ("steady panned sound (real stereo) survives in REPAIR mode");
        {
            auto p = make ("elite-swirl");
            set (*p, "amount", 100.0f); set (*p, "from", 4000.0f); set (*p, "protect", 40.0f); set (*p, "mode", 1.0f);
            p->prepareToPlay (kFs, kBlock);
            const int n = (int) kFs * 3;
            auto src = noise (n, 0.25f, 7);
            Stereo in (n);
            for (int i = 0; i < n; ++i) { in.l[(size_t) i] = src.l[(size_t) i]; in.r[(size_t) i] = 0.35f * src.l[(size_t) i]; }   // panned left
            auto out = in;
            run (*p, out);
            const int lat = p->getLatencySamples();
            std::vector<float> sIn ((size_t) n), sOut ((size_t) n);
            for (int i = 0; i < n - lat; ++i)
            {
                sIn[(size_t) i] = 0.5f * (in.l[(size_t) i] - in.r[(size_t) i]);
                sOut[(size_t) i] = 0.5f * (out.l[(size_t) (i + lat)] - out.r[(size_t) (i + lat)]);
            }
            const double sideChange = 10.0 * std::log10 (bandEnergy (sOut, (int) kFs, 6000.0) / bandEnergy (sIn, (int) kFs, 6000.0));
            logMessage ("panned side change " + juce::String (sideChange, 2) + " dB");
            expectGreaterThan (sideChange, -1.0, "real stereo was eaten");
        }

        beginTest ("WHISTLE HUNTER notches a steady 12.2 kHz whine in the sides, finds its frequency, leaves the centre");
        {
            auto run12k = [this] (float whistle, float& freq, double& sideTone, double& midTone)
            {
                auto p = make ("elite-swirl");
                set (*p, "amount", 0.0f); set (*p, "whistle", whistle); set (*p, "from", 4000.0f);
                p->prepareToPlay (kFs, kBlock);
                const int n = (int) kFs * 3;
                auto mid = noise (n, 0.15f, 4);
                Stereo in (n);
                for (int i = 0; i < n; ++i)
                {
                    const float w = 0.05f * (float) std::sin (juce::MathConstants<double>::twoPi * 12200.0 * i / kFs);
                    const float c = 0.05f * (float) std::sin (juce::MathConstants<double>::twoPi * 9000.0 * i / kFs);   // centre tone: must survive
                    in.l[(size_t) i] = mid.l[(size_t) i] + c + w;
                    in.r[(size_t) i] = mid.l[(size_t) i] + c - w;
                }
                run (*p, in);
                freq = p->getReadout (EliteSwirl::whistleHz);
                auto goertzel = [&] (bool side, double hz)
                {
                    const int from = (int) kFs * 2, len = (int) kFs / 2;
                    const double w0 = juce::MathConstants<double>::twoPi * hz / kFs, cf = 2.0 * std::cos (w0);
                    double s0 = 0, s1 = 0, s2 = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const float x = side ? 0.5f * (in.l[(size_t) (from + i)] - in.r[(size_t) (from + i)])
                                             : 0.5f * (in.l[(size_t) (from + i)] + in.r[(size_t) (from + i)]);
                        s0 = x + cf * s1 - s2; s2 = s1; s1 = s0;
                    }
                    return std::sqrt (s1 * s1 + s2 * s2 - cf * s1 * s2) / len * 2.0;
                };
                sideTone = goertzel (true, 12200.0);
                midTone = goertzel (false, 9000.0);
            };
            float f0, f1; double s0, s1, m0, m1;
            run12k (0.0f, f0, s0, m0);
            run12k (100.0f, f1, s1, m1);
            logMessage ("whistle: side 12.2k " + juce::String (20.0 * std::log10 (s1 / s0), 1) + " dB, centre 9k " + juce::String (20.0 * std::log10 (m1 / m0), 2)
                        + " dB, found " + juce::String (f1, 0) + " Hz");
            expectLessThan (20.0 * std::log10 (s1 / s0), -15.0);
            expectWithinAbsoluteError (20.0 * std::log10 (m1 / m0), 0.0, 0.3);
            expectWithinAbsoluteError (f1, 12200.0f, 30.0f);
            expectEquals (f0, 0.0f);
        }

        beginTest ("COHERENCE reads +1 for mono, ~0 for unrelated sides, -1 for out of phase");
        {
            auto coh = [this] (int kind)
            {
                auto p = make ("elite-swirl");
                set (*p, "from", 1000.0f);
                p->prepareToPlay (kFs, kBlock);
                auto a = noise ((int) kFs, 0.3f, 1);
                auto b = noise ((int) kFs, 0.3f, 2);
                for (int i = 0; i < a.size(); ++i)
                    a.r[(size_t) i] = kind == 0 ? a.l[(size_t) i] : (kind == 1 ? b.l[(size_t) i] : -a.l[(size_t) i]);
                run (*p, a);
                return p->getReadout (EliteSwirl::coherence);
            };
            expectGreaterThan (coh (0), 0.95f);
            expectWithinAbsoluteError (coh (1), 0.0f, 0.15f);
            expectLessThan (coh (2), -0.95f);
        }

        beginTest ("TRANSIENT SHIELD holds the cleaning back on drum hits");
        {
            auto sideAfterHits = [this] (float shield)
            {
                auto p = make ("elite-swirl");
                set (*p, "amount", shield < 0.0f ? 0.0f : 100.0f); set (*p, "protect", 0.0f); set (*p, "shield", juce::jmax (0.0f, shield)); set (*p, "mode", 0.0f);
                p->prepareToPlay (kFs, kBlock);
                const int n = (int) kFs * 3, period = 24000;
                auto mid = noise (n, 1.0f, 8);
                auto side = noise (n, 1.0f, 9);
                Stereo in (n);
                for (int i = 0; i < n; ++i)
                {
                    const float env = 0.02f + 0.8f * std::exp (-(float) (i % period) / 600.0f);   // a hit every 0.5 s
                    in.l[(size_t) i] = env * (mid.l[(size_t) i] + 0.5f * side.l[(size_t) i]);
                    in.r[(size_t) i] = env * (mid.l[(size_t) i] - 0.5f * side.l[(size_t) i]);
                }
                auto out = in;
                run (*p, out);
                const int lat = p->getLatencySamples();
                double e = 0.0;
                for (int hit = 2; hit < 6; ++hit)
                    for (int i = 0; i < 960; ++i)   // the first 20 ms of each hit
                    {
                        const int k = hit * period + i;
                        const float sOut = 0.5f * (out.l[(size_t) (k + lat)] - out.r[(size_t) (k + lat)]);
                        e += (double) sOut * sOut;
                    }
                return e;
            };
            const double off = sideAfterHits (0.0f), on = sideAfterHits (100.0f), dry = sideAfterHits (-1.0f);
            logMessage ("dry side on hits " + juce::String (10.0 * std::log10 (dry), 1) + " dB");
            logMessage ("side on hits: shield off " + juce::String (10.0 * std::log10 (off), 1) + " dB, on " + juce::String (10.0 * std::log10 (on), 1) + " dB");
            expectLessThan (10.0 * std::log10 (off / dry), -1.5);                  // without the shield the hits lose side
            expectWithinAbsoluteError (10.0 * std::log10 (on / dry), 0.0, 0.5);    // with it, the attacks stay as they were
        }

        beginTest ("LISTEN plays only what is removed (a mono signal -> silence)");
        {
            auto p = make ("elite-swirl");
            set (*p, "amount", 100.0f); set (*p, "listen", 1.0f);
            p->prepareToPlay (kFs, kBlock);
            auto in = noise ((int) kFs, 0.3f, 3);
            in.r = in.l;
            run (*p, in);
            expectLessThan (rms (in.l, 8192), 1.0e-4);
        }

        beginTest ("3D view frames arrive while the editor is open, not otherwise");
        {
            auto p = make ("elite-swirl");
            auto& sw = static_cast<EliteSwirl&> (*p);
            auto in = noise (kBlock * 40, 0.3f);
            run (*p, in);
            EliteSwirl::Frame f[64];
            expectEquals (sw.readFrames (f, 64), 0);
            p->setAnalyzerActive (true);
            auto in2 = noise (kBlock * 40, 0.3f);
            run (*p, in2);
            const int got = sw.readFrames (f, 64);
            expect (got > 10, juce::String (got));
            expect (f[0].level[30] > -90.0f && f[0].level[30] < 10.0f);
        }
    }
};

//==============================================================================
class NewPluginTests final : public juce::UnitTest
{
public:
    NewPluginTests() : juce::UnitTest ("Elite Plugins 0.10 (ten new)", "Playground") {}

    /** Amplitude of the hz component in x[from..] (projection on sin / cos). */
    static double toneAmp (const std::vector<float>& x, double hz, int from, double fs = kFs)
    {
        double s = 0.0, c = 0.0;
        const int n = (int) x.size() - from;
        for (int i = from; i < (int) x.size(); ++i)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * hz * i / fs;
            s += x[(size_t) i] * std::sin (w);
            c += x[(size_t) i] * std::cos (w);
        }
        return 2.0 * std::sqrt (s * s + c * c) / juce::jmax (1, n);
    }

    static double highEnergy (const std::vector<float>& x, int from, double hz)
    {
        auto c = BiquadCoeffs::highPass (kFs, hz, 0.7071);
        BiquadState s1, s2;
        double e = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const float y = s2.process (c, s1.process (c, x[i]));
            if ((int) i >= from) e += (double) y * y;
        }
        return e;
    }

    /** Frequency from rising zero crossings (with sub-sample interpolation) over x[from..]. */
    static double zeroCrossHz (const std::vector<float>& x, int from)
    {
        double first = -1.0, last = -1.0;
        int count = 0;
        for (int i = from + 1; i < (int) x.size(); ++i)
            if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
            {
                const double t = (double) (i - 1) + x[(size_t) i - 1] / (double) (x[(size_t) i - 1] - x[(size_t) i]);
                if (first < 0.0) first = t; else ++count;
                last = t;
            }
        return count > 0 ? kFs * count / (last - first) : 0.0;
    }

    /** Kick-like hits: 80 Hz sine with a 60 ms decay, every 250 ms. */
    static Stereo hits (int n)
    {
        Stereo s (n);
        const int period = (int) (0.25 * kFs);
        for (int i = 0; i < n; ++i)
        {
            const int t = i % period;
            const float v = 0.5f * (float) std::exp (-t / (0.06 * kFs)) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 80.0 * t / kFs);
            s.l[(size_t) i] = s.r[(size_t) i] = v;
        }
        return s;
    }

    static double segmentRms (const Stereo& s, int periodOffsetFrom, int periodOffsetTo)
    {
        const int period = (int) (0.25 * kFs);
        double acc = 0.0; int cnt = 0;
        for (int start = period * 2; start + period <= s.size(); start += period)   // skip the first two hits (warm-up)
            for (int i = start + periodOffsetFrom; i < start + periodOffsetTo; ++i) { acc += (double) s.l[(size_t) i] * s.l[(size_t) i]; ++cnt; }
        return std::sqrt (acc / juce::jmax (1, cnt));
    }

    void runTest() override
    {
        //------------------------------------------------------------------
        beginTest ("Elite Transient: defaults leave audio alone; ATTACK + boosts the hit; SUSTAIN - shortens the tail");
        {
            const int n = (int) kFs * 2;
            const auto dry = hits (n);
            const int hitFrom = 0, hitTo = (int) (0.004 * kFs), tailFrom = (int) (0.06 * kFs), tailTo = (int) (0.2 * kFs);
            {
                auto p = make ("elite-transient");
                auto x = dry; run (*p, x);
                double err = 0.0;
                for (int i = 0; i < n; ++i) err = juce::jmax (err, (double) std::abs (x.l[(size_t) i] - dry.l[(size_t) i]));
                expectLessThan (err, 1.0e-5);
            }
            {
                auto p = make ("elite-transient");
                set (*p, "attack", 100.0f);
                auto x = dry; run (*p, x);
                const double gain = db (segmentRms (x, hitFrom, hitTo) / segmentRms (dry, hitFrom, hitTo));
                logMessage ("  attack +100: hit " + juce::String (gain, 2) + " dB");
                expectGreaterThan (gain, 3.0);
            }
            {
                auto p = make ("elite-transient");
                set (*p, "sustain", -100.0f);
                auto x = dry; run (*p, x);
                const double tail = db (segmentRms (x, tailFrom, tailTo) / segmentRms (dry, tailFrom, tailTo));
                logMessage ("  sustain -100: tail " + juce::String (tail, 2) + " dB");
                expectLessThan (tail, -3.0);
            }
        }

        //------------------------------------------------------------------
        beginTest ("Elite Multiband: no compression = flat (crossovers add back up); band gain moves only its band");
        {
            auto p = make ("elite-multiband");
            for (int b = 0; b < 3; ++b) set (*p, EliteMultiband::id (b, "ratio").toRawUTF8(), 1.0f);
            for (double hz : { 60.0, 180.0, 1000.0, 3500.0, 10000.0 })
                expectWithinAbsoluteError (gainAt (*p, hz), 0.0, 0.2, "not flat at " + juce::String (hz));
            set (*p, EliteMultiband::id (0, "gain").toRawUTF8(), 6.0f);
            expectWithinAbsoluteError (gainAt (*p, 40.0), 6.0, 0.5);
            expectWithinAbsoluteError (gainAt (*p, 8000.0), 0.0, 0.3);
        }
        beginTest ("Elite Multiband: a loud low band gets compressed, the high band does not");
        {
            auto p = make ("elite-multiband");
            set (*p, EliteMultiband::id (0, "thr").toRawUTF8(), -30.0f);
            set (*p, EliteMultiband::id (0, "ratio").toRawUTF8(), 4.0f);
            set (*p, EliteMultiband::id (2, "thr").toRawUTF8(), -30.0f);
            set (*p, EliteMultiband::id (2, "ratio").toRawUTF8(), 4.0f);
            auto x = sine ((int) kFs, 60.0, 0.5f);
            run (*p, x);
            expectGreaterThan (p->getReadout (EliteMultiband::lowGr), 6.0f);
            expectLessThan (p->getReadout (EliteMultiband::highGr), 0.5f);
        }

        //------------------------------------------------------------------
        beginTest ("Elite Width: 0 % = mono; MONO BELOW removes the sides in the lows but keeps them up top");
        {
            auto p = make ("elite-width");
            set (*p, "width", 0.0f); set (*p, "mono_below", 20.0f);
            p->prepareToPlay (kFs, kBlock);
            auto x = noise ((int) kFs, 0.3f);
            run (*p, x);
            double err = 0.0;
            for (int i = 4800; i < x.size(); ++i) err = juce::jmax (err, (double) std::abs (x.l[(size_t) i] - x.r[(size_t) i]));
            expectLessThan (err, 1.0e-5);
            expectGreaterThan (p->getReadout (EliteWidth::correlation), 0.99f);

            auto q = make ("elite-width");
            set (*q, "mono_below", 150.0f);
            q->prepareToPlay (kFs, kBlock);
            for (double hz : { 40.0, 3000.0 })
            {
                q->reset();
                Stereo s ((int) kFs);   // pure side: L = +sine, R = -sine
                for (int i = 0; i < s.size(); ++i) { s.l[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kFs); s.r[(size_t) i] = -s.l[(size_t) i]; }
                run (*q, s);
                const double g = db (toneAmp (s.l, hz, (int) kFs / 2) / 0.3);
                logMessage ("  side at " + juce::String (hz) + " Hz: " + juce::String (g, 2) + " dB");
                if (hz < 100.0) expectLessThan (g, -20.0); else expectWithinAbsoluteError (g, 0.0, 0.5);
            }
        }

        //------------------------------------------------------------------
        beginTest ("Elite Hush: hiss goes down by 6 dB or more, a held 1 kHz tone stays within 1 dB");
        {
            auto p = make ("elite-hush");
            set (*p, "amount", 60.0f); set (*p, "reduction", 18.0f);
            p->prepareToPlay (kFs, kBlock);
            const int n = (int) kFs * 6;
            auto x = noise (n, 0.01f, 31);
            auto ref = x;
            for (int i = 0; i < n; ++i)
            {
                const float t = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / kFs);
                x.l[(size_t) i] += t; x.r[(size_t) i] += t;
            }
            auto in = x;
            run (*p, x);
            const int from = (int) kFs * 4;   // after the noise floor has been learned
            const double hissDrop = 10.0 * std::log10 (highEnergy (x.l, from, 4000.0) / highEnergy (in.l, from - p->getLatencySamples(), 4000.0));
            const double tone = db (toneAmp (x.l, 1000.0, from) / 0.3);
            logMessage ("  hiss above 4 kHz: " + juce::String (hissDrop, 2) + " dB, 1 kHz tone: " + juce::String (tone, 2) + " dB, readout removed "
                        + juce::String (p->getReadout (EliteHush::removedDb), 1) + " dB");
            expectLessThan (hissDrop, -6.0);
            expectWithinAbsoluteError (tone, 0.0, 1.0);
        }

        //------------------------------------------------------------------
        beginTest ("Elite De-Ess: a loud 8 kHz 'ess' is turned down, 500 Hz at the same level is left alone");
        {
            for (double hz : { 8000.0, 500.0 })
            {
                auto p = make ("elite-de-ess");
                auto x = sine ((int) kFs, hz, 0.5f);
                run (*p, x);
                const double g = db (toneAmp (x.l, hz, (int) kFs / 2) / 0.5);
                logMessage ("  " + juce::String (hz) + " Hz: " + juce::String (g, 2) + " dB");
                if (hz > 1000.0) { expectLessThan (g, -6.0); expectGreaterThan (p->getReadout (EliteDeEss::gr), 6.0f); }
                else expectWithinAbsoluteError (g, 0.0, 0.3);
            }
            auto q = make ("elite-de-ess");
            auto quiet = sine ((int) kFs, 8000.0, 0.005f);   // below the threshold: untouched
            run (*q, quiet);
            expectWithinAbsoluteError (db (toneAmp (quiet.l, 8000.0, (int) kFs / 2) / 0.005), 0.0, 0.3);
        }

        //------------------------------------------------------------------
        beginTest ("Elite Doubler: AMOUNT 0 = untouched; a mono vocal comes out wide (left and right differ)");
        {
            auto p = make ("elite-doubler");
            set (*p, "amount", 0.0f);
            p->prepareToPlay (kFs, kBlock);
            auto in = noise ((int) kFs, 0.3f);
            in.r = in.l;
            auto x = in; run (*p, x);
            double err = 0.0;
            for (int i = 0; i < x.size(); ++i) err = juce::jmax (err, (double) std::abs (x.l[(size_t) i] - in.l[(size_t) i]));
            expectLessThan (err, 1.0e-6);

            auto q = make ("elite-doubler");
            set (*q, "amount", 100.0f); set (*q, "width", 100.0f);
            q->prepareToPlay (kFs, kBlock);
            auto y = in; run (*q, y);
            double lr = 0, ll = 0, rr = 0;
            for (int i = 4800; i < y.size(); ++i) { lr += y.l[(size_t) i] * y.r[(size_t) i]; ll += y.l[(size_t) i] * y.l[(size_t) i]; rr += y.r[(size_t) i] * y.r[(size_t) i]; }
            const double corr = lr / std::sqrt (ll * rr);
            logMessage ("  L/R correlation with doubling: " + juce::String (corr, 3));
            expectLessThan (corr, 0.9);
            expectGreaterThan (q->getReadout (EliteDoubler::spreadMs), 3.0f);
        }

        //------------------------------------------------------------------
        beginTest ("Elite Console: quiet parts keep their level, loud parts get harmonics; characters differ");
        {
            auto p = make ("elite-console");
            set (*p, "character", 0.0f); set (*p, "crosstalk", 0.0f); set (*p, "drive", 12.0f);
            p->prepareToPlay (kFs, kBlock);
            {
                p->reset();
                auto quiet = sine ((int) kFs, 1000.0, 0.003f);
                run (*p, quiet);
                expectWithinAbsoluteError (db (toneAmp (quiet.l, 1000.0, (int) kFs / 2) / 0.003), 0.0, 0.2);
            }
            p->reset();
            auto loud = sine ((int) kFs, 200.0, 0.8f);
            run (*p, loud);
            const double h3 = db (toneAmp (loud.l, 600.0, (int) kFs / 2) / toneAmp (loud.l, 200.0, (int) kFs / 2));
            const double h2clean = db (toneAmp (loud.l, 400.0, (int) kFs / 2) / toneAmp (loud.l, 200.0, (int) kFs / 2));
            logMessage ("  Clean Desk, 12 dB drive: 3rd harmonic " + juce::String (h3, 1) + " dB, 2nd " + juce::String (h2clean, 1) + " dB");
            expectGreaterThan (h3, -40.0);
            expectLessThan (h2clean, -80.0);   // symmetric curve: no even harmonics
            expectGreaterThan (p->getReadout (EliteConsole::driveDb), 1.0f);

            set (*p, "character", 3.0f);   // Tube Bus: asymmetric = even harmonics
            p->reset();
            auto loud2 = sine ((int) kFs, 200.0, 0.8f);
            run (*p, loud2);
            const double h2 = db (toneAmp (loud2.l, 400.0, (int) kFs / 2) / toneAmp (loud2.l, 200.0, (int) kFs / 2));
            logMessage ("  Tube Bus: 2nd harmonic " + juce::String (h2, 1) + " dB");
            expectGreaterThan (h2, -50.0);
        }

        //------------------------------------------------------------------
        beginTest ("Elite Lo-Fi: MIX 0 is dry; 4 bits leaves only a handful of levels; wow bends the pitch");
        {
            auto p = make ("elite-lofi");
            set (*p, "mix", 0.0f);
            p->prepareToPlay (kFs, kBlock);
            auto in = noise ((int) kFs / 2, 0.3f);
            auto x = in; run (*p, x);
            double err = 0.0;
            for (int i = 0; i < x.size(); ++i) err = juce::jmax (err, (double) std::abs (x.l[(size_t) i] - in.l[(size_t) i]));
            expectLessThan (err, 1.0e-6);

            auto q = make ("elite-lofi");
            set (*q, "wow", 0.0f); set (*q, "flutter", 0.0f); set (*q, "vinyl", 0.0f); set (*q, "tone", 20000.0f); set (*q, "bits", 4.0f);
            q->prepareToPlay (kFs, kBlock);
            auto s = sine (4800, 100.0, 0.9f);
            // measure the crush itself: tone filter wide open, compare against the nearest of 2^3 steps
            run (*q, s);
            double dev = 0.0;
            const auto clean = sine (4800, 100.0, 0.9f);
            for (int i = 1000; i < 4800; ++i) dev = juce::jmax (dev, (double) std::abs (s.l[(size_t) i] - clean.l[(size_t) i]));
            logMessage ("  4-bit max error vs clean: " + juce::String (dev, 3));
            expectGreaterThan (dev, 0.04);   // 1/8 steps: errors up to ~0.06 (plus the tone filter's ripple)

            auto w = make ("elite-lofi");
            set (*w, "wow", 100.0f); set (*w, "flutter", 0.0f); set (*w, "vinyl", 0.0f); set (*w, "tone", 20000.0f);
            w->prepareToPlay (kFs, kBlock);
            auto t = sine ((int) kFs * 2, 1000.0, 0.5f);
            run (*w, t);
            // pitch over short windows must wander (tape wow), then come back
            double lo = 1.0e9, hi = 0.0;
            for (int start = (int) kFs / 2; start + 4800 <= t.size(); start += 2400)
            {
                std::vector<float> seg (t.l.begin() + start, t.l.begin() + start + 4800);
                const double f = zeroCrossHz (seg, 0);
                lo = juce::jmin (lo, f); hi = juce::jmax (hi, f);
            }
            logMessage ("  wow 100 %: pitch wanders " + juce::String (lo, 1) + " .. " + juce::String (hi, 1) + " Hz");
            expectGreaterThan (hi - lo, 4.0);
        }

        //------------------------------------------------------------------
        beginTest ("Elite Tune: notes snap to the key / scale");
        {
            expectEquals (EliteTune::snapToScale (69.3f, 0, 0), 69.0f);    // chromatic: nearest semitone
            expectEquals (EliteTune::snapToScale (61.4f, 0, 1), 62.0f);    // C major: C# is not allowed, D is nearer
            expectEquals (EliteTune::snapToScale (66.2f, 0, 1), 67.0f);    // F# -> G
            expectEquals (EliteTune::snapToScale (68.3f, 9, 2), 69.0f);    // A minor: G# -> A
            expectEquals (EliteTune::snapToScale (63.2f, 9, 2), 64.0f);    // A minor: D# (a bit sharp) -> E
            expectEquals (EliteTune::snapToScale (62.8f, 9, 2), 62.0f);    // A minor: D# (a bit flat) -> D
        }
        beginTest ("Elite Tune: a 452 Hz voice (47 cents sharp of A) comes out at 440 Hz; AMOUNT 0 = untouched");
        {
            auto p = make ("elite-tune");
            set (*p, "speed", 0.0f);
            p->prepareToPlay (kFs, kBlock);
            auto x = sine ((int) kFs * 2, 452.0, 0.4f);
            run (*p, x);
            std::vector<float> tail (x.l.begin() + (int) kFs, x.l.end());
            const double hz = zeroCrossHz (tail, 0);
            logMessage ("  in 452 Hz -> out " + juce::String (hz, 2) + " Hz, detected " + juce::String (p->getReadout (EliteTune::detectedHz), 2)
                        + " Hz, correction " + juce::String (p->getReadout (EliteTune::correctionCents), 1) + " cents");
            expectWithinAbsoluteError (hz, 440.0, 2.0);
            expectWithinAbsoluteError ((double) p->getReadout (EliteTune::detectedHz), 452.0, 2.0);
            expectEquals ((int) p->getReadout (EliteTune::targetMidi), 69);

            auto q = make ("elite-tune");
            set (*q, "amount", 0.0f);
            q->prepareToPlay (kFs, kBlock);
            const int lat = q->getLatencySamples();
            expectEquals (lat, (int) std::lround (0.03 * kFs) / 2);
            auto in = noise ((int) kFs, 0.3f);
            auto y = in; run (*q, y);
            double err = 0.0;
            for (int i = lat; i < y.size(); ++i) err = juce::jmax (err, (double) std::abs (y.l[(size_t) i] - in.l[(size_t) (i - lat)]));
            expectLessThan (err, 1.0e-5);
        }

        //------------------------------------------------------------------
        beginTest ("Elite Balance: a mix with a +8 dB bump at 1 kHz gets the 1 kHz band turned down");
        {
            auto p = make ("elite-balance");
            set (*p, "amount", 100.0f); set (*p, "range", 12.0f); set (*p, "speed", 2.0f);
            set (*p, "tilt", 3.0f);   // white noise rises 3 dB per octave in octave bands: that is the target here
            p->prepareToPlay (kFs, kBlock);
            const int n = (int) kFs * 8;
            auto x = noise (n, 0.1f, 17);
            auto bump = BiquadCoeffs::peak (kFs, 1000.0, 1.41, 8.0);
            BiquadState sl, sr;
            for (int i = 0; i < n; ++i) { x.l[(size_t) i] = sl.process (bump, x.l[(size_t) i]); x.r[(size_t) i] = sr.process (bump, x.r[(size_t) i]); }
            run (*p, x);
            juce::String shown;
            int mostCut = 0;
            for (int b = 0; b < EliteBalance::kBands; ++b)
            {
                shown << juce::String (p->getReadout (b), 1) << " ";
                if (p->getReadout (b) < p->getReadout (mostCut)) mostCut = b;
            }
            logMessage ("  band corrections (62.5 Hz .. 8 kHz): " + shown);
            expectEquals (mostCut, 4);
            expectLessThan (p->getReadout (4), -3.0f);
            expectWithinAbsoluteError (p->getReadout (0), 0.0f, 3.0f);

            // silence / freeze: corrections stay where they are
            set (*p, "freeze", 1.0f);
            const float before = p->getReadout (4);
            auto silence = Stereo ((int) kFs);
            run (*p, silence);
            expectWithinAbsoluteError (p->getReadout (4), before, 1.0e-4f);
        }
    }
};

static NewPluginTests newPluginTests;

static RegistryTests registryTests;
static SwirlTests swirlTests;
static RealtimeTests realtimeTests;
static DspTests dspTests;
} // namespace pg::test
