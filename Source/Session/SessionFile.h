#pragma once

#include "Engine/PlaygroundEngine.h"

namespace pg
{
/**
    .hemplay session files (XML). Track files are stored with their full path and also relative to
    the session's folder, so a song folder moved to another drive or PC still opens.
    Message thread for save; read() is safe on any thread.
*/
struct SessionFile
{
    static constexpr const char* kExtension = ".hemplay";

    static bool save (const PlaygroundEngine&, const juce::File& file, juce::String& error);
    /** Reads and validates a session. Each TRACK's "file" is rewritten to where the audio actually is
        (full path, else relative to the session). Invalid tree + error on failure. */
    static juce::ValueTree read (const juce::File& file, juce::String& error);
};
} // namespace pg
