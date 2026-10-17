// Renders Band Forge demo songs to WAV (mastered mix) for listening tests.
// BandDemo <outdir> [full|short] [seed] [sound pack folder]
#include "Band/BandComposer.h"
#include "Band/BandRender.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace pg::band;

int main (int argc, char** argv)
{
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp/band-demo"));
    out.createDirectory();
    struct D { Genre g; int key; bool minor; const char* name; };
    const D demos[] { { Genre::outlawCountry, 7, false, "Outlaw Country" }, { Genre::bluegrass, 2, false, "Bluegrass" },
                      { Genre::blues, 4, false, "Blues" }, { Genre::gospel, 5, false, "Gospel" },
                      { Genre::folk, 9, true, "Folk" }, { Genre::cinematic, 2, true, "Cinematic" } };
    constexpr double sr = 48000.0;
    SoundPack pack;
    if (argc > 4)
    {
        juce::String err;
        if (! pack.open (juce::File (juce::String (argv[4])), err)) { std::cerr << err << "\n"; return 2; }
        std::cout << "Sound pack: " << pack.describe() << "\n";
    }
    for (auto& d : demos)
    {
        Request r; r.genre = d.g; r.keyRoot = d.key; r.minor = d.minor; r.fullSong = argc > 2 && juce::String (argv[2]) == "full" ? true : false; r.seed = argc > 3 ? (uint32_t) juce::String (argv[3]).getIntValue() : 7u;
        const Song song = compose (r);
        std::vector<Stem> stems;
        if (! renderSong (song, sr, stems, {}, nullptr, pack.isOpen() ? &pack : nullptr)) return 1;
        const auto f = out.getChildFile (juce::String (d.name) + " - " + song.describe().replace ("#", "sharp") + ".wav");
        juce::String info, err;
        if (! writeMix (stems, sr, MixFinish::mastered, f, info, err)) { std::cerr << err << "\n"; return 3; }
        const double secs = stems.empty() ? 0.0 : stems[0].audio.getNumSamples() / sr;
        int rec = 0;
        for (auto& st : stems) rec += st.recorded ? 1 : 0;
        std::cout << f.getFullPathName() << "  " << stems.size() << " parts (" << rec << " recorded), " << secs << " s, " << info << "\n";
    }
    return 0;
}
