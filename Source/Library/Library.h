#pragma once

#include "Library/AudioFacts.h"
#include <juce_core/juce_core.h>

namespace pg::library
{
/** One song, beat, vocal or stem in the LIBRARY. */
struct Entry
{
    juce::String id;               // stable, never reused
    juce::String file;             // path inside the library folder ("Audio/My Beat.wav")
    juce::String title;
    juce::String kind = "Beat";    // one of kinds()
    juce::String genre;            // free text, usually a Band Forge genre
    juce::String madeFrom;         // Band Forge take folder this came from (absolute path), or empty
    juce::String notes;
    int stars = 0;                 // 0..5
    juce::int64 added = 0;         // ms since 1970
    AudioFacts facts;
    bool measured = false;

    /** "Beat - 96 BPM - G major - 3:12" */
    juce::String summary() const;
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
    explicit Library (juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    static const juce::StringArray& kinds();   // Beat, Song, Vocal, Stem, Other

    /** Reads library.json (missing file = empty library). */
    bool load (juce::String& error);
    /** Writes library.json (through a temp file, so a crash can't leave half a file). */
    bool save (juce::String& error) const;

    /** Copies a file in (a new name if one with that name is already there) and adds an entry.
        Returns the new entry's id, or empty + error. Guesses kind/genre/title from the file name. */
    juce::String addFile (const juce::File& source, juce::String& error);
    /** Takes the entry out of the list and moves its file to Library/Removed (never deleted). */
    bool remove (const juce::String& id, juce::String& error);

    int size() const noexcept { return entries.size(); }
    Entry* find (const juce::String& id);
    const Entry* find (const juce::String& id) const;
    const juce::Array<Entry>& all() const noexcept { return entries; }
    juce::File fileOf (const Entry& e) const { return folder.getChildFile (e.file); }
    juce::File getFolder() const { return folder; }

    /** Ids matching a search ("trap 140", "minor", a word in the title / genre / notes) and a kind
        ("" = all), sorted by `sortColumn` (0 stars, 1 title, 2 kind, 3 genre, 4 bpm, 5 key, 6 length,
        7 loudness, 8 added). */
    juce::StringArray query (const juce::String& search, const juce::String& kind, int sortColumn, bool forwards) const;

    /** Band Forge take folders, newest first (for the "made from" choice). */
    static juce::Array<juce::File> bandForgeTakes();

private:
    juce::File folder;
    juce::Array<Entry> entries;
};
} // namespace pg::library
