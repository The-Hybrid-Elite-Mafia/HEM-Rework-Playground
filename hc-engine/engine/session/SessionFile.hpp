#pragma once
// =============================================================================
//  .hemplay session files (XML, the same layout 0.10.0 wrote, so old songs open).
//  Track files are stored with their full path and also relative to the session's folder, so a
//  song folder moved to another drive or PC still opens. Message thread for save; read() any thread.
// =============================================================================
#include "PlaygroundEngine.hpp"

namespace hem::pg
{
struct SessionFile
{
    static constexpr const char* kExtension = ".hemplay";

    static bool save (const PlaygroundEngine&, const std::filesystem::path& file, std::string& error);
    /** Reads and checks a session. Each TRACK's "file" is rewritten to where the audio actually is
        (full path, else relative to the session). Empty + a reason on failure. */
    static std::optional<Tree> read (const std::filesystem::path& file, std::string& error);

    /** Text <-> path, the way sessions store paths (UTF-8). */
    static std::filesystem::path toPath (const std::string& text);
    static std::string toText (const std::filesystem::path&);
};
} // namespace hem::pg
