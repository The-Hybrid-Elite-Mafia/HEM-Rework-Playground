#include "BandSampler.hpp"
#include "hem/pg/Files.hpp"
#include "hem/pg/Json.hpp"
#include "media/MediaLoader.hpp"

#include <cmath>

namespace hem::pg::band
{
namespace
{
using I = Instrument;

const std::pair<const char*, I> kPackNames[] = {
    { "fiddle", I::fiddle }, { "strings", I::strings }, { "uprightBass", I::uprightBass },
    { "piano", I::piano }, { "drums", I::drums }, { "organ", I::organ }, { "acousticGuitar", I::acousticGuitar },
    { "electricGuitar", I::electricGuitar }, { "banjo", I::banjo }, { "mandolin", I::mandolin },
    { "bass", I::bass }, { "harmonica", I::harmonica },
};

SoundPack::Zone readZone (const Json& v, bool drums)
{
    SoundPack::Zone z;
    z.file = v["file"].toString();
    if (drums)
        z.root = z.lo = z.hi = v["note"].toInt();
    else
    {
        z.root = v["root"].toInt();
        z.lo = v["lo"].toInt();
        z.hi = v["hi"].toInt();
        z.loopStart = v.has ("loopStart") ? v["loopStart"].toInt() : -1;
        z.loopEnd = v.has ("loopEnd") ? v["loopEnd"].toInt() : -1;
        z.tuneCents = v.has ("tune") ? jlimit (-60.0f, 60.0f, (float) v["tune"].toDouble()) : 0.0f;
    }
    z.velLo = (float) v["velLo"].toDouble();
    z.velHi = (float) v["velHi"].toDouble();
    z.rr = jmax (1, v["rr"].toInt());
    return z;
}

/** Decodes samples on first use and keeps them for one render. */
class SampleCache
{
public:
    explicit SampleCache (const std::filesystem::path& f) : folder (f) {}

    const AudioBuffer* get (const std::string& rel, std::string& error)
    {
        if (auto it = loaded.find (rel); it != loaded.end()) return &it->second;
        const auto file = folder / files::toPath (rel);
        std::string why;
        auto d = MediaLoader::decodeNative (file, why);
        if (d == nullptr) { error = "Can't read " + files::toText (file); return nullptr; }
        AudioBuffer b (2, d->getNumFrames() + 4);   // 4 frames of guard for interpolation
        b.clear();
        b.copyFrom (0, 0, d->channel (0), d->getNumFrames());
        if (d->getNumChannels() > 1) b.copyFrom (1, 0, d->channel (1), d->getNumFrames());
        else b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        return &(loaded[rel] = std::move (b));
    }

private:
    std::filesystem::path folder;
    std::map<std::string, AudioBuffer> loaded;
};

/** Picks the zone for a note: key range, then velocity layer, then the next round robin. */
class ZonePicker
{
public:
    const SoundPack::Zone* pick (const std::vector<SoundPack::Zone>& zones, int pitch, float vel)
    {
        if (zones.empty()) return nullptr;
        // key: zones covering the pitch, else the nearest root
        int bestRoot = -1, bestDist = 1 << 30;
        for (auto& z : zones)
        {
            const int d = (pitch >= z.lo && pitch <= z.hi) ? -1 : std::abs (z.root - pitch);
            if (d < bestDist) { bestDist = d; bestRoot = z.root; }
        }
        // velocity: the layer containing vel, else the nearest layer
        float bestLayer = -1.0f, bestVd = 1.0e9f;
        for (auto& z : zones)
        {
            if (z.root != bestRoot) continue;
            const bool inside = vel >= z.velLo && (vel < z.velHi || (z.velHi >= 0.999f && vel >= z.velHi));
            const float vd = inside ? -1.0f : jmin (std::abs (vel - z.velLo), std::abs (vel - z.velHi));
            if (vd < bestVd) { bestVd = vd; bestLayer = z.velLo; }
        }
        std::vector<const SoundPack::Zone*> group;
        for (auto& z : zones)
            if (z.root == bestRoot && z.velLo == bestLayer) group.push_back (&z);
        if (group.empty()) return nullptr;
        auto& counter = rrCounter[(bestRoot << 12) ^ (int) (bestLayer * 1000.0f)];
        return group[(size_t) (counter++ % group.size())];
    }

private:
    std::map<int, unsigned> rrCounter;
};

/** Cubic Hermite read at fractional `pos`; inside a loop the neighbours wrap to the loop start
    (the pack's looped files end exactly at loopEnd). */
inline float hermite (const float* d, double pos, int loopStart, int loopEnd) noexcept
{
    const int i = (int) pos;
    const float t = (float) (pos - i);
    auto at = [&] (int k) { if (loopEnd > 0 && k >= loopEnd) k -= loopEnd - loopStart; return d[jmax (0, k)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

/** Plays one zone into out from `start`. noteLen/release in output samples; maxLen caps the voice
    (hi-hat choke); step = source frames per output sample. */
void playVoice (const AudioBuffer& src, const SoundPack::Zone& z, double step, int start,
                int noteLen, int release, int maxLen, float gainL, float gainR, AudioBuffer& out)
{
    const int total = out.getNumSamples();
    const int srcLen = src.getNumSamples() - 4;
    const bool loop = z.loopStart >= 0 && z.loopEnd > z.loopStart + 64 && z.loopEnd <= srcLen;
    const double loopLen = loop ? (double) (z.loopEnd - z.loopStart) : 0.0;
    const int ls = loop ? z.loopStart : 0, le = loop ? z.loopEnd : 0;
    const float* a = src.getReadPointer (0);
    const float* b = src.getReadPointer (1);
    float* l = out.getWritePointer (0);
    float* r = out.getWritePointer (1);
    const int chokeFade = jmax (1, jmin (maxLen, 960));
    double pos = 0.0;
    for (int k = 0; start + k < total && k < maxLen; ++k)
    {
        float env = 1.0f;
        if (k >= noteLen)
        {
            if (release <= 0) break;
            env = 1.0f - (float) (k - noteLen) / (float) release;
            if (env <= 0.0f) break;
            env *= env;   // faster tail, like a damper
        }
        if (maxLen - k < chokeFade) env *= (float) (maxLen - k) / (float) chokeFade;
        if (loop) { while (pos >= z.loopEnd) pos -= loopLen; }
        else if (pos >= srcLen - 1) break;
        const int o = start + k;
        if (o >= 0)
        {
            l[o] += hermite (a, pos, ls, le) * env * gainL;
            r[o] += hermite (b, pos, ls, le) * env * gainR;
        }
        pos += step;
    }
}

float velocityGain (float vel) noexcept
{
    return std::pow (10.0f, -6.0f * (1.0f - jlimit (0.0f, 1.0f, vel)) / 20.0f);
}
} // namespace

bool SoundPack::open (const std::filesystem::path& f, std::string& error)
{
    instruments.clear();
    folder = f;
    const auto text = files::readText (f / "pack.json");
    const auto parsed = text ? Json::parse (*text) : std::nullopt;
    if (! parsed || ! parsed->isObject()) { error = "No pack.json in " + files::toText (f); return false; }
    const auto& json = *parsed;
    packRate = json.has ("sampleRate") ? json["sampleRate"].toDouble() : 48000.0;
    const auto& inst = json["instruments"];
    for (auto& [name, id] : kPackNames)
    {
        const auto& v = inst[name];
        if (! v.isObject()) continue;
        InstrumentDef def;
        def.drums = v["type"].toString() == "drums";
        def.release = v.has ("release") ? (float) v["release"].toDouble() : 0.15f;
        for (auto& z : v["zones"].items()) def.zones.push_back (readZone (z, def.drums));
        for (auto& z : v["short"].items()) def.shortZones.push_back (readZone (z, false));
        if (! def.zones.empty()) instruments[id] = std::move (def);
    }
    if (instruments.empty()) { error = "pack.json lists no instruments"; return false; }
    return true;
}

bool SoundPack::has (Instrument i) const { return instruments.count (i) != 0; }

std::string SoundPack::describe() const
{
    std::string out;
    for (auto& [id, def] : instruments)
    {
        std::string n = instrumentName (id);
        for (auto& c : n) c = (char) std::tolower ((unsigned char) c);
        out += (out.empty() ? "" : ", ") + n;
    }
    return out;
}

std::vector<std::filesystem::path> SoundPack::searchFolders()
{
    return {
        files::executableFolder() / "Sounds" / "HEM Sound Pack",
        files::playgroundDocuments() / "Sounds" / "HEM Sound Pack",
    };
}

std::shared_ptr<const SoundPack> SoundPack::findInstalled()
{
    auto pack = std::make_shared<SoundPack>();
    std::string ignored;
    std::error_code ec;
    for (auto& f : searchFolders())
        if (std::filesystem::is_regular_file (f / "pack.json", ec) && pack->open (f, ignored))
            return pack;
    return std::make_shared<SoundPack>();
}

bool SoundPack::renderPart (const Song& song, const Part& part, double sr, AudioBuffer& out,
                            std::vector<Note>& notCovered, const std::atomic<bool>* cancel, std::string& error) const
{
    const auto it = instruments.find (part.instrument);
    if (it == instruments.end()) { error = std::string ("The sound pack has no ") + instrumentName (part.instrument); return false; }
    const auto& def = it->second;
    const double spb = song.secondsPerBeat();
    const double rateRatio = packRate / sr;
    const float pl = 1.0f - jmax (0.0f, part.pan), pr = 1.0f + jmin (0.0f, part.pan);
    SampleCache cache (folder);
    ZonePicker picker, shortPicker;
    const int release = (int) (def.release * sr);

    std::vector<bool> covered (128, false);
    if (def.drums)
        for (auto& z : def.zones) if (z.root >= 0 && z.root < 128) covered[(size_t) z.root] = true;

    for (size_t i = 0; i < part.notes.size(); ++i)
    {
        if ((i & 31) == 0 && cancel != nullptr && cancel->load()) return false;
        const auto& n = part.notes[i];
        const int start = (int) std::llround (n.start * spb * sr);
        if (start >= out.getNumSamples()) continue;
        const int noteLen = jmax (1, (int) std::llround (n.length * spb * sr));

        if (def.drums)
        {
            if (n.pitch < 0 || n.pitch >= 128 || ! covered[(size_t) n.pitch]) { notCovered.push_back (n); continue; }
            const auto* z = picker.pick (def.zones, n.pitch, n.velocity);
            if (z == nullptr) { notCovered.push_back (n); continue; }
            const auto* src = cache.get (z->file, error);
            if (src == nullptr) return false;
            int maxLen = 1 << 30;
            if (n.pitch == drum::hatOpen)   // the next closed or pedal hat chokes an open hat
                for (size_t j = i + 1; j < part.notes.size(); ++j)
                    if (part.notes[j].pitch == drum::hatClosed || part.notes[j].pitch == drum::hatPedal)
                    {
                        maxLen = jmax (1, (int) std::llround ((part.notes[j].start - n.start) * spb * sr) + (int) (0.02 * sr));
                        break;
                    }
            const float g = 0.7f * velocityGain (n.velocity);
            playVoice (*src, *z, rateRatio, start, 1 << 30, 0, maxLen, g * pl, g * pr, out);
            continue;
        }

        const double seconds = n.length * spb;
        const bool useShort = ! def.shortZones.empty() && (seconds < 0.25 || n.articulation == art::chop);
        const auto* z = useShort ? shortPicker.pick (def.shortZones, n.pitch, n.velocity)
                                 : picker.pick (def.zones, n.pitch, n.velocity);
        if (z == nullptr) continue;
        const auto* src = cache.get (z->file, error);
        if (src == nullptr) return false;
        const double step = std::pow (2.0, (n.pitch - z->root + z->tuneCents / 100.0) / 12.0) * rateRatio;
        const float g = 0.5f * velocityGain (n.velocity);
        playVoice (*src, *z, step, start, noteLen, release, 1 << 30, g * pl, g * pr, out);
    }
    return true;
}
} // namespace hem::pg::band
