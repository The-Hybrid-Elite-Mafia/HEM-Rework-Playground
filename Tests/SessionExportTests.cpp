// Media decoding, .hemplay session files (incl. moved song folders) and offline WAV export.

#include "Engine/Exporter.h"
#include "Engine/MediaLoader.h"
#include "Session/SessionFile.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace pg::test
{
namespace
{
    juce::File tempDir (const juce::String& name)
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pg-test-" + name + "-" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)));
        d.createDirectory();
        return d;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& audio, double rate, int bits = 24)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (audio.getNumChannels()).withBitsPerSample (bits));
        return w != nullptr && w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    juce::AudioBuffer<float> readWav (const juce::File& f, double& rate)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        juce::AudioBuffer<float> b;
        if (r == nullptr) return b;
        rate = r->sampleRate;
        b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        return b;
    }

    juce::AudioBuffer<float> constantBuf (int channels, int n, float v)
    {
        juce::AudioBuffer<float> b (channels, n);
        for (int c = 0; c < channels; ++c) juce::FloatVectorOperations::fill (b.getWritePointer (c), v, n);
        return b;
    }
}

class SessionExportTests final : public juce::UnitTest
{
public:
    SessionExportTests() : juce::UnitTest ("Session + Export", "Playground") {}

    void runTest() override
    {
        auto dir = tempDir ("session");

        beginTest ("decode WAV (mono and stereo, native rate); clear errors for bad files");
        {
            auto f = dir.getChildFile ("stems/kick.wav");
            expect (writeWav (f, constantBuf (1, 44100, 0.25f), 44100.0));
            juce::String err;
            const double savedTarget = MediaLoader::getTargetRate();
            MediaLoader::setTargetRate (0.0);   // keep native rate
            auto s = MediaLoader::decode (f, err);
            expect (s != nullptr, err);
            if (s != nullptr)
            {
                expectEquals (s->getNumFrames(), 44100);
                expectEquals (s->getSourceRate(), 44100.0);
                expectWithinAbsoluteError (s->getBuffer().getSample (0, 1000), 0.25f, 1.0e-5f);
                expect (s->getFile() == f);
            }
            MediaLoader::setTargetRate (48000.0);   // engine at 48 kHz: the file is converted once, on load
            auto c = MediaLoader::decode (f, err);
            expect (c != nullptr, err);
            if (c != nullptr)
            {
                expectEquals (c->getNumFrames(), 48000);
                expectEquals (c->getSourceRate(), 48000.0);
                expectWithinAbsoluteError (c->getBuffer().getSample (0, 24000), 0.25f, 1.0e-4f);
            }
            MediaLoader::setTargetRate (savedTarget);
            auto junk = dir.getChildFile ("notes.wav");
            junk.replaceWithText ("not audio");
            expect (MediaLoader::decode (junk, err) == nullptr && err.isNotEmpty());
            expect (MediaLoader::decode (dir.getChildFile ("missing.wav"), err) == nullptr && err.contains ("not found"));
            expect (MediaLoader::isAudioFile (juce::File ("/x/a.WAV")) && ! MediaLoader::isAudioFile (juce::File ("/x/a.txt")));
            juce::StringArray failed;
            auto all = MediaLoader::decodeAll ({ f.getFullPathName(), f.getFullPathName(), junk.getFullPathName() }, failed);
            expectEquals ((int) all.size(), 1);
            expectEquals (failed.size(), 1);
        }

        beginTest ("session file: save, read back, and find the audio after the song folder moved");
        {
            auto song = dir.getChildFile ("My Song");
            auto kick = song.getChildFile ("stems/kick.wav");
            expect (writeWav (kick, constantBuf (2, 48000, 0.1f), 48000.0));
            juce::String err;
            PlaygroundEngine e;
            e.prepareToPlay (48000.0, 512);
            const int t = e.addTrack (MediaLoader::decode (kick, err), "Kick");
            e.setBusName (2, "Keys");
            auto sessionFile = song.getChildFile ("My Song.hemplay");
            expect (SessionFile::save (e, sessionFile, err), err);
            expect (sessionFile.existsAsFile());

            auto moved = dir.getChildFile ("Moved Song");
            expect (song.moveFileTo (moved));
            auto tree = SessionFile::read (moved.getChildFile ("My Song.hemplay"), err);
            expect (tree.isValid(), err);
            const auto files = PlaygroundEngine::mediaFilesIn (tree);
            expectEquals (files.size(), 1);
            expect (files[0] == moved.getChildFile ("stems/kick.wav").getFullPathName(), files[0]);

            juce::StringArray failed;
            auto media = MediaLoader::decodeAll (files, failed);
            PlaygroundEngine f;
            f.prepareToPlay (48000.0, 512);
            juce::StringArray missing;
            expect (f.applySessionTree (tree, media, &missing));
            expect (missing.isEmpty() && f.getTrackMedia (t) != nullptr);
            expectEquals (f.getBusName (2), juce::String ("Keys"));

            auto bad = dir.getChildFile ("bad.hemplay");
            bad.replaceWithText ("<NOPE/>");
            expect (! SessionFile::read (bad, err).isValid() && err.isNotEmpty());
        }

        beginTest ("export: exact length (song + tail), latency trimmed, ceiling respected, first sample intact");
        {
            PlaygroundEngine e;
            e.prepareToPlay (44100.0, 256);
            juce::AudioBuffer<float> src = constantBuf (2, 44100, 0.0f);
            for (int i = 0; i < src.getNumSamples(); ++i)
            {
                const float v = (i % 4410 == 0) ? 0.9f : 0.3f * (float) std::sin (0.05 * i);   // spikes every 100 ms
                src.setSample (0, i, v); src.setSample (1, i, v);
            }
            e.addTrack (new SampleData (std::move (src), 44100.0, juce::File()), "Beat");
            auto* m = e.insertPlugin (PlaygroundEngine::kMaster, 7, "elite-master");
            m->getAPVTS().getParameter ("input")->setValueNotifyingHost (m->getAPVTS().getParameter ("input")->convertTo0to1 (9.0f));
            const auto session = e.createSessionTree();

            ExportOptions o;
            o.file = dir.getChildFile ("export/mix.wav");
            o.sampleRate = 48000.0;
            o.bitDepth = 24;
            o.tailSeconds = 0.5;
            OfflineExport ex (session, {}, o);
            juce::String err;
            // the track's file is not in the media map: nothing to export
            expect (! ex.render ({}, {}, err) && err.isNotEmpty());

            // media map keyed by the (empty) path of the in-memory track: rebuild with a real file
            auto stem = dir.getChildFile ("export/beat.wav");
            {
                juce::AudioBuffer<float> again = e.getTrackMedia (0)->getBuffer();
                expect (writeWav (stem, again, 44100.0, 32));
            }
            PlaygroundEngine e2;
            e2.prepareToPlay (44100.0, 256);
            e2.addTrack (MediaLoader::decode (stem, err), "Beat");
            auto* m2 = e2.insertPlugin (PlaygroundEngine::kMaster, 7, "elite-master");
            m2->getAPVTS().getParameter ("input")->setValueNotifyingHost (m2->getAPVTS().getParameter ("input")->convertTo0to1 (9.0f));
            juce::StringArray failed;
            const auto session2 = e2.createSessionTree();
            auto media = MediaLoader::decodeAll (PlaygroundEngine::mediaFilesIn (session2), failed);
            OfflineExport ex2 (session2, media, o);
            double lastProgress = 0.0;
            expect (ex2.render ([&] (double p) { lastProgress = p; }, {}, err), err);
            expectWithinAbsoluteError (lastProgress, 1.0, 1.0e-9);

            double rate = 0.0;
            auto out = readWav (o.file, rate);
            expectEquals (rate, 48000.0);
            expectEquals (out.getNumSamples(), 48000 + 24000);
            expectLessThan (out.getMagnitude (0, out.getNumSamples()), params::dbToGain (-1.0f) + 1.0e-3f);
            expectLessThan (ex2.getPeak(), params::dbToGain (-1.0f) + 1.0e-3f);
            // look-ahead trimmed: the spike at t = 0 lands at the very start of the file
            int firstLoud = -1;
            for (int i = 0; i < 200; ++i) if (std::abs (out.getSample (0, i)) > 0.5f) { firstLoud = i; break; }
            expect (firstLoud >= 0 && firstLoud < 8, "first transient at " + juce::String (firstLoud));
            // tail after the song is (near) silent here (no reverb)
            expectLessThan (out.getMagnitude (0, 48000 + 2000, 20000), 1.0e-3f);

            beginTest ("export: 16-bit, BEFORE version, cancel leaves no file");
            ExportOptions o16 = o;
            o16.file = dir.getChildFile ("export/mix16.wav");
            o16.bitDepth = 16;
            o16.beforeMode = true;   // limiter bypassed: the +9 dB input is not applied
            OfflineExport ex3 (session2, media, o16);
            expect (ex3.render ({}, {}, err), err);
            auto out16 = readWav (o16.file, rate);
            expectEquals (out16.getNumSamples(), 72000);
            expectWithinAbsoluteError (out16.getMagnitude (0, out16.getNumSamples()), 0.9f, 0.02f);

            beginTest ("export: CLEAN skips the master inserts and keeps peaks at -3 dBFS (turned down, never up)");
            ExportOptions ocl = o;
            ocl.file = dir.getChildFile ("export/clean.wav");
            ocl.clean = true;           // Elite Master (+9 dB, limiter) on the master strip is skipped
            OfflineExport ex5 (session2, media, ocl);
            expect (ex5.render ({}, {}, err), err);
            auto clean = readWav (ocl.file, rate);
            expectEquals (clean.getNumSamples(), 72000);
            const float limit = params::dbToGain (ExportOptions::kCleanPeakDb);
            expectWithinAbsoluteError (clean.getMagnitude (0, clean.getNumSamples()), limit, 0.003f);
            expectWithinAbsoluteError (ex5.getCleanGain(), limit / 0.9f, 0.01f, "the 0.9 spikes set the gain");
            // same shape as the BEFORE file (no master processing), just scaled
            expectWithinAbsoluteError (clean.getRMSLevel (0, 1000, 3000) / juce::jmax (1.0e-6f, out16.getRMSLevel (0, 1000, 3000)), limit / 0.9f, 0.02f);
            {
                // a quiet mix is left alone
                PlaygroundEngine e3;
                e3.prepareToPlay (44100.0, 256);
                auto quietFile = dir.getChildFile ("export/quiet.wav");
                juce::AudioBuffer<float> q = constantBuf (2, 22050, 0.1f);
                expect (writeWav (quietFile, q, 44100.0, 32));
                e3.addTrack (MediaLoader::decode (quietFile, err), "Quiet");
                e3.insertPlugin (PlaygroundEngine::kMaster, 7, "elite-master");
                const auto session3 = e3.createSessionTree();
                auto media3 = MediaLoader::decodeAll (PlaygroundEngine::mediaFilesIn (session3), failed);
                ExportOptions oq = ocl;
                oq.file = dir.getChildFile ("export/quiet-clean.wav");
                OfflineExport ex6 (session3, media3, oq);
                expect (ex6.render ({}, {}, err), err);
                expectWithinAbsoluteError (ex6.getCleanGain(), 1.0f, 1.0e-6f);
                auto qOut = readWav (oq.file, rate);
                expectWithinAbsoluteError (qOut.getSample (0, 10000), 0.1f, 0.002f);
            }

            ExportOptions oc = o;
            oc.file = dir.getChildFile ("export/cancelled.wav");
            OfflineExport ex4 (session2, media, oc);
            int calls = 0;
            expect (! ex4.render ({}, [&] { return ++calls > 3; }, err));
            expect (! oc.file.existsAsFile());
        }

        dir.deleteRecursively();
    }
};

static SessionExportTests sessionExportTests;
} // namespace pg::test
