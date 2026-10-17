// Renders the main window (every tab) with a demo song to PNG, headless.
// Usage: ScreenPreview <output folder>

#include "UI/MainComponent.h"
#include "UI/Editors/EliteEditor.h"
#include "Session/SessionFile.h"
#include "Engine/MediaLoader.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace pg;

namespace
{
    juce::File writeStem (const juce::File& dir, const juce::String& name, std::function<float (int, double)> gen, double seconds = 40.0)
    {
        const double fs = 48000.0;
        const int n = (int) (seconds * fs);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { const float v = gen (i, fs); b.setSample (0, i, v); b.setSample (1, i, v); }
        auto f = dir.getChildFile (name + ".wav");
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (new juce::FileOutputStream (f));
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (b, 0, n);
        return f;
    }

    void shot (juce::Component& c, const juce::File& f)
    {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("%s\n", f.getFullPathName().toRawUTF8());
    }

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp/screens"));
    out.createDirectory();
    auto stems = out.getChildFile ("demo-stems");
    stems.createDirectory();
    const double bpm = 140.0, beat = 60.0 / bpm;
    auto kick  = writeStem (stems, "Kick",  [=] (int i, double fs) { const double t = std::fmod (i / fs, beat); return (float) (0.9 * std::exp (-t * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 90.0 * std::exp (-t * 40.0)) * t)); });
    auto snare = writeStem (stems, "Snare", [=] (int i, double fs) { const double t = std::fmod (i / fs + beat, 2.0 * beat); juce::Random r (i); return (float) (0.5 * std::exp (-t * 25.0) * (r.nextFloat() * 2.0 - 1.0)); });
    auto hats  = writeStem (stems, "Hats",  [=] (int i, double fs) { const double t = std::fmod (i / fs, beat * 0.5); juce::Random r (i * 7); return (float) (0.2 * std::exp (-t * 60.0) * (r.nextFloat() * 2.0 - 1.0)); });
    auto bass  = writeStem (stems, "808",   [=] (int i, double fs) { const double t = std::fmod (i / fs, beat * 4.0); return (float) (0.6 * std::exp (-t * 1.2) * std::sin (2.0 * juce::MathConstants<double>::pi * 49.0 * t)); });
    auto vox   = writeStem (stems, "Lead Vox", [=] (int i, double fs) { const double t = i / fs; return (float) (0.35 * (0.5 + 0.5 * std::sin (t * 1.7)) * std::sin (2.0 * juce::MathConstants<double>::pi * (220.0 + 30.0 * std::sin (t * 3.0)) * t) * (std::fmod (t, 4.0) < 3.2 ? 1.0 : 0.0)); });
    auto keys  = writeStem (stems, "Keys",  [=] (int i, double fs) { const double t = i / fs; return (float) (0.15 * (std::sin (2.0 * juce::MathConstants<double>::pi * 261.6 * t) + std::sin (2.0 * juce::MathConstants<double>::pi * 311.1 * t) + std::sin (2.0 * juce::MathConstants<double>::pi * 392.0 * t))); });

    ui::EliteEditor::install();
    PlaygroundEngine engine;
    engine.prepareToPlay (48000.0, 512);
    engine.newSession();
    juce::String err;
    for (auto& f : { kick, snare, hats, bass, vox, keys })
        engine.addTrack (MediaLoader::decode (f, err), f.getFileNameWithoutExtension());
    auto set = [&] (const juce::String& id, float v) { auto* p = engine.getAPVTS().getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (PlaygroundEngine::Ids::bpm, (float) bpm);
    for (int t = 0; t < 3; ++t) set (PlaygroundEngine::Ids::track (t, "out"), 1.0f);   // drums -> Drum Bus
    set (PlaygroundEngine::Ids::track (4, "sa"), -8.0f);
    set (PlaygroundEngine::Ids::track (4, "sb"), -14.0f);
    set (PlaygroundEngine::Ids::track (5, "vol"), -6.0f);
    set (PlaygroundEngine::Ids::track (3, "pan"), -0.2f);
    engine.insertPlugin (0, 0, "elite-vox");
    engine.insertPlugin (0, 1, "elite-eq");
    engine.insertPlugin (4, 0, "elite-channel");
    engine.insertPlugin (4, 1, "elite-vox");
    engine.insertPlugin (3, 0, "elite-bass");
    engine.insertPlugin (PlaygroundEngine::busStrip (0), 0, "elite-comp");
    auto sessionFile = out.getChildFile ("Demo Song.hemplay");
    SessionFile::save (engine, sessionFile, err);

    juce::PropertiesFile::Options o;
    o.applicationName = "ScreenPreview";
    o.folderName = "pg-preview";
    o.filenameSuffix = ".settings";
    juce::PropertiesFile settings (out.getChildFile ("preview.settings"), o);
    juce::AudioDeviceManager devices;
    settings.setValue ("bandShort", true);

    {
        ui::MainComponent mc (engine, devices, settings);
        mc.setSize (1600, 900);
        mc.openSession (sessionFile);
        pump (1500);
        // play 20 s through the mix so meters, loudness and play-head show real values
        engine.setLoop (true, 8 * 4 * beat, 16 * 4 * beat);
        engine.setPositionSeconds (8 * 4 * beat + 3.0);
        engine.play();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 48000 * 6 / 512; ++i) engine.processBlock (buf, midi);
        pump (400);
        mc.showTab (ui::tabs::edit);   pump (300); shot (mc, out.getChildFile ("main-edit.png"));
        mc.showTab (ui::tabs::mix);    pump (300); shot (mc, out.getChildFile ("main-mix.png"));
        mc.showTab (ui::tabs::forge);  pump (300); shot (mc, out.getChildFile ("main-forge.png"));
        mc.showTab (ui::tabs::master); pump (300); shot (mc, out.getChildFile ("main-master.png"));
        mc.setSize (1389, 810);   // Steven's laptop, maximised
        mc.showTab (ui::tabs::mix);    pump (300); shot (mc, out.getChildFile ("main-mix-laptop.png"));
        mc.setSize (1920, 1080);
        mc.showTab (ui::tabs::mix);    pump (300); shot (mc, out.getChildFile ("main-mix-1080p.png"));

        // SONG FORGE in ACE-Step mode
        mc.setSize (1600, 900);
        mc.showTab (ui::tabs::forge);
        mc.getForgePage()->setBandMode (false); pump (300); shot (mc, out.getChildFile ("main-forge-ace.png"));
        mc.getForgePage()->setBandMode (true);  pump (300);

        // BAND FORGE end to end: forge a short band, wait, it lands in the session as tracks
        engine.stop();
        const int before = engine.getNumUsedTracks();
        auto& band = mc.getForgePage()->getBandPanel();
        band.forge();
        pump (500); shot (mc, out.getChildFile ("main-forge-working.png"));
        for (int i = 0; i < 600 && band.isBusy(); ++i) pump (100);
        pump (3000);
        shot (mc, out.getChildFile ("main-forge-done.png"));
        std::printf ("BAND FORGE: busy=%d tracks before=%d after=%d\n", (int) band.isBusy(), before, engine.getNumUsedTracks());
        for (int t = 0; t < engine.getNumUsedTracks(); ++t) std::printf ("  track %d: %s\n", t + 1, engine.getTrackName (t).toRawUTF8());
        mc.showTab (ui::tabs::edit); pump (500); shot (mc, out.getChildFile ("main-edit-band.png"));

        // LIBRARY: the band take goes in (TO LIBRARY), gets measured, previews
        mc.showTab (ui::tabs::forge);
        auto& lib = mc.getForgePage()->getLibraryPanel();
        juce::Array<juce::File> stems;
        for (auto& f : juce::File (ui::AppContext::songsFolder().getChildFile ("Band Forge")).findChildFiles (juce::File::findFiles, true, "*.wav")) stems.add (f);
        lib.addFiles (stems);
        mc.getForgePage()->setMode (ui::ForgePage::Mode::library);
        for (int i = 0; i < 600 && lib.isBusy(); ++i) pump (100);
        pump (500);
        shot (mc, out.getChildFile ("main-library.png"));
        std::printf ("LIBRARY: %d entries\n", lib.getLibrary().size());
        for (auto& e : lib.getLibrary().all()) std::printf ("  %s: %s\n", e.title.toRawUTF8(), e.summary().toRawUTF8());
    }
    return 0;
}
