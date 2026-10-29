// =============================================================================
//  hem_pg_bench — measures CPU time per audio block for every plug-in engine and for a full mix.
//  For each case: median and 99th-percentile microseconds per 512-sample block, and that time as a
//  share of the block's real-time length (10.67 ms at 48 kHz, 5.33 ms at 96 kHz).
//  The numbers are for the machine it runs on (a cloud runner is not Steven's i7-14700HX).
// =============================================================================
#include "PlaygroundEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace hem::pg;

namespace
{
constexpr int kBlock = 512;

struct Result { double median, p99; };

template <typename Fn>
Result timeBlocks (Fn&& run, int blocks)
{
    for (int i = 0; i < 50; ++i) run();   // warm up
    std::vector<double> us ((size_t) blocks);
    for (int i = 0; i < blocks; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        run();
        const auto t1 = std::chrono::steady_clock::now();
        us[(size_t) i] = std::chrono::duration<double, std::micro> (t1 - t0).count();
    }
    std::sort (us.begin(), us.end());
    return { us[us.size() / 2], us[(size_t) ((double) us.size() * 0.99)] };
}

/** Puts a plug-in to work: every control a quarter of the way from its start to its top (all switches on). */
void wakeUp (Plugin& p)
{
    for (auto& prm : p.parameters().all())
    {
        if (prm->id() == "bypass") continue;
        if (prm->spec().type == ParamSpec::Type::toggle) { prm->setValue (1.0f); continue; }
        if (prm->spec().type == ParamSpec::Type::choice) continue;
        prm->setNormalised (jlimit (0.0f, 1.0f, prm->getNormalised() + 0.25f));
    }
}
}

int main()
{
    std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
    unsigned seed = 1;
    auto refill = [&]
    {
        for (int i = 0; i < kBlock; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float n = (float) (seed >> 8) / 16777216.0f * 2.0f - 1.0f;
            l[(size_t) i] = 0.3f * n; r[(size_t) i] = -0.25f * n;
        }
    };

    for (double fs : { 48000.0, 96000.0 })
    {
        const double blockUs = kBlock / fs * 1.0e6;
        for (auto* info : PluginRegistry::all())
        {
            auto p = PluginRegistry::create (info->id);
            std::atomic<float> bpm { 120.0f };
            p->setTempoSource (&bpm);
            wakeUp (*p);
            p->prepare (fs, kBlock);
            const auto res = timeBlocks ([&] { refill(); p->processStereo (l.data(), r.data(), kBlock); }, 2000);
            std::printf ("BENCH %s @ %.0f kHz: median %.1f us, 99%% %.1f us per 512-sample block = %.2f %% of real time\n",
                         info->name, fs / 1000.0, res.median, res.p99, 100.0 * res.median / blockUs);
        }

        // a full song: 32 tracks with 3 inserts each, sends, 6 buses with a compressor, Elite Master
        PlaygroundEngine e;
        e.prepare (fs, kBlock);
        e.newSession();
        const StringArray ids { "elite-vox", "elite-eq", "elite-comp", "elite-bass", "elite-channel", "elite-verb", "elite-delay" };
        for (int t = 0; t < PlaygroundEngine::kMaxTracks; ++t)
        {
            std::vector<float> a ((size_t) (fs * 4.0));
            for (size_t i = 0; i < a.size(); ++i) a[i] = 0.05f * (float) std::sin (MathConstants<double>::twoPi * (100.0 + 10.0 * t) * (double) i / fs);
            e.addTrack (std::make_shared<const SampleData> (std::vector<std::vector<float>> { a }, fs, std::filesystem::path()), "T");
            for (int s = 0; s < 3; ++s) e.insertPlugin (t, s, ids[(size_t) (t + s) % ids.size()]);
            e.setMixer (PlaygroundEngine::Ids::track (t, "sa"), -10.0f);
            e.setMixer (PlaygroundEngine::Ids::track (t, "sb"), -14.0f);
        }
        for (int b = 0; b < PlaygroundEngine::kNumBuses; ++b) e.insertPlugin (PlaygroundEngine::busStrip (b), 1, "elite-comp");
        e.setLoop (true, 0.0, 3.5);
        e.play();
        float* outs[2] { l.data(), r.data() };
        const auto res = timeBlocks ([&] { e.process (outs, 2, kBlock); }, 1000);
        std::printf ("BENCH full mix (32 tracks x 3 inserts, 6 bus comps, reverb, delay, Elite Master) @ %.0f kHz: median %.1f us, 99%% %.1f us per 512-sample block = %.1f %% of one core\n",
                     fs / 1000.0, res.median, res.p99, 100.0 * res.median / blockUs);
    }
    return 0;
}
