#pragma once

#include "BandInstruments.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hem::pg::band
{
/**
    The HEM Sound Pack: recorded instruments for Band Forge (offline only, never the audio thread).

    The pack is a folder with pack.json (built by Tools/build_sound_pack.py) and FLAC samples.
    Instruments the pack has are played from the recordings; every other instrument, and any drum
    note the pack doesn't cover (clap, brush, tambourine, shaker), stays on the modeled sound.

    Samples are decoded only for the instrument being rendered and released afterwards, so memory
    stays at one instrument (largest about 170 MB) however big the song is.
*/
class SoundPack
{
public:
    /** Reads pack.json (no audio is decoded yet). False + error if the folder isn't a valid pack. */
    bool open (const std::filesystem::path& folder, std::string& error);
    bool isOpen() const noexcept { return ! instruments.empty(); }
    bool has (Instrument) const;
    std::filesystem::path getFolder() const { return folder; }
    /** "piano, fiddle, strings, upright bass, drums" */
    std::string describe() const;

    /** Renders a part from the recordings into `out` (stereo, added in place). Drum notes the pack
        lacks are appended to `notCovered` for the modeled kit. Returns false if cancelled or the
        instrument's samples could not be decoded (then `error` says why). */
    bool renderPart (const Song&, const Part&, double sampleRate, AudioBuffer& out,
                     std::vector<Note>& notCovered, const std::atomic<bool>* cancel, std::string& error) const;

    /** Folders searched, in order: <app folder>/Sounds/HEM Sound Pack, then
        Documents/Hybrid Elite Mafia/Rework Playground/Sounds/HEM Sound Pack. */
    static std::vector<std::filesystem::path> searchFolders();
    /** Opens the first valid pack found; empty pack if none. */
    static std::shared_ptr<const SoundPack> findInstalled();

    struct Zone
    {
        std::string file;
        int root = 60, lo = 0, hi = 127;      // pitched: key range; drums: lo = hi = root = GM note
        float velLo = 0.0f, velHi = 1.0f;
        int rr = 1;
        float tuneCents = 0.0f;               // measured correction that puts the recording in tune
        int loopStart = -1, loopEnd = -1;     // sample frames at the pack's rate; -1 = no loop
    };
    struct InstrumentDef
    {
        bool drums = false;
        float release = 0.15f;                // seconds of fade after the note ends
        std::vector<Zone> zones;
        std::vector<Zone> shortZones;         // fiddle: spiccato for short notes
    };

private:
    std::filesystem::path folder;
    double packRate = 48000.0;
    std::map<Instrument, InstrumentDef> instruments;
};
} // namespace hem::pg::band
