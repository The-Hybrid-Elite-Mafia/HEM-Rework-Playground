// =============================================================================
//  legacy-render --sessions: the 0.10.0 mixer, session files, media loading and export (the JUCE
//  code in Source/), run on fixed test songs. Writes:
//    <dir>/stems/*          test stems in every format 0.10.0 opens (WAV, AIFF, FLAC, Ogg, MP3 on Windows)
//    <dir>/*.hemplay        sessions saved by 0.10.0
//    <dir>/old-*.wav|mp3    0.10.0 exports of those sessions
//    <dir>/sessions.txt     M lines (decoded media fingerprints) and E lines (export file fingerprints)
//  hem_pg_session_check opens the same sessions with the new engine and must match.
// =============================================================================
#include "Engine/Exporter.h"
#include "Engine/MediaLoader.h"
#include "Engine/Mp3Encoder.h"
#include "Session/SessionFile.h"
#include <cstdio>
#include <cstring>

using namespace pg;

namespace
{
constexpr double kFs = 48000.0;

unsigned long long fnvBytes (const void* data, size_t n, unsigned long long h = 1469598103934665603ull)
{
    auto* b = static_cast<const unsigned char*> (data);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

unsigned long long fnvFile (const juce::File& f)
{
    juce::MemoryBlock mb;
    f.loadFileAsData (mb);
    return fnvBytes (mb.getData(), mb.getSize());
}

/** A 6 s two-channel test stem with its own character (kick / bass / chord / noise mix). */
juce::AudioBuffer<float> stemAudio (int which, double rate, double seconds)
{
    const int n = (int) (seconds * rate);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (1000 + which);
    double ph1 = 0.0, ph2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / rate;
        const double f1 = 55.0 * (1 + which) * (1.0 + 0.01 * std::sin (2 * juce::MathConstants<double>::pi * 0.5 * t));
        ph1 += f1 / rate; ph1 -= std::floor (ph1);
        ph2 += f1 * 1.5 / rate; ph2 -= std::floor (ph2);
        const double tk = std::fmod (t + 0.1 * which, 0.5);
        const float hit = (float) (std::sin (2 * juce::MathConstants<double>::pi * 60 * tk) * std::exp (-tk * 12) * 0.4);
        const float tone = (float) (0.25 * std::sin (2 * juce::MathConstants<double>::pi * ph1) + 0.12 * (2.0 * ph2 - 1.0));
        const float noise = (rng.nextFloat() * 2 - 1) * 0.02f;
        b.setSample (0, i, hit + tone + noise);
        b.setSample (1, i, hit + 0.8f * tone - noise);
    }
    return b;
}

bool writeWith (juce::AudioFormat& fmt, const juce::File& f, const juce::AudioBuffer<float>& audio, double rate, int bits, int quality = 0, bool floatData = false)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
    using SF = juce::AudioFormatWriterOptions::SampleFormat;
    auto opts = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (audio.getNumChannels()).withBitsPerSample (bits)
                    .withQualityOptionIndex (quality).withSampleFormat (floatData ? SF::floatingPoint : SF::integral);
    auto w = fmt.createWriterFor (os, opts);
    return w != nullptr && w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void setParam (ElitePlugin& p, const juce::String& id, float v)
{
    if (auto* rp = p.getAPVTS().getParameter (id)) rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

void setMixer (PlaygroundEngine& e, const juce::String& id, float v)
{
    if (auto* rp = e.getAPVTS().getParameter (id)) rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

const FactoryPreset* presetNamed (const char* id, const char* name)
{
    for (const auto& p : Presets::factory (id)) if (p.name == name) return &p;
    return nullptr;
}

struct Stem { juce::String name; juce::File file; };
} // namespace

int runSessions (const juce::File& dir)
{
    dir.createDirectory();
    const auto stems = dir.getChildFile ("stems");
    stems.createDirectory();
    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;
    juce::OggVorbisAudioFormat ogg;

    std::vector<Stem> exact, lossy;
    auto add = [&] (std::vector<Stem>& list, const char* name, bool ok) { if (ok) list.push_back ({ name, stems.getChildFile (name) }); else std::printf ("could not write %s\n", name); };
    add (exact, "drums-48k-24.wav",   writeWith (wav,  stems.getChildFile ("drums-48k-24.wav"),   stemAudio (0, 48000.0, 6.0), 48000.0, 24));
    add (exact, "bass-44k-16.wav",    writeWith (wav,  stems.getChildFile ("bass-44k-16.wav"),    stemAudio (1, 44100.0, 6.0), 44100.0, 16));
    add (exact, "keys-48k-24.aiff",   writeWith (aiff, stems.getChildFile ("keys-48k-24.aiff"),   stemAudio (2, 48000.0, 5.5), 48000.0, 24));
    add (exact, "gtr-44k-24.flac",    writeWith (flac, stems.getChildFile ("gtr-44k-24.flac"),    stemAudio (3, 44100.0, 6.0), 44100.0, 24));
    add (exact, "vox-96k-32f.wav",    writeWith (wav,  stems.getChildFile ("vox-96k-32f.wav"),    stemAudio (4, 96000.0, 4.0), 96000.0, 32, 0, true));
    {
        // a mono stem
        auto m = stemAudio (5, 48000.0, 6.0);
        juce::AudioBuffer<float> mono (1, m.getNumSamples());
        mono.copyFrom (0, 0, m, 0, 0, m.getNumSamples());
        add (exact, "fx-48k-24-mono.wav", writeWith (wav, stems.getChildFile ("fx-48k-24-mono.wav"), mono, 48000.0, 24));
    }
    add (lossy, "pad-48k.ogg", writeWith (ogg, stems.getChildFile ("pad-48k.ogg"), stemAudio (6, 48000.0, 6.0), 48000.0, 16, 6));
   #if JUCE_WINDOWS
    {
        const auto src = stems.getChildFile ("lead-src.wav");
        writeWith (wav, src, stemAudio (7, 48000.0, 6.0), 48000.0, 16);
        juce::String err;
        add (lossy, "lead-48k-320.mp3", Mp3Encoder::encode (src, stems.getChildFile ("lead-48k-320.mp3"), 320, err));
        src.deleteFile();
    }
   #endif

    FILE* f = std::fopen (dir.getChildFile ("sessions.txt").getFullPathName().toRawUTF8(), "w");
    if (f == nullptr) return 1;

    //------------------------------------------------------------------ decoded media, the way the engine loads it (48 kHz)
    MediaLoader::setTargetRate (kFs);
    for (auto* list : { &exact, &lossy })
        for (auto& s : *list)
        {
            juce::String err;
            auto d = MediaLoader::decode (s.file, err);
            if (d == nullptr) { std::fprintf (f, "M %s|ERROR %s\n", s.name.toRawUTF8(), err.toRawUTF8()); continue; }
            unsigned long long h = 1469598103934665603ull;
            for (int c = 0; c < d->getNumChannels(); ++c) h = fnvBytes (d->getBuffer().getReadPointer (c), sizeof (float) * (size_t) d->getNumFrames(), h);
            std::fprintf (f, "M %s|%s|%d|%d|%.17g|%016llx\n", s.name.toRawUTF8(), list == &exact ? "exact" : "lossy",
                          d->getNumFrames(), d->getNumChannels(), d->getSourceRate(), h);
            // the decoded samples, for measuring how far the lossy formats are from the new decoders
            juce::FileOutputStream o (dir.getChildFile ("old-decoded-" + s.name + ".f32"));
            o.setPosition (0); o.truncate();
            for (int c = 0; c < d->getNumChannels(); ++c) o.write (d->getBuffer().getReadPointer (c), sizeof (float) * (size_t) d->getNumFrames());
        }

    //------------------------------------------------------------------ sessions
    auto build = [&] (const juce::String& name, const std::vector<Stem>& use, int variant) -> bool
    {
        PlaygroundEngine e;
        e.prepareToPlay (kFs, 512);
        e.newSession();
        for (size_t i = 0; i < use.size(); ++i)
        {
            juce::String err;
            auto m = MediaLoader::decode (use[i].file, err);
            const int t = e.addTrack (m, use[i].name.upToFirstOccurrenceOf (".", false, false));
            if (t < 0) return false;
            const float faders[] { 0.0f, -3.5f, -6.0f, 1.5f, -9.0f, -2.0f, -4.0f, -1.0f };
            const float pans[] { 0.0f, -0.4f, 0.35f, 0.0f, 0.7f, -0.2f, 0.1f, -0.6f };
            setMixer (e, PlaygroundEngine::Ids::track (t, "vol"), faders[i % 8]);
            setMixer (e, PlaygroundEngine::Ids::track (t, "pan"), pans[i % 8]);
            if (i % 2 == 1) setMixer (e, PlaygroundEngine::Ids::track (t, "sa"), -12.0f);   // to the Reverb bus
            if (i % 3 == 2) setMixer (e, PlaygroundEngine::Ids::track (t, "sb"), -16.0f);   // to the Delay bus
            if (i == 0) setMixer (e, PlaygroundEngine::Ids::track (t, "out"), 1.0f);         // drums to Drum Bus
        }
        if (variant == 0)
        {
            if (auto* p = e.insertPlugin (0, 0, "elite-eq"))   { if (auto* pr = presetNamed ("elite-eq", "Kick Punch")) Presets::apply (*p, *pr); else setParam (*p, "output", -1.0f); }
            if (auto* p = e.insertPlugin (1, 0, "elite-bass")) setParam (*p, "intensity", 40.0f);
            if (auto* p = e.insertPlugin (2, 0, "elite-comp")) { setParam (*p, "threshold", -18.0f); setParam (*p, "ratio", 3.0f); }
            if (auto* p = e.insertPlugin (4, 0, "elite-vox"))  { if (! Presets::factory ("elite-vox").empty()) Presets::apply (*p, Presets::factory ("elite-vox")[0]); }
            if (auto* p = e.insertPlugin (PlaygroundEngine::busStrip (0), 0, "elite-multiband")) setParam (*p, "mix", 60.0f);
            if (auto* p = e.getPlugin (PlaygroundEngine::kMaster, PlaygroundEngine::kSlots - 1)) setParam (*p, "input", 4.0f);
            setMixer (e, PlaygroundEngine::Ids::bpm, 96.0f);
        }
        else
        {
            if (auto* p = e.insertPlugin (0, 1, "elite-transient")) setParam (*p, "attack", 30.0f);
            if (auto* p = e.insertPlugin (1, 1, "elite-width")) setParam (*p, "width", 140.0f);
            if (auto* p = e.insertPlugin (PlaygroundEngine::kMaster, 0, "elite-console")) setParam (*p, "drive", 30.0f);
            setMixer (e, PlaygroundEngine::Ids::masterVol, -1.5f);
        }
        juce::String err;
        return SessionFile::save (e, dir.getChildFile (name + ".hemplay"), err);
    };

    struct Job { juce::String session; juce::String label; int bits; double rate; bool clean, before; int mp3; };
    std::vector<Job> jobs;
    if (build ("song-exact", exact, 0))
    {
        jobs.push_back ({ "song-exact", "24bit-48k", 24, 48000.0, false, false, 0 });
        jobs.push_back ({ "song-exact", "16bit-44k", 16, 44100.0, false, false, 0 });
        jobs.push_back ({ "song-exact", "32float-96k", 32, 96000.0, false, false, 0 });
        jobs.push_back ({ "song-exact", "clean-24bit", 24, 48000.0, true, false, 0 });
        jobs.push_back ({ "song-exact", "before-24bit", 24, 48000.0, false, true, 0 });
       #if JUCE_WINDOWS
        jobs.push_back ({ "song-exact", "mp3-320", 16, 48000.0, false, false, 320 });
       #endif
    }
    std::vector<Stem> all = exact;
    all.insert (all.end(), lossy.begin(), lossy.end());
    if (build ("song-all-formats", all, 1))
        jobs.push_back ({ "song-all-formats", "24bit-48k", 24, 48000.0, false, false, 0 });

    for (auto& j : jobs)
    {
        juce::String err;
        const auto sessionFile = dir.getChildFile (j.session + ".hemplay");
        auto tree = SessionFile::read (sessionFile, err);
        juce::StringArray failed;
        auto media = MediaLoader::decodeAll (PlaygroundEngine::mediaFilesIn (tree), failed);
        ExportOptions o;
        o.file = dir.getChildFile ("old-" + j.session + "-" + j.label + (j.mp3 > 0 ? ".mp3" : ".wav"));
        o.sampleRate = j.rate;
        o.bitDepth = j.bits;
        o.tailSeconds = 2.0;
        o.clean = j.clean;
        o.beforeMode = j.before;
        o.mp3Kbps = j.mp3;
        OfflineExport ex (tree, media, o);
        if (! ex.render ({}, {}, err)) { std::fprintf (f, "E %s|%s|ERROR %s\n", j.session.toRawUTF8(), j.label.toRawUTF8(), err.toRawUTF8()); continue; }
        std::fprintf (f, "E %s|%s|%d|%.17g|%d|%d|%d|%016llx|%lld\n", j.session.toRawUTF8(), j.label.toRawUTF8(), j.bits, j.rate,
                      j.clean ? 1 : 0, j.before ? 1 : 0, j.mp3, fnvFile (ex.getTargetFile()), (long long) ex.getTargetFile().getSize());
    }
    std::fclose (f);
    std::printf ("legacy-render --sessions: %d media, %d exports\n", (int) (exact.size() + lossy.size()), (int) jobs.size());
    return 0;
}
