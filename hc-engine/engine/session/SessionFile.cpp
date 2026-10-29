#include "SessionFile.hpp"
#include "export/WavFile.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace hem::pg
{
namespace
{
    /** Local time as ISO 8601 with its offset ("2026-10-29T09:12:05.000-05:00"), like 0.10.0 saved it. */
    std::string nowIso8601()
    {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t (now);
        const auto ms = (int) (std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch()).count() % 1000);
        std::tm local {}, utc {};
#if defined(_WIN32)
        localtime_s (&local, &t);
        gmtime_s (&utc, &t);
#else
        localtime_r (&t, &local);
        gmtime_r (&t, &utc);
#endif
        utc.tm_isdst = local.tm_isdst;
        const long offsetMin = (long) std::difftime (std::mktime (&local), std::mktime (&utc)) / 60;
        char b[64];
        std::snprintf (b, sizeof b, "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02ld:%02ld",
                       local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, ms,
                       offsetMin < 0 ? '-' : '+', std::labs (offsetMin) / 60, std::labs (offsetMin) % 60);
        return b;
    }

    std::string relativeText (const std::filesystem::path& file, const std::filesystem::path& folder)
    {
        auto rel = std::filesystem::path (file).lexically_relative (folder);
        auto s = SessionFile::toText (rel.empty() ? file : rel);   // another drive: the full path, like 0.10.0
        for (auto& c : s) if (c == '\\') c = '/';
        return s;
    }
}

std::filesystem::path SessionFile::toPath (const std::string& text) { return std::filesystem::path (std::u8string (text.begin(), text.end())); }
std::string SessionFile::toText (const std::filesystem::path& p) { return pathText (p); }

bool SessionFile::save (const PlaygroundEngine& engine, const std::filesystem::path& fileIn, std::string& error)
{
    auto file = fileIn;
    file.replace_extension (kExtension);
    auto tree = engine.createSessionTree();
    tree.set ("app", "HEM Rework Playground");
    tree.set ("savedAt", nowIso8601());
    for (auto& c : tree.children())
        if (c.hasType ("TRACK") && ! c.get ("file").empty())
            c.set ("relFile", relativeText (toPath (c.get ("file")), file.parent_path()));

    std::error_code ec;
    if (! file.parent_path().empty())
    {
        std::filesystem::create_directories (file.parent_path(), ec);
        if (! std::filesystem::is_directory (file.parent_path(), ec)) { error = "Cannot create " + toText (file.parent_path()); return false; }
    }

    // write to a temp file first so a crash mid-save never destroys the old session
    TempFile temp (file);
    {
        std::ofstream out (temp.path(), std::ios::binary | std::ios::trunc);
        const auto xml = tree.toXml();
        out.write (xml.data(), (std::streamsize) xml.size());
        out.close();
        if (! out) { error = "Cannot write " + toText (file); return false; }
    }
    if (! temp.replaceTarget()) { error = "Cannot replace " + toText (file); return false; }
    return true;
}

std::optional<Tree> SessionFile::read (const std::filesystem::path& file, std::string& error)
{
    std::ifstream in (file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    auto tree = in ? Tree::fromXml (ss.str()) : std::nullopt;
    if (! tree) { error = "Not a readable session file: " + toText (file.filename()); return std::nullopt; }
    if (! tree->hasType (PlaygroundEngine::kSessionType)) { error = toText (file.filename()) + " is not a Rework Playground session."; return std::nullopt; }

    std::error_code ec;
    for (auto& c : tree->children())
        if (c.hasType ("TRACK"))
        {
            const auto full = toPath (c.get ("file"));
            if (! c.get ("file").empty() && std::filesystem::is_regular_file (full, ec)) continue;
            const auto rel = c.get ("relFile");
            if (! rel.empty())
            {
                const auto moved = (file.parent_path() / toPath (rel)).lexically_normal();
                if (std::filesystem::is_regular_file (moved, ec)) c.set ("file", toText (moved));
            }
        }
    return tree;
}
} // namespace hem::pg
