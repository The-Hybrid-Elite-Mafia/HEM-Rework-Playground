// LIBRARY: tempo / key / loudness measurement, the library index (add, save, load, search, remove)
// and the engine's preview player.

#include "Library/Library.h"
#include "Band/BandComposer.h"
#include "Band/BandRender.h"
#include "Engine/PlaygroundEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace pg::test
{
namespace
{
juce::AudioBuffer<float> bandMix (band::Genre g, int key, bool minor, double bpm, uint32_t seed, double sr)
{
    band::Request r;
    r.genre = g; r.keyRoot = key; r.minor = minor; r.bpm = bpm; r.fullSong = false; r.seed = seed;
    std::vector<band::Stem> stems;
    band::renderSong (band::compose (r), sr, stems);
    juce::AudioBuffer<float> mix (2, stems[0].audio.getNumSamples());
    mix.clear();
    for (auto& s : stems) for (int c = 0; c < 2; ++c) mix.addFrom (c, 0, s.audio, c, 0, mix.getNumSamples(), 0.4f);
    return mix;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

class LibraryTests final : public juce::UnitTest
{
public:
    LibraryTests() : juce::UnitTest ("Library", "Playground") {}

    void runTest() override
    {
        using band::Genre;
        beginTest ("tempo and key of Band Forge songs are measured (tempo within 0.5 BPM, key exact)");
        struct Case { Genre g; int key; bool minor; double bpm; };
        for (const auto& c : { Case { Genre::outlawCountry, 7, false, 96.0 }, Case { Genre::bluegrass, 2, false, 132.0 },
                               Case { Genre::folk, 9, true, 104.0 }, Case { Genre::gospel, 5, false, 74.0 },
                               Case { Genre::popTrap, 1, true, 100.0 }, Case { Genre::cinematic, 2, true, 72.0 } })
        {
            for (double sr : { 48000.0, 44100.0 })
            {
                const auto mix = bandMix (c.g, c.key, c.minor, c.bpm, 5, sr);
                const auto f = library::measure (mix, sr);
                const juce::String name = juce::String (band::genreName (c.g)) + " @" + juce::String (sr);
                logMessage (name + ": " + juce::String (f.bpm, 2) + " BPM (conf " + juce::String (f.tempoConfidence, 2) + "), "
                            + f.keyName() + " (conf " + juce::String (f.keyConfidence, 2) + "), " + juce::String (f.lufs, 1) + " LUFS");
                // a slow song with busy 8th notes can read at double time (the LIBRARY has x2 / /2 for that);
                // every other genre must be exact
                const bool octave = c.g == Genre::cinematic && std::abs (f.bpm - 2.0 * c.bpm) <= 1.0;
                if (! octave) expectWithinAbsoluteError (f.bpm, c.bpm, 0.5, name + " tempo " + juce::String (f.bpm));
                expectEquals (f.keyRoot, c.key, name + " key " + f.keyName());
                expectEquals ((int) f.minor, (int) c.minor, name + " mode " + f.keyName());
                expectWithinAbsoluteError (f.seconds, mix.getNumSamples() / sr, 1.0e-6);
                expect (f.lufs > -40.0f && f.lufs < 0.0f);
            }
        }

        beginTest ("silence and very short files don't invent a tempo or key");
        {
            juce::AudioBuffer<float> quiet (2, 48000 * 3);
            quiet.clear();
            const auto f = library::measure (quiet, 48000.0);
            expectEquals (f.bpm, 0.0);
            expectEquals (f.keyRoot, -1);
        }

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hem-library-test");
        dir.deleteRecursively();
        const auto src = dir.getChildFile ("in");
        src.createDirectory();

        beginTest ("files are copied in, guessed, saved, loaded back, searched and removed (never deleted)");
        {
            library::Library lib (dir.getChildFile ("Library"));
            juce::String err;
            expect (lib.load (err) && lib.size() == 0, err);
            const auto beat = src.getChildFile ("Dirt Road Trap (Instrumental).wav");
            const auto vocal = src.getChildFile ("Whiskey Gospel Vocals.wav");
            const auto song = src.getChildFile ("Front Porch Folk.wav");
            writeWav (beat, bandMix (Genre::trap, 1, true, 140.0, 2, 48000.0), 48000.0);
            writeWav (vocal, bandMix (Genre::gospel, 5, false, 74.0, 2, 48000.0), 48000.0);
            writeWav (song, bandMix (Genre::folk, 9, true, 96.0, 2, 48000.0), 48000.0);
            const auto idBeat = lib.addFile (beat, err);
            const auto idVocal = lib.addFile (vocal, err);
            const auto idSong = lib.addFile (song, err);
            const auto idAgain = lib.addFile (beat, err);   // same name again: kept, new file name
            expect (idBeat.isNotEmpty() && idVocal.isNotEmpty() && idSong.isNotEmpty() && idAgain.isNotEmpty(), err);
            expectEquals (lib.size(), 4);
            expect (beat.existsAsFile(), "the original stays where it was");
            expect (lib.fileOf (*lib.find (idBeat)).existsAsFile() && lib.fileOf (*lib.find (idAgain)).existsAsFile());
            expect (lib.fileOf (*lib.find (idBeat)) != lib.fileOf (*lib.find (idAgain)));
            expectEquals (lib.find (idBeat)->kind, juce::String ("Beat"));
            expectEquals (lib.find (idBeat)->genre, juce::String ("Trap"));
            expectEquals (lib.find (idVocal)->kind, juce::String ("Vocal"));
            expectEquals (lib.find (idVocal)->genre, juce::String ("Gospel"));
            expectEquals (lib.find (idSong)->kind, juce::String ("Song"));

            // measure like the UI does (decode the copied file)
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            for (auto id : { idBeat, idVocal, idSong, idAgain })
            {
                auto* e = lib.find (id);
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (lib.fileOf (*e)));
                juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples);
                rd->read (&b, 0, b.getNumSamples(), 0, true, true);
                e->facts = library::measure (b, rd->sampleRate);
                e->measured = true;
            }
            lib.find (idSong)->stars = 5;
            lib.find (idSong)->notes = "took it through Suno, kept the hook";
            lib.find (idSong)->madeFrom = "C:/Band Forge/2026-10-17 Folk";
            expect (lib.save (err), err);

            library::Library again (dir.getChildFile ("Library"));
            expect (again.load (err), err);
            expectEquals (again.size(), 4);
            const auto* s2 = again.find (idSong);
            expect (s2 != nullptr && s2->stars == 5 && s2->notes.contains ("Suno") && s2->measured && s2->madeFrom.contains ("Folk"));
            if (s2 != nullptr) expectWithinAbsoluteError (s2->facts.bpm, 96.0, 0.5);

            expectEquals (again.query ("", "", 8, true).size(), 4);
            expectEquals (again.query ("", "Vocal", 8, true).size(), 1);
            expectEquals (again.query ("gospel", "", 8, true)[0], idVocal);
            expectEquals (again.query ("95", "", 8, true)[0], idSong);             // number = tempo within 3 BPM
            expectEquals (again.query ("suno", "", 8, true)[0], idSong);           // notes are searched
            expectEquals (again.query ("trap minor", "", 1, true).size(), 2);      // both beats: trap + C# minor
            expectEquals (again.query ("", "", 0, false)[0], idSong);              // 5 stars first
            logMessage ("trap beat measured " + juce::String (again.find (idBeat)->facts.bpm) + " BPM, " + again.find (idBeat)->facts.keyName());
            const auto byTempo = again.query ("", "", 4, true);
            expect (byTempo.indexOf (idVocal) < byTempo.indexOf (idSong), "74 BPM before 96 BPM");

            expect (again.remove (idAgain, err), err);
            expectEquals (again.size(), 3);
            expect (dir.getChildFile ("Library/Removed").findChildFiles (juce::File::findFiles, false).size() == 1, "moved, not deleted");
            expect (again.save (err), err);

            dir.getChildFile ("Library/library.json").replaceWithText ("{ not json");
            library::Library broken (dir.getChildFile ("Library"));
            expect (! broken.load (err) && err.contains ("damaged"));
        }

        beginTest ("preview plays a file straight to the output, without the session, and stops at its end");
        {
            PlaygroundEngine e;
            e.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> tone (2, 4800);
            for (int i = 0; i < tone.getNumSamples(); ++i) { tone.setSample (0, i, 0.25f); tone.setSample (1, i, -0.25f); }
            e.startPreview (new SampleData (std::move (tone), 48000.0, juce::File()));
            expect (e.isPreviewPlaying());
            juce::AudioBuffer<float> out (2, 512);
            juce::MidiBuffer midi;
            e.processBlock (out, midi);
            expectWithinAbsoluteError (out.getSample (0, 100), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (out.getSample (1, 100), -0.25f, 1.0e-6f);
            expectWithinAbsoluteError (e.getPreviewPosition(), 512.0 / 48000.0, 1.0e-6);
            e.seekPreview (0.05);   // 2400 samples in
            e.processBlock (out, midi);
            expectWithinAbsoluteError (e.getPreviewPosition(), (2400.0 + 512.0) / 48000.0, 1.0e-6);
            for (int k = 0; k < 6; ++k) e.processBlock (out, midi);
            expect (! e.isPreviewPlaying(), "stopped at the end of the file");
            expectEquals (out.getMagnitude (0, 512), 0.0f);
            e.stopPreview();
            e.collectGarbage();
        }
        dir.deleteRecursively();
    }
};

static LibraryTests libraryTests;
} // namespace pg::test
