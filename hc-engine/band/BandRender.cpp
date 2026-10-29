#include "BandRender.hpp"
#include "EliteMaster.hpp"
#include "Loudness.hpp"
#include "Registry.hpp"
#include "export/WavFile.hpp"
#include "hem/pg/Files.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace hem::pg::band
{
bool renderSong (const Song& song, double sr, std::vector<Stem>& stems, const std::function<void (float)>& progress,
                 const std::atomic<bool>* cancel, const SoundPack* pack)
{
    stems.clear();
    const int total = (int) std::ceil (song.lengthSeconds() * sr);
    const int n = (int) song.parts.size();
    for (int i = 0; i < n; ++i)
    {
        Stem s;
        s.instrument = song.parts[(size_t) i].instrument;
        s.audio.setSize (2, total);
        s.audio.clear();
        const auto& part = song.parts[(size_t) i];
        bool done = false;
        if (pack != nullptr && pack->has (part.instrument))
        {
            Part leftover = part;
            leftover.notes.clear();
            std::string error;
            if (pack->renderPart (song, part, sr, s.audio, leftover.notes, cancel, error))
            {
                s.recorded = done = true;
                if (! leftover.notes.empty() && ! renderPart (song, leftover, sr, s.audio, cancel)) return false;
            }
            else
            {
                if (cancel != nullptr && cancel->load()) return false;
                s.audio.clear();   // fall back to the modeled instrument
            }
        }
        if (! done && ! renderPart (song, part, sr, s.audio, cancel)) return false;
        applySectionDynamics (song, s, sr);
        levelStem (s, sr);
        stems.push_back (std::move (s));
        if (progress) progress ((float) (i + 1) / (float) jmax (1, n));
    }
    return true;
}

namespace
{
/** dB of lift per unit of section energy, by the instrument's job in the band. */
float dynamicsDepthDb (Instrument i)
{
    switch (i)
    {
        case Instrument::drums: case Instrument::trapDrums: return 6.0f;
        case Instrument::bass: case Instrument::uprightBass: case Instrument::bass808: return 5.0f;
        case Instrument::strings: case Instrument::organ: return 10.0f;
        case Instrument::fiddle: case Instrument::harmonica: case Instrument::electricGuitar: return 6.0f;
        case Instrument::acousticGuitar: case Instrument::banjo: case Instrument::mandolin: case Instrument::piano: return 8.0f;
        case Instrument::count: break;
    }
    return 7.0f;
}

/** dB -> gain the way 0.10.0 did it (silence at -100 dB and below). */
float decibelsToGain (float db) noexcept { return db > -100.0f ? std::pow (10.0f, db * 0.05f) : 0.0f; }
float gainToDecibels (float g) noexcept { return g > 0.0f ? jmax (-100.0f, std::log10 (g) * 20.0f) : -100.0f; }

std::string oneDecimal (float v)
{
    char b[32];
    std::snprintf (b, sizeof b, "%.1f", (double) v);
    return b;
}
} // namespace

float sectionGainDb (const Song& song, Instrument inst, int index)
{
    if (! isPositiveAndBelow (index, (int) song.sections.size())) return 0.0f;
    float top = 0.0f;
    for (auto& s : song.sections) top = jmax (top, s.energy);
    return dynamicsDepthDb (inst) * (song.sections[(size_t) index].energy - top);
}

void applySectionDynamics (const Song& song, Stem& stem, double sr)
{
    const int nSec = (int) song.sections.size();
    const int len = stem.audio.getNumSamples();
    if (nSec == 0 || len == 0) return;
    const double spb = song.secondsPerBeat();
    std::vector<double> startSec ((size_t) nSec + 1);
    std::vector<float> g ((size_t) nSec);
    for (int i = 0; i < nSec; ++i)
    {
        startSec[(size_t) i] = song.sections[(size_t) i].startBar * song.beatsPerBar * spb;
        g[(size_t) i] = sectionGainDb (song, stem.instrument, i);
    }
    startSec[(size_t) nSec] = 1.0e9;
    const double swell = 2.0 * spb, ease = 0.5 * spb, jump = 0.03;

    // gain (dB) at time t
    auto gainAt = [&] (double t)
    {
        int i = 0;
        while (i + 1 < nSec && t >= startSec[(size_t) i + 1]) ++i;
        const float here = g[(size_t) i];
        if (i > 0)   // just after a boundary
        {
            const float prev = g[(size_t) i - 1];
            const double since = t - startSec[(size_t) i];
            if (here >= prev)
            {
                const float from = prev + 0.5f * (here - prev);   // where the swell got to
                if (since < jump) return from + (here - from) * (float) (since / jump);
            }
            else if (since < ease)
                return prev + (here - prev) * (float) (since / ease);
        }
        if (i + 1 < nSec && g[(size_t) i + 1] > here)   // swell into a bigger section
        {
            const double until = startSec[(size_t) i + 1] - t;
            if (until < swell) return here + 0.5f * (g[(size_t) i + 1] - here) * (float) (1.0 - until / swell);
        }
        return here;
    };

    constexpr int step = 32;
    float prevGain = decibelsToGain (gainAt (0.0));
    for (int st = 0; st < len; st += step)
    {
        const int m = jmin (step, len - st);
        const float next = decibelsToGain (gainAt ((st + m) / sr));
        for (int ch = 0; ch < stem.audio.getNumChannels(); ++ch)
            stem.audio.applyGainRamp (ch, st, m, prevGain, next);
        prevGain = next;
    }
}

void levelStem (Stem& s, double sr)
{
    auto& a = s.audio;
    const int len = a.getNumSamples();
    if (len == 0) return;
    // RMS over 50 ms blocks that are actually playing (above -50 dB of the loudest block)
    const int block = jmax (1, (int) (0.05 * sr));
    std::vector<double> blockMs;
    double loudest = 0.0;
    for (int st = 0; st < len; st += block)
    {
        const int m = jmin (block, len - st);
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch) { const auto* d = a.getReadPointer (ch, st); for (int k = 0; k < m; ++k) sum += (double) d[k] * d[k]; }
        const double ms = sum / (2.0 * m);
        blockMs.push_back (ms);
        loudest = jmax (loudest, ms);
    }
    if (loudest <= 1.0e-14) return;
    double sum = 0.0; int count = 0;
    for (double ms : blockMs) if (ms > loudest * 1.0e-5) { sum += ms; ++count; }
    const double rms = std::sqrt (sum / jmax (1, count));
    float gain = (float) (std::pow (10.0, stemTargetRmsDb (s.instrument) / 20.0) / jmax (1.0e-9, rms));
    const float peak = a.getMagnitude (0, len) * gain;
    const float ceiling = std::pow (10.0f, -1.0f / 20.0f);
    a.applyGain (gain);
    if (peak > ceiling)
    {
        // gentle soft-knee limiting of the few loudest peaks rather than turning the whole stem down
        const float knee = ceiling * 0.7f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = a.getWritePointer (ch);
            for (int k = 0; k < len; ++k)
            {
                const float x = d[k], ax = std::abs (x);
                if (ax > knee) d[k] = std::copysign (knee + (ceiling - knee) * std::tanh ((ax - knee) / (ceiling - knee)), x);
            }
        }
    }
}

namespace
{
bool writeWav24 (const std::filesystem::path& file, const AudioBuffer& b, int start, int count, double sr, std::string& error)
{
    std::error_code ec;
    std::filesystem::remove (file, ec);
    WavWriter w;
    if (! w.open (file, sr, 2, 24, error)) { error = "Can't write " + files::toText (file); return false; }
    const float* chans[2] { b.getReadPointer (0, start), b.getReadPointer (1, start) };
    if (! w.write (chans, count) || ! w.close()) { error = "Writing " + files::toText (file.filename()) + " failed"; return false; }
    return true;
}
} // namespace

std::vector<std::filesystem::path> writeStems (const Song& song, const std::vector<Stem>& stems, double sr, const std::filesystem::path& folder, std::string& error)
{
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    std::filesystem::create_directories (folder, ec);
    if (! std::filesystem::is_directory (folder, ec)) { error = "Can't create " + files::toText (folder); return out; }
    for (size_t i = 0; i < stems.size(); ++i)
    {
        char num[16];
        std::snprintf (num, sizeof num, "%02d", (int) i + 1);
        const auto name = std::string (num) + " " + instrumentName (stems[i].instrument) + ".wav";
        const auto file = folder / files::toPath (name);
        if (! writeWav24 (file, stems[i].audio, 0, stems[i].audio.getNumSamples(), sr, error)) return {};
        out.push_back (file);
    }
    writeMidi (song, folder / "Band Forge.mid");
    return out;
}

bool writeMix (const std::vector<Stem>& stems, double sr, MixFinish finish, const std::filesystem::path& file, std::string& info, std::string& error)
{
    if (finish == MixFinish::none || stems.empty()) return true;
    const int len = stems[0].audio.getNumSamples();
    AudioBuffer mix (2, len);
    mix.clear();
    for (auto& s : stems)
        for (int ch = 0; ch < 2; ++ch) mix.addFrom (ch, 0, s.audio, ch, 0, jmin (len, s.audio.getNumSamples()));

    if (finish == MixFinish::clean)
    {
        const float peak = mix.getMagnitude (0, len), limit = decibelsToGain (-3.0f);
        if (peak > limit) mix.applyGain (limit / peak);
        info = "clean, peak " + oneDecimal (gainToDecibels (jmin (peak, limit))) + " dBFS";
        return writeWav24 (file, mix, 0, len, sr, error);
    }

    // MASTERED: the sum is first set to -20 LUFS (Elite Master's INPUT only adds gain), then Elite
    // Master's input is matched to -14 LUFS in a few passes (the limiter makes it non-linear)
    {
        dsp::LoudnessMeter lm;
        lm.prepare (sr);
        lm.process (mix.getReadPointer (0), mix.getReadPointer (1), len);
        if (lm.getIntegrated() > -70.0f) mix.applyGain (decibelsToGain (-20.0f - lm.getIntegrated()));
    }
    auto plugin = PluginRegistry::create ("elite-master");
    auto* master = dynamic_cast<EliteMaster*> (plugin.get());
    if (master == nullptr) { error = "Elite Master is missing"; return false; }
    auto* input = master->parameters().find ("input");
    constexpr int block = 1024;
    AudioBuffer out;
    int latency = 0;
    float integrated = -100.0f;
    for (int pass = 0; pass < 4; ++pass)
    {
        master->prepare (sr, block);
        master->resetMeters();
        latency = master->getLatencySamples();
        out.setSize (2, len + latency, false, false, true);
        out.clear();
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, mix, ch, 0, len);
        auto ptrs = out.getArrayOfWritePointers();
        for (int st = 0; st < out.getNumSamples(); st += block)
            master->processStereo (ptrs[0] + st, ptrs[1] + st, jmin (block, out.getNumSamples() - st));
        integrated = master->getReadout (EliteMaster::integrated);
        if (integrated <= -70.0f) break;
        const float now = input->spec().range.valueFrom01 (input->getNormalised());
        const float next = jlimit (0.0f, 18.0f, now + (EliteMaster::targetLufs (0) - integrated));
        if (std::abs (next - now) < 0.1f || pass == 3) break;
        input->setValue (next);
    }
    const float peak = out.getMagnitude (latency, len);
    info = "mastered, " + oneDecimal (integrated) + " LUFS, peak " + oneDecimal (gainToDecibels (peak)) + " dBFS";
    return writeWav24 (file, out, latency, len, sr, error);
}

//==============================================================================
namespace
{
/** A MIDI event as 0.10.0 kept it: a time in ticks (fractional) and its bytes. */
struct MidiEvent { double tick; std::vector<unsigned char> bytes; };

/** Adds after every event at the same or an earlier time (the order 0.10.0's MIDI lists used). */
void addEvent (std::vector<MidiEvent>& list, MidiEvent e)
{
    int i = (int) list.size();
    while (--i >= 0)
        if (list[(size_t) i].tick <= e.tick) break;
    list.insert (list.begin() + (i + 1), std::move (e));
}

bool isNoteOn (const MidiEvent& e) { return (e.bytes[0] & 0xf0) == 0x90 && e.bytes.size() > 2 && e.bytes[2] != 0; }
bool isNoteOff (const MidiEvent& e) { return (e.bytes[0] & 0xf0) == 0x80 || ((e.bytes[0] & 0xf0) == 0x90 && e.bytes.size() > 2 && e.bytes[2] == 0); }
int channelOf (const MidiEvent& e) { return (e.bytes[0] & 0xf0) != 0xf0 ? (e.bytes[0] & 0xf) + 1 : 0; }

/** A note-on of a pitch that is still sounding gets a note-off right before it (as 0.10.0 did). */
void matchPairs (std::vector<MidiEvent>& list)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (! isNoteOn (list[i])) continue;
        const int note = list[i].bytes[1], chan = channelOf (list[i]);
        const size_t len = list.size();
        for (size_t j = i + 1; j < len; ++j)
        {
            const auto& m = list[j];
            if (m.bytes.size() > 1 && m.bytes[1] == note && channelOf (m) == chan)
            {
                if (isNoteOff (m)) break;
                if (isNoteOn (m))
                {
                    list.insert (list.begin() + (std::ptrdiff_t) j, MidiEvent { m.tick, { (unsigned char) (0x80 | (chan - 1)), (unsigned char) note, 0 } });
                    break;
                }
            }
        }
    }
}

void putVarLen (std::vector<unsigned char>& out, std::uint32_t v)
{
    std::uint32_t buffer = v & 0x7f;
    while ((v >>= 7) != 0)
    {
        buffer <<= 8;
        buffer |= ((v & 0x7f) | 0x80);
    }
    for (;;)
    {
        out.push_back ((unsigned char) buffer);
        if (buffer & 0x80) buffer >>= 8;
        else break;
    }
}

std::vector<unsigned char> trackChunk (const std::vector<MidiEvent>& list)
{
    std::vector<unsigned char> data;
    int lastTick = 0;
    unsigned char lastStatus = 0;
    bool endWritten = false;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& b = list[i].bytes;
        if (b.size() >= 2 && b[0] == 0xff && b[1] == 0x2f) endWritten = true;
        const int tick = (int) std::nearbyint (list[i].tick);
        putVarLen (data, (std::uint32_t) jmax (0, tick - lastTick));
        lastTick = tick;
        size_t from = 0;
        const unsigned char status = b[0];
        if (status == lastStatus && (status & 0xf0) != 0xf0 && b.size() > 1 && i > 0) from = 1;   // running status
        data.insert (data.end(), b.begin() + (std::ptrdiff_t) from, b.end());
        lastStatus = status;
    }
    if (! endWritten) { data.push_back (0); data.insert (data.end(), { 0xff, 0x2f, 0x00 }); }
    std::vector<unsigned char> chunk { 'M', 'T', 'r', 'k' };
    const auto n = (std::uint32_t) data.size();
    chunk.insert (chunk.end(), { (unsigned char) (n >> 24), (unsigned char) (n >> 16), (unsigned char) (n >> 8), (unsigned char) n });
    chunk.insert (chunk.end(), data.begin(), data.end());
    return chunk;
}
} // namespace

bool writeMidi (const Song& song, const std::filesystem::path& file)
{
    constexpr int ppq = 960;
    std::vector<std::vector<MidiEvent>> tracks;
    {
        std::vector<MidiEvent> tempo;
        const int us = (int) std::llround (60000000.0 / song.bpm);
        addEvent (tempo, { 0.0, { 0xff, 81, 3, (unsigned char) (us >> 16), (unsigned char) (us >> 8), (unsigned char) us } });
        int n = 1, powerOfTwo = 0;
        while (n < 4) { n <<= 1; ++powerOfTwo; }
        addEvent (tempo, { 0.0, { 0xff, 0x58, 0x04, (unsigned char) song.beatsPerBar, (unsigned char) powerOfTwo, 1, 96 } });
        tracks.push_back (std::move (tempo));
    }
    int channel = 1;
    for (auto& part : song.parts)
    {
        const bool drums = part.instrument == Instrument::drums || part.instrument == Instrument::trapDrums;
        const int ch = drums ? 10 : channel;
        if (! drums) channel = channel == 9 ? 11 : jmin (16, channel + 1);
        std::vector<MidiEvent> seq;
        {
            const std::string name = instrumentName (part.instrument);
            std::vector<unsigned char> meta { 0xff, 3 };
            putVarLen (meta, (std::uint32_t) name.size());
            meta.insert (meta.end(), name.begin(), name.end());
            addEvent (seq, { 0.0, std::move (meta) });
        }
        for (auto& note : part.notes)
        {
            const double on = note.start * ppq, off = (note.start + note.length) * ppq;
            const auto vel = (unsigned char) jlimit (1, 127, (int) std::lround (note.velocity * 127.0f));
            addEvent (seq, { on, { (unsigned char) (0x90 | (ch - 1)), (unsigned char) (note.pitch & 127), vel } });
            addEvent (seq, { off, { (unsigned char) (0x80 | (ch - 1)), (unsigned char) (note.pitch & 127), 0 } });
        }
        matchPairs (seq);
        tracks.push_back (std::move (seq));
    }

    std::vector<unsigned char> bytes { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };
    bytes.push_back ((unsigned char) (tracks.size() >> 8));
    bytes.push_back ((unsigned char) tracks.size());
    bytes.push_back ((unsigned char) (ppq >> 8));
    bytes.push_back ((unsigned char) (ppq & 0xff));
    for (auto& t : tracks)
    {
        const auto chunk = trackChunk (t);
        bytes.insert (bytes.end(), chunk.begin(), chunk.end());
    }
    std::error_code ec;
    std::filesystem::remove (file, ec);
    std::ofstream out (file, std::ios::binary);
    out.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return (bool) out;
}
} // namespace hem::pg::band
