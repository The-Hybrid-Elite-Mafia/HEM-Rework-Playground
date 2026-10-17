#include "Band/BandRender.h"
#include "Plugins/ElitePlugins.h"
#include "Plugins/PluginRegistry.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace pg::band
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
            juce::String error;
            if (pack->renderPart (song, part, sr, s.audio, leftover.notes, cancel, error))
            {
                s.recorded = done = true;
                if (! leftover.notes.empty() && ! renderPart (song, leftover, sr, s.audio, cancel)) return false;
            }
            else
            {
                if (cancel != nullptr && cancel->load()) return false;
                DBG ("Band Forge: sound pack failed for " << instrumentName (part.instrument) << ": " << error);
                s.audio.clear();   // fall back to the modeled instrument
            }
        }
        if (! done && ! renderPart (song, part, sr, s.audio, cancel)) return false;
        applySectionDynamics (song, s, sr);
        levelStem (s, sr);
        stems.push_back (std::move (s));
        if (progress) progress ((float) (i + 1) / (float) juce::jmax (1, n));
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
} // namespace

float sectionGainDb (const Song& song, Instrument inst, int index)
{
    if (! juce::isPositiveAndBelow (index, (int) song.sections.size())) return 0.0f;
    float top = 0.0f;
    for (auto& s : song.sections) top = juce::jmax (top, s.energy);
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
    float prevGain = juce::Decibels::decibelsToGain (gainAt (0.0));
    for (int st = 0; st < len; st += step)
    {
        const int m = juce::jmin (step, len - st);
        const float next = juce::Decibels::decibelsToGain (gainAt ((st + m) / sr));
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
    const int block = juce::jmax (1, (int) (0.05 * sr));
    std::vector<double> blockMs;
    double loudest = 0.0;
    for (int st = 0; st < len; st += block)
    {
        const int m = juce::jmin (block, len - st);
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch) { const auto* d = a.getReadPointer (ch, st); for (int k = 0; k < m; ++k) sum += (double) d[k] * d[k]; }
        const double ms = sum / (2.0 * m);
        blockMs.push_back (ms);
        loudest = juce::jmax (loudest, ms);
    }
    if (loudest <= 1.0e-14) return;
    double sum = 0.0; int count = 0;
    for (double ms : blockMs) if (ms > loudest * 1.0e-5) { sum += ms; ++count; }
    const double rms = std::sqrt (sum / juce::jmax (1, count));
    float gain = (float) (std::pow (10.0, stemTargetRmsDb (s.instrument) / 20.0) / juce::jmax (1.0e-9, rms));
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

juce::Array<juce::File> writeStems (const Song& song, const std::vector<Stem>& stems, double sr, const juce::File& folder, juce::String& error)
{
    juce::Array<juce::File> files;
    if (! folder.createDirectory()) { error = "Can't create " + folder.getFullPathName(); return files; }
    juce::WavAudioFormat wav;
    for (size_t i = 0; i < stems.size(); ++i)
    {
        const auto name = juce::String (i + 1).paddedLeft ('0', 2) + " " + instrumentName (stems[i].instrument) + ".wav";
        const auto file = folder.getChildFile (name);
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
        if (os == nullptr) { error = "Can't write " + file.getFullPathName(); return {}; }
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (writer == nullptr) { error = "Can't create a WAV writer"; return {}; }
        os.release();
        if (! writer->writeFromAudioSampleBuffer (stems[i].audio, 0, stems[i].audio.getNumSamples())) { error = "Writing " + name + " failed"; return {}; }
        files.add (file);
    }
    writeMidi (song, folder.getChildFile ("Band Forge.mid"));
    return files;
}

namespace
{
bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& b, int start, int count, double sr, juce::String& error)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> os (file.createOutputStream().release());
    if (os == nullptr) { error = "Can't write " + file.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr) { error = "Can't create a WAV writer"; return false; }
    if (! writer->writeFromAudioSampleBuffer (b, start, count)) { error = "Writing " + file.getFileName() + " failed"; return false; }
    return true;
}
} // namespace

bool writeMix (const std::vector<Stem>& stems, double sr, MixFinish finish, const juce::File& file, juce::String& info, juce::String& error)
{
    if (finish == MixFinish::none || stems.empty()) return true;
    const int len = stems[0].audio.getNumSamples();
    juce::AudioBuffer<float> mix (2, len);
    mix.clear();
    for (auto& s : stems)
        for (int ch = 0; ch < 2; ++ch) mix.addFrom (ch, 0, s.audio, ch, 0, juce::jmin (len, s.audio.getNumSamples()));

    if (finish == MixFinish::clean)
    {
        const float peak = mix.getMagnitude (0, len), limit = juce::Decibels::decibelsToGain (-3.0f);
        if (peak > limit) mix.applyGain (limit / peak);
        info = "clean, peak " + juce::String (juce::Decibels::gainToDecibels (juce::jmin (peak, limit)), 1) + " dBFS";
        return writeWav24 (file, mix, 0, len, sr, error);
    }

    // MASTERED: the sum is first set to -20 LUFS (Elite Master's INPUT only adds gain), then Elite
    // Master's input is matched to -14 LUFS in a few passes (the limiter makes it non-linear)
    {
        dsp::LoudnessMeter lm;
        lm.prepare (sr);
        lm.process (mix.getReadPointer (0), mix.getReadPointer (1), len);
        if (lm.getIntegrated() > -70.0f) mix.applyGain (juce::Decibels::decibelsToGain (-20.0f - lm.getIntegrated()));
    }
    auto plugin = PluginRegistry::create ("elite-master");
    auto* master = dynamic_cast<EliteMaster*> (plugin.get());
    if (master == nullptr) { error = "Elite Master is missing"; return false; }
    auto* input = master->getAPVTS().getParameter ("input");
    constexpr int block = 1024;
    juce::AudioBuffer<float> out;
    juce::MidiBuffer midi;
    int latency = 0;
    float integrated = -100.0f;
    for (int pass = 0; pass < 4; ++pass)
    {
        master->prepareToPlay (sr, block);
        master->resetMeters();
        latency = master->getLatencySamples();
        out.setSize (2, len + latency, false, false, true);
        out.clear();
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, mix, ch, 0, len);
        for (int st = 0; st < out.getNumSamples(); st += block)
        {
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, st, juce::jmin (block, out.getNumSamples() - st));
            master->processBlock (view, midi);
        }
        integrated = master->getReadout (EliteMaster::integrated);
        if (integrated <= -70.0f) break;
        const float now = input->convertFrom0to1 (input->getValue());
        const float next = juce::jlimit (0.0f, 18.0f, now + (EliteMaster::targetLufs (0) - integrated));
        if (std::abs (next - now) < 0.1f || pass == 3) break;
        input->setValueNotifyingHost (input->convertTo0to1 (next));
    }
    const float peak = out.getMagnitude (latency, len);
    info = "mastered, " + juce::String (integrated, 1) + " LUFS, peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS";
    return writeWav24 (file, out, latency, len, sr, error);
}

bool writeMidi (const Song& song, const juce::File& file)
{
    constexpr int ppq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / song.bpm)), 0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (song.beatsPerBar, 4), 0);
    mf.addTrack (tempo);
    int channel = 1;
    for (auto& part : song.parts)
    {
        const bool drums = part.instrument == Instrument::drums || part.instrument == Instrument::trapDrums;
        const int ch = drums ? 10 : channel;
        if (! drums) channel = channel == 9 ? 11 : juce::jmin (16, channel + 1);
        juce::MidiMessageSequence seq;
        auto nameMsg = juce::MidiMessage::textMetaEvent (3, instrumentName (part.instrument));
        seq.addEvent (nameMsg, 0);
        for (auto& n : part.notes)
        {
            const double on = n.start * ppq, off = (n.start + n.length) * ppq;
            const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::lround (n.velocity * 127.0f));
            seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, vel), on);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), off);
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && mf.writeTo (os);
}
} // namespace pg::band
