// =============================================================================
//  legacy-render — renders the 0.10.0 Playground plug-ins (the JUCE code in Source/) through a
//  fixed list of scenarios and writes, for each one, every control's exact value and a
//  fingerprint of the output. hem_pg_render (the new C++23 engines) replays the same file and must
//  produce the same fingerprints. This tool exists only to prove the port; the app never uses it.
//
//  Usage: legacy-render <out-dir> [--dump <scenario-substring>]  (--dump writes old-<n>.f32 for diagnosis)
//         legacy-render <out-dir> --rate <Hz>  (the same scenarios at another sample rate)
//         legacy-render <out-dir> --sessions   (mixer, sessions, media and export: see sessions.cpp)
//    <out-dir>/signal.f32      the 6 s stereo test signal (left block, then right block, float32)
//    <out-dir>/scenarios.txt   one block per render (see writeScenario)
// =============================================================================
#include "Plugins/PluginRegistry.h"
#include <cstdio>
#include <cstring>

using namespace pg;

namespace
{
double kFs = 48000.0;          // --rate changes it (the default and the plug-ins' own checks are at 48 kHz)
constexpr int kBlock = 512;
int kLen = (int) (6 * 48000.0);

struct Stereo { std::vector<float> l, r; };

Stereo makeSignal()
{
    Stereo s { std::vector<float> ((size_t) kLen), std::vector<float> ((size_t) kLen) };
    juce::Random rng (0x48454D);
    double vphase = 0.0, padA = 0.0, padB = 0.0;
    float swirlEnv = 0, prevHat = 0, sAcc = 0, prevSwirl = 0;
    for (int i = 0; i < kLen; ++i)
    {
        const double t = i / kFs;
        float l = 0, r = 0;
        const double tk = std::fmod (t, 0.5);   // kick every 0.5 s
        const float kick = (float) (std::sin (2 * juce::MathConstants<double>::pi * (50 + 120 * std::exp (-tk * 40)) * tk) * std::exp (-tk * 9) * 0.5);
        l += kick; r += kick;
        const double ts = std::fmod (t + 0.25, 0.5);   // snare on the off-beat
        if (std::fmod (t + 0.25, 1.0) < 0.5) { const float n = (rng.nextFloat() * 2 - 1) * (float) std::exp (-ts * 25) * 0.25f; l += n * 0.9f; r += n * 1.1f; }
        const double th = std::fmod (t, 0.125);        // hats every 0.125 s
        { const float n = rng.nextFloat() * 2 - 1; const float hp = n - prevHat; prevHat = n; const float h = hp * (float) std::exp (-th * 70) * 0.08f; l += h * 1.2f; r += h * 0.8f; }
        // sung-like tone 35 cents sharp of A3 with 5 Hz vibrato, and an "s" near the end of every second
        const double f0 = 220.0 * std::pow (2.0, 0.35 / 12.0) * (1.0 + 0.004 * std::sin (2 * juce::MathConstants<double>::pi * 5 * t));
        vphase += f0 / kFs; vphase -= std::floor (vphase);
        float v = 0; for (int k = 1; k <= 12; ++k) v += (float) (std::sin (2 * juce::MathConstants<double>::pi * k * vphase) / (k * (k > 3 ? 1.6 : 1.0)));
        v *= 0.06f * (0.6f + 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * 0.25 * t) * 0.5f + 0.2f);
        l += v; r += v;
        if (std::fmod (t, 1.0) > 0.85) { const float s2 = (rng.nextFloat() * 2 - 1) * 0.12f; sAcc = s2 - sAcc * 0.3f; l += sAcc; r += sAcc; }
        padA += 110.0 * 1.003 / kFs; padA -= std::floor (padA);   // wide pad
        padB += 110.0 * 0.997 / kFs; padB -= std::floor (padB);
        l += (float) (padA * 2 - 1) * 0.04f; r += (float) (padB * 2 - 1) * 0.04f;
        if (i % 480 == 0) swirlEnv = rng.nextFloat();              // flickering side-only highs
        { const float n = rng.nextFloat() * 2 - 1; const float hp = n - prevSwirl; prevSwirl = n; l += hp * swirlEnv * 0.02f; r -= hp * swirlEnv * 0.02f; }
        l += (rng.nextFloat() * 2 - 1) * 0.001f;                   // hiss about -60 dBFS
        r += (rng.nextFloat() * 2 - 1) * 0.001f;
        s.l[(size_t) i] = l; s.r[(size_t) i] = r;
    }
    return s;
}

std::atomic<float> tempo { 120.0f };
juce::String dumpFilter;   // optional: write the outputs of scenarios whose name contains this text
juce::File dumpDir;
int dumped = 0;

/** FNV-1a over the float bits of both channels, from sample `from` on. */
unsigned long long fingerprint (const Stereo& s, int from = 0)
{
    unsigned long long h = 1469598103934665603ull;
    auto add = [&h, from] (const std::vector<float>& v) {
        for (size_t i = (size_t) from; i < v.size(); ++i) { unsigned int b; std::memcpy (&b, &v[i], 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xffu; h *= 1099511628211ull; } }
    };
    add (s.l); add (s.r);
    return h;
}
constexpr int kTailFrom = 2048;   // the second fingerprint skips the first 2048 samples (start-up only)

void dumpValues (FILE* f, char tag, ElitePlugin& p)
{
    for (auto* prm : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            std::fprintf (f, "%c %s %a\n", tag, rp->getParameterID().toRawUTF8(), (double) p.getAPVTS().getRawParameterValue (rp->getParameterID())->load());
}

void setParam (ElitePlugin& p, const juce::String& id, float realValue)
{
    if (auto* rp = p.getAPVTS().getParameter (id))
        rp->setValueNotifyingHost (rp->convertTo0to1 (realValue));
}

struct Change { juce::String id; float value; };

/** One render: settings applied before prepare, optional second set applied at `split` samples. */
void writeScenario (FILE* f, const Stereo& in, const juce::String& name, const juce::String& pluginId,
                    const std::vector<Change>& first, const FactoryPreset* preset, int split, const std::vector<Change>& second)
{
    auto p = PluginRegistry::create (pluginId);
    p->setTempoSource (&tempo);
    if (preset != nullptr) Presets::apply (*p, *preset);
    for (auto& c : first) setParam (*p, c.id, c.value);
    std::fprintf (f, "R %s|%s|%d\n", name.toRawUTF8(), pluginId.toRawUTF8(), split);
    dumpValues (f, 'A', *p);
    p->prepareToPlay (kFs, kBlock);
    Stereo out = in;
    bool switched = false;
    for (int start = 0; start < kLen; start += kBlock)
    {
        if (split >= 0 && ! switched && start >= split)
        {
            for (auto& c : second) setParam (*p, c.id, c.value);
            dumpValues (f, 'B', *p);
            switched = true;
        }
        p->processStereo (out.l.data() + start, out.r.data() + start, juce::jmin (kBlock, kLen - start));
    }
    std::fprintf (f, "H %016llx\n", fingerprint (out));
    std::fprintf (f, "T %016llx\n", fingerprint (out, kTailFrom));
    if (dumpFilter.isNotEmpty() && name.contains (dumpFilter))
    {
        juce::FileOutputStream o (dumpDir.getChildFile ("old-" + juce::String (dumped++) + ".f32"));
        o.setPosition (0); o.truncate();
        o.write (out.l.data(), sizeof (float) * (size_t) kLen);
        o.write (out.r.data(), sizeof (float) * (size_t) kLen);
    }
}

std::vector<std::pair<float, juce::String>> candidatesFor (juce::RangedAudioParameter& rp)
{
    std::vector<std::pair<float, juce::String>> c;
    const auto range = rp.getNormalisableRange();
    const float def = rp.convertFrom0to1 (rp.getDefaultValue());
    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (&rp))
    {
        for (int i = 0; i < ch->choices.size(); ++i)
            if ((float) i != def) c.push_back ({ (float) i, ch->choices[i] });
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (&rp) != nullptr)
        c.push_back ({ def > 0.5f ? 0.0f : 1.0f, def > 0.5f ? "off" : "on" });
    else
        for (float v : { range.start, range.end, rp.convertFrom0to1 (0.25f), rp.convertFrom0to1 (0.75f) })
            if (std::abs (v - def) > 1e-6f * (1.0f + std::abs (def)))
                c.push_back ({ v, juce::String (v, 3) });
    return c;
}
} // namespace

int runSessions (const juce::File& dir);   // sessions.cpp

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();
    if (argc > 2 && juce::String (argv[2]) == "--sessions") return runSessions (outDir);
    if (argc > 3 && juce::String (argv[2]) == "--rate")
    {
        kFs = juce::String (argv[3]).getDoubleValue();
        kLen = (int) (6 * kFs);
    }
    outDir.getChildFile ("rate.txt").replaceWithText (juce::String (kFs, 0));
    if (argc > 3 && juce::String (argv[2]) == "--dump") { dumpFilter = argv[3]; dumpDir = outDir; }
    const auto in = makeSignal();
    {
        juce::FileOutputStream sig (outDir.getChildFile ("signal.f32"));
        sig.setPosition (0); sig.truncate();
        sig.write (in.l.data(), sizeof (float) * (size_t) kLen);
        sig.write (in.r.data(), sizeof (float) * (size_t) kLen);
    }
    FILE* f = std::fopen (outDir.getChildFile ("scenarios.txt").getFullPathName().toRawUTF8(), "w");
    if (f == nullptr) return 1;
    int count = 0;
    for (auto* info : PluginRegistry::all())
    {
        const juce::String id = info->id;
        writeScenario (f, in, "default", id, {}, nullptr, -1, {}); ++count;
        writeScenario (f, in, "bypass", id, { { "bypass", 1.0f } }, nullptr, -1, {}); ++count;
        writeScenario (f, in, "bypass-at-3s", id, {}, nullptr, (int) (3 * kFs), { { "bypass", 1.0f } }); ++count;
        auto probe = PluginRegistry::create (id);
        for (auto* prm : probe->getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm);
            if (rp == nullptr || rp->getParameterID() == "bypass") continue;
            for (auto& [v, label] : candidatesFor (*rp))
            {
                const auto n = rp->getParameterID() + "=" + label;
                writeScenario (f, in, n, id, { { rp->getParameterID(), v } }, nullptr, -1, {}); ++count;
                writeScenario (f, in, n + "@3s", id, {}, nullptr, (int) (3 * kFs), { { rp->getParameterID(), v } }); ++count;
            }
        }
        for (auto& pr : Presets::factory (id))
        {
            writeScenario (f, in, "preset:" + pr.name, id, {}, &pr, -1, {}); ++count;
        }
        std::fflush (f);
    }
    std::fclose (f);
    std::printf ("legacy-render: %d scenarios\n", count);
    return 0;
}
