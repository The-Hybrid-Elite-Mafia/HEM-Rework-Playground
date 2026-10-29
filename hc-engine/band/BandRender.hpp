#pragma once

#include "BandSampler.hpp"

#include <functional>

namespace hem::pg::band
{
struct Stem
{
    Instrument instrument = Instrument::acousticGuitar;
    AudioBuffer audio;                // stereo
    bool recorded = false;            // played from the sound pack (false = modeled)
};

/** Renders every part of the song into its own stereo stem and levels it (stemTargetRmsDb,
    peaks kept under -1 dBFS). progress(0..1) is called from the rendering thread.
    With a sound pack, the instruments it has are played from its recordings (drum notes it lacks,
    and any instrument whose samples fail to load, fall back to the modeled sound).
    Returns false if cancelled. */
bool renderSong (const Song&, double sampleRate, std::vector<Stem>& stems,
                 const std::function<void (float)>& progress = {}, const std::atomic<bool>* cancel = nullptr,
                 const SoundPack* pack = nullptr);

/** Verse-to-hook lift: each section is turned down by how far its energy sits under the song's
    biggest section (pads and rhythm instruments most, drums and bass less), the last two beats
    before a bigger section swell half-way up, and the drop to a quieter section is eased over half a
    beat. The level of the song's biggest section is unchanged. */
float sectionGainDb (const Song&, Instrument, int sectionIndex);
void applySectionDynamics (const Song&, Stem&, double sampleRate);

/** Levels one stem in place: RMS of its playing parts to the instrument's target, peak <= -1 dBFS. */
void levelStem (Stem&, double sampleRate);

/** Writes "01 Drums.wav" ... (24-bit) and "Band Forge.mid" into `folder`. Returns the WAV files in order. */
std::vector<std::filesystem::path> writeStems (const Song&, const std::vector<Stem>&, double sampleRate,
                                               const std::filesystem::path& folder, std::string& error);

/** How the optional full-mix file is finished. */
enum class MixFinish { none, clean, mastered };

/** Sums the stems into one stereo mix and writes it as a 24-bit WAV.
    clean: nothing added, turned down only if its peaks go over -3 dBFS.
    mastered: through Elite Master (the MASTER page's mastering plug-in) with its input matched so the
    mix lands on -14 LUFS integrated (streaming), true peaks held under -1 dBTP.
    `info` gets a short report ("-14.1 LUFS, peak -1.0 dBFS"). Returns false + error on failure. */
bool writeMix (const std::vector<Stem>&, double sampleRate, MixFinish, const std::filesystem::path&, std::string& info, std::string& error);

/** All parts as a type-1 MIDI file (one track per instrument, tempo map, drums on channel 10). */
bool writeMidi (const Song&, const std::filesystem::path&);
} // namespace hem::pg::band
