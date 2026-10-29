#pragma once

#include "AudioFacts.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace hem::pg::library
{
/** One song, beat, vocal or stem in the LIBRARY. */
struct Entry
{
    std::string id;                // stable, never reused
    std::string file;              // path inside the library folder ("Audio/My Beat.wav")
    std::string title;
    std::string kind = "Beat";     // one of kinds()
    std::string genre;             // free text, usually a Band Forge genre
    std::string madeFrom;          // Band Forge take folder this came from (absolute path), or empty
    std::string notes;
    int stars = 0;                 // 0..5
    std::int64_t added = 0;        // ms since 1970
    AudioFacts facts;
    bool measured = false;

    /** "Beat  -  96 BPM  -  G major  -  3:12" */
    std::string summary() const;
};

/**
    The private LIBRARY: Steven's own beats and songs (Band Forge parts taken through Suno and back,
    and anything else he makes). Files are copied into
    Documents/Hybrid Elite Mafia/Rework Playground/Library/Audio and listed in library.json there.
    Nothing is uploaded anywhere. Message thread only (measuring runs on a background thread in the UI).
*/
class Library
{
public:
    explicit Library (std::filesystem::path folder = defaultFolder());

    static std::filesystem::path defaultFolder();
    static const std::vector<std::string>& kinds();   // Beat, Song, Vocal, Stem, Other

    /** Reads library.json (missing file = empty library). */
    bool load (std::string& error);
    /** Writes library.json (through a temp file, so a crash can't leave half a file). */
    bool save (std::string& error) const;

    /** Copies a file in (a new name if one with that name is already there) and adds an entry.
        Returns the new entry's id, or empty + error. Guesses kind/genre/title from the file name. */
    std::string addFile (const std::filesystem::path& source, std::string& error);
    /** Takes the entry out of the list and moves its file to Library/Removed (never deleted). */
    bool remove (const std::string& id, std::string& error);

    int size() const noexcept { return (int) entries.size(); }
    Entry* find (const std::string& id);
    const Entry* find (const std::string& id) const;
    const std::vector<Entry>& all() const noexcept { return entries; }
    std::filesystem::path fileOf (const Entry& e) const;
    std::filesystem::path getFolder() const { return folder; }

    /** Ids matching a search ("trap 140", "minor", a word in the title / genre / notes) and a kind
        ("" = all), sorted by `sortColumn` (0 stars, 1 title, 2 kind, 3 genre, 4 bpm, 5 key, 6 length,
        7 loudness, 8 added). */
    std::vector<std::string> query (const std::string& search, const std::string& kind, int sortColumn, bool forwards) const;

    /** Band Forge take folders, newest first (for the "made from" choice). */
    static std::vector<std::filesystem::path> bandForgeTakes();

private:
    std::filesystem::path folder;
    std::vector<Entry> entries;
};
} // namespace hem::pg::library
