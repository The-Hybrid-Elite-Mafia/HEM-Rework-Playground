// =============================================================================
//  hem_pg_render — replays legacy-render's scenarios through the new C++23 plug-in engines and
//  checks every output against the 0.10.0 fingerprint.
//
//  Usage: hem_pg_render <dir-with-signal.f32-and-scenarios.txt> [--dump <scenario-substring>] [--fixes <known-fixes.txt>]
//  Prints one line per mismatch and a summary; exit code 0 only when every render matches, except
//  renders listed in known-fixes.txt (a bug fixed on purpose), which are reported separately.
//  --dump writes <dir>/new-<n>.f32 for scenarios whose name contains the text (for diagnosis).
// =============================================================================
#include "Registry.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

using namespace hem::pg;

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

struct Scenario
{
    std::string name, pluginId;
    int split = -1;
    std::vector<std::pair<std::string, float>> first, second;
    unsigned long long hash = 0, tailHash = 0;
};

unsigned long long fingerprint (const std::vector<float>& l, const std::vector<float>& r, int from = 0)
{
    unsigned long long h = 1469598103934665603ull;
    auto add = [&h, from] (const std::vector<float>& v) {
        for (std::size_t i = (std::size_t) from; i < v.size(); ++i) { unsigned int b; std::memcpy (&b, &v[i], 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xffu; h *= 1099511628211ull; } }
    };
    add (l); add (r);
    return h;
}
constexpr int kTailFrom = 2048;

std::vector<Scenario> readScenarios (const std::string& path)
{
    std::vector<Scenario> out;
    std::ifstream in (path);
    std::string line;
    while (std::getline (in, line))
    {
        if (line.size() < 2) continue;
        const char tag = line[0];
        const std::string rest = line.substr (2);
        if (tag == 'R')
        {
            Scenario s;
            const auto a = rest.find ('|'), b = rest.rfind ('|');
            s.name = rest.substr (0, a);
            s.pluginId = rest.substr (a + 1, b - a - 1);
            s.split = std::stoi (rest.substr (b + 1));
            out.push_back (std::move (s));
        }
        else if ((tag == 'A' || tag == 'B') && ! out.empty())
        {
            const auto sp = rest.find (' ');
            const float v = (float) std::strtod (rest.c_str() + sp + 1, nullptr);
            (tag == 'A' ? out.back().first : out.back().second).emplace_back (rest.substr (0, sp), v);
        }
        else if (tag == 'H' && ! out.empty())
            out.back().hash = std::stoull (rest, nullptr, 16);
        else if (tag == 'T' && ! out.empty())
            out.back().tailHash = std::stoull (rest, nullptr, 16);
    }
    return out;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: hem_pg_render <dir> [--dump text]\n"); return 2; }
    const std::string dir = argv[1];
    std::string dumpFilter, fixesPath;
    for (int a = 2; a + 1 < argc; a += 2)
    {
        if (std::string (argv[a]) == "--dump") dumpFilter = argv[a + 1];
        else if (std::string (argv[a]) == "--fixes") fixesPath = argv[a + 1];
    }
    struct Fix { std::string plugin, text, why; bool startOnly = false; };
    std::vector<Fix> fixes;
    if (! fixesPath.empty())
    {
        std::ifstream fx (fixesPath);
        std::string ln;
        while (std::getline (fx, ln))
        {
            if (ln.empty() || ln[0] == '#') continue;
            const auto a = ln.find ('|'), b = ln.find ('|', a + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            const auto c = ln.find ('|', b + 1);
            fixes.push_back ({ ln.substr (0, a), ln.substr (a + 1, b - a - 1), ln.substr (b + 1, c == std::string::npos ? std::string::npos : c - b - 1),
                               c != std::string::npos && ln.substr (c + 1) == "start-only" });
        }
    }

    std::ifstream sig (dir + "/signal.f32", std::ios::binary | std::ios::ate);
    if (! sig) { std::fprintf (stderr, "no signal.f32 in %s\n", dir.c_str()); return 2; }
    const auto bytes = (std::size_t) sig.tellg();
    const int len = (int) (bytes / sizeof (float) / 2);
    std::vector<float> inL ((std::size_t) len), inR ((std::size_t) len);
    sig.seekg (0);
    sig.read (reinterpret_cast<char*> (inL.data()), (std::streamsize) (sizeof (float) * (std::size_t) len));
    sig.read (reinterpret_cast<char*> (inR.data()), (std::streamsize) (sizeof (float) * (std::size_t) len));

    const auto scenarios = readScenarios (dir + "/scenarios.txt");
    if (scenarios.empty()) { std::fprintf (stderr, "no scenarios\n"); return 2; }

    std::atomic<float> tempo { 120.0f };
    std::map<std::string, std::pair<int, int>> perPlugin;   // matched, total
    std::map<std::string, double> usPerBlock;
    int matched = 0, dumped = 0, fixedOnPurpose = 0;
    for (const auto& s : scenarios)
    {
        auto p = PluginRegistry::create (s.pluginId);
        if (p == nullptr) { std::printf ("MISSING plug-in %s\n", s.pluginId.c_str()); perPlugin[s.pluginId].second++; continue; }
        p->setTempoSource (&tempo);
        for (auto& [id, v] : s.first)
            if (! p->setRawParameter (id, v)) std::printf ("UNKNOWN control %s on %s\n", id.c_str(), s.pluginId.c_str());
        p->prepare (kFs, kBlock);
        auto l = inL, r = inR;
        bool switched = false;
        int blocks = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int start = 0; start < len; start += kBlock, ++blocks)
        {
            if (s.split >= 0 && ! switched && start >= s.split)
            {
                for (auto& [id, v] : s.second) p->setRawParameter (id, v);
                switched = true;
            }
            p->processStereo (l.data() + start, r.data() + start, std::min (kBlock, len - start));
        }
        const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / blocks;
        if (s.name == "default") usPerBlock[s.pluginId] = us;
        const bool ok = fingerprint (l, r) == s.hash;
        perPlugin[s.pluginId].second++;
        if (ok) { perPlugin[s.pluginId].first++; ++matched; }
        else
        {
            const Fix* known = nullptr;
            const bool tailSame = fingerprint (l, r, kTailFrom) == s.tailHash;
            for (auto& f : fixes)
                if (f.plugin == s.pluginId && s.name.find (f.text) != std::string::npos && (! f.startOnly || tailSame)) known = &f;
            if (known != nullptr) { ++fixedOnPurpose; std::printf ("FIXED %s | %s (%s)\n", s.pluginId.c_str(), s.name.c_str(), known->why.c_str()); }
            else std::printf ("DIFFERS %s | %s\n", s.pluginId.c_str(), s.name.c_str());
        }
        if (! dumpFilter.empty() && s.name.find (dumpFilter) != std::string::npos)
        {
            std::ofstream o (dir + "/new-" + std::to_string (dumped++) + ".f32", std::ios::binary);
            o.write (reinterpret_cast<const char*> (l.data()), (std::streamsize) (sizeof (float) * l.size()));
            o.write (reinterpret_cast<const char*> (r.data()), (std::streamsize) (sizeof (float) * r.size()));
        }
    }
    for (auto& [id, mt] : perPlugin)
        std::printf ("PLUGIN %-18s %4d / %4d renders identical   default render %.1f us per %d-sample block\n", id.c_str(), mt.first, mt.second, usPerBlock[id], kBlock);
    std::printf ("HEM PORT SUMMARY: %d of %zu renders bit-identical to 0.10.0, %d differ only by a listed bug fix, %d differ unexpectedly\n",
                 matched, scenarios.size(), fixedOnPurpose, (int) scenarios.size() - matched - fixedOnPurpose);
    return matched + fixedOnPurpose == (int) scenarios.size() ? 0 : 1;
}
