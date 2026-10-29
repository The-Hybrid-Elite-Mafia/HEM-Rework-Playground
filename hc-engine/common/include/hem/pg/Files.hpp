#pragma once
// =============================================================================
//  Files — small file and folder helpers for the app (message / worker threads only).
// =============================================================================
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace hem::pg::files
{
namespace fs = std::filesystem;

/** The user's Documents folder (Windows: the real one, even if moved to another drive). */
fs::path documentsFolder();
/** The folder the running .exe is in. */
fs::path executableFolder();
/** Documents/Hybrid Elite Mafia/Rework Playground — where songs, Band Forge takes and the library live. */
fs::path playgroundDocuments();

/** UTF-8 text <-> path. */
fs::path toPath (const std::string& utf8);
std::string toText (const fs::path&);

std::optional<std::string> readText (const fs::path&);
/** Writes through a temp file next to it, then swaps it in: a crash never leaves half a file. */
bool writeTextAtomic (const fs::path&, const std::string& text);

/** A name made safe for a file: the characters "#@,;:<>*^|?\/ removed, at most 128 long (keeps the extension). */
std::string legalFileName (const std::string& name);
/** `file` if it does not exist, else name2.ext, name3.ext ... (name_2.ext if the name ends in a digit). */
fs::path nonexistentSibling (const fs::path& file);
/** A new random id, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (lower-case hex). */
std::string newId();
/** Milliseconds since 1 Jan 1970. */
std::int64_t nowMillis();
} // namespace hem::pg::files
