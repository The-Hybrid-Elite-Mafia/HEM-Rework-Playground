#include "Library.hpp"
#include "hem/pg/Files.hpp"
#include "hem/pg/Json.hpp"

#include <algorithm>
#include <cmath>
#include <variant>

namespace hem::pg::library
{
namespace
{
Json factsToJson (const AudioFacts& f)
{
    Json o = Json::object();
    o.set ("seconds", f.seconds);
    o.set ("bpm", f.bpm);
    o.set ("tempoConfidence", (double) f.tempoConfidence);
    o.set ("keyRoot", f.keyRoot);
    o.set ("minor", f.minor);
    o.set ("keyConfidence", (double) f.keyConfidence);
    o.set ("lufs", (double) f.lufs);
    o.set ("peakDb", (double) f.peakDb);
    return o;
}

AudioFacts factsFromJson (const Json& v)
{
    AudioFacts f;
    if (! v.isObject()) return f;
    f.seconds = v.has ("seconds") ? v["seconds"].toDouble() : 0.0;
    f.bpm = v.has ("bpm") ? v["bpm"].toDouble() : 0.0;
    f.tempoConfidence = (float) (v.has ("tempoConfidence") ? v["tempoConfidence"].toDouble() : 0.0);
    f.keyRoot = v.has ("keyRoot") ? v["keyRoot"].toInt() : -1;
    f.minor = v.has ("minor") && v["minor"].toBool();
    f.keyConfidence = (float) (v.has ("keyConfidence") ? v["keyConfidence"].toDouble() : 0.0);
    f.lufs = (float) (v.has ("lufs") ? v["lufs"].toDouble() : -100.0);
    f.peakDb = (float) (v.has ("peakDb") ? v["peakDb"].toDouble() : -100.0);
    return f;
}

std::string timeText (double s)
{
    const int t = roundToInt (s);
    char b[32];
    std::snprintf (b, sizeof b, "%d:%02d", t / 60, t % 60);
    return b;
}

std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

bool contains (const std::string& hay, const std::string& needle) { return hay.find (needle) != std::string::npos; }

const char* const kGenreWords[][2] = {
    { "outlaw", "Outlaw Country" }, { "country", "Outlaw Country" }, { "bluegrass", "Bluegrass" }, { "folk", "Folk" },
    { "gospel", "Gospel" }, { "blues", "Blues" }, { "pop trap", "Pop Trap" }, { "poptrap", "Pop Trap" },
    { "trap", "Trap" }, { "cinematic", "Cinematic" }, { "spoken", "Cinematic" },
};

/** Splits a search into words; "quoted words" stay together. */
std::vector<std::string> searchWords (const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : s)
    {
        if (c == '"') { quoted = ! quoted; continue; }
        if (c == ' ' && ! quoted) { out.push_back (cur); cur.clear(); continue; }
        cur += c;
    }
    out.push_back (cur);
    return out;
}
} // namespace

std::string Entry::summary() const
{
    std::string out = kind;
    if (facts.bpm > 0.0)
    {
        char b[32];
        std::snprintf (b, sizeof b, facts.bpm == std::floor (facts.bpm) ? "%.0f" : "%.1f", facts.bpm);
        out += std::string ("  -  ") + b + " BPM";
    }
    if (facts.keyRoot >= 0) out += "  -  " + facts.keyName();
    if (facts.seconds > 0.0) out += "  -  " + timeText (facts.seconds);
    return out;
}

Library::Library (std::filesystem::path f) : folder (std::move (f)) {}

std::filesystem::path Library::defaultFolder() { return files::playgroundDocuments() / "Library"; }

std::filesystem::path Library::fileOf (const Entry& e) const { return (folder / files::toPath (e.file)).lexically_normal(); }

const std::vector<std::string>& Library::kinds()
{
    static const std::vector<std::string> k { "Beat", "Song", "Vocal", "Stem", "Other" };
    return k;
}

bool Library::load (std::string& error)
{
    entries.clear();
    const auto index = folder / "library.json";
    std::error_code ec;
    if (! std::filesystem::is_regular_file (index, ec)) return true;
    const auto text = files::readText (index);
    const auto json = text ? Json::parse (*text) : std::nullopt;
    if (! json || ! json->isObject()) { error = "library.json is damaged (" + files::toText (index) + ")"; return false; }
    for (auto& v : (*json)["entries"].items())
    {
        Entry e;
        e.id = v["id"].toString();
        e.file = v["file"].toString();
        e.title = v["title"].toString();
        e.kind = v.has ("kind") ? v["kind"].toString() : "Beat";
        e.genre = v["genre"].toString();
        e.madeFrom = v["madeFrom"].toString();
        e.notes = v["notes"].toString();
        e.stars = jlimit (0, 5, v.has ("stars") ? v["stars"].toInt() : 0);
        e.added = v.has ("added") ? v["added"].toInt64() : 0;
        e.measured = v.has ("measured") && v["measured"].toBool();
        e.facts = factsFromJson (v["facts"]);
        if (! e.id.empty() && ! e.file.empty()) entries.push_back (e);
    }
    return true;
}

bool Library::save (std::string& error) const
{
    std::error_code ec;
    std::filesystem::create_directories (folder, ec);
    if (! std::filesystem::is_directory (folder, ec)) { error = "Can't create " + files::toText (folder); return false; }
    Json arr = Json::array();
    for (auto& e : entries)
    {
        Json o = Json::object();
        o.set ("id", e.id);
        o.set ("file", e.file);
        o.set ("title", e.title);
        o.set ("kind", e.kind);
        o.set ("genre", e.genre);
        o.set ("madeFrom", e.madeFrom);
        o.set ("notes", e.notes);
        o.set ("stars", e.stars);
        o.set ("added", e.added);
        o.set ("measured", e.measured);
        o.set ("facts", factsToJson (e.facts));
        arr.push (std::move (o));
    }
    Json root = Json::object();
    root.set ("app", "HEM Rework Playground");
    root.set ("version", 1);
    root.set ("entries", std::move (arr));
    const auto index = folder / "library.json";
    if (! files::writeTextAtomic (index, root.dump (true))) { error = "Can't write " + files::toText (index); return false; }
    return true;
}

std::string Library::addFile (const std::filesystem::path& source, std::string& error)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (source, ec)) { error = "Not found: " + files::toText (source); return {}; }
    const auto audioDir = folder / "Audio";
    std::filesystem::create_directories (audioDir, ec);
    if (! std::filesystem::is_directory (audioDir, ec)) { error = "Can't create " + files::toText (audioDir); return {}; }
    auto dest = files::nonexistentSibling (audioDir / files::toPath (files::legalFileName (files::toText (source.filename()))));
    std::filesystem::copy_file (source, dest, ec);
    if (ec) { error = "Can't copy " + files::toText (source.filename()) + " into the library"; return {}; }

    Entry e;
    e.id = files::newId();
    e.file = files::toText (dest.lexically_relative (folder));
    for (auto& c : e.file) if (c == '\\') c = '/';
    e.title = files::toText (source.stem());
    e.added = files::nowMillis();
    const auto low = lower (files::toText (source.stem()));
    for (auto& [word, genre] : kGenreWords)
        if (contains (low, word)) { e.genre = genre; break; }
    auto has = [&low] (std::initializer_list<const char*> words) { for (auto* w : words) if (contains (low, w)) return true; return false; };
    if (has ({ "vocal", "acapella", "a cappella" })) e.kind = "Vocal";
    else if (has ({ "instrumental", "beat" })) e.kind = "Beat";
    else if (has ({ "stem", "(drums)", "(bass)", "(guitar)", "(keys)", "(synth)", "(backing" })) e.kind = "Stem";
    else e.kind = "Song";
    entries.push_back (e);
    return e.id;
}

bool Library::remove (const std::string& id, std::string& error)
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].id == id)
        {
            const auto f = fileOf (entries[i]);
            std::error_code ec;
            if (std::filesystem::is_regular_file (f, ec))
            {
                const auto removed = folder / "Removed";
                std::filesystem::create_directories (removed, ec);
                const auto dest = files::nonexistentSibling (removed / f.filename());
                std::filesystem::rename (f, dest, ec);
                if (ec) { error = "Can't move " + files::toText (f.filename()) + " (is it playing or open somewhere?)"; return false; }
            }
            entries.erase (entries.begin() + (std::ptrdiff_t) i);
            return true;
        }
    error = "Not in the library";
    return false;
}

Entry* Library::find (const std::string& id)
{
    for (auto& e : entries) if (e.id == id) return &e;
    return nullptr;
}

const Entry* Library::find (const std::string& id) const
{
    for (auto& e : entries) if (e.id == id) return &e;
    return nullptr;
}

std::vector<std::string> Library::query (const std::string& search, const std::string& kind, int sortColumn, bool forwards) const
{
    const auto words = searchWords (lower (search));
    std::vector<const Entry*> hits;
    for (auto& e : entries)
    {
        if (! kind.empty() && e.kind != kind) continue;
        const std::string hay = lower (e.title + " " + e.kind + " " + e.genre + " " + e.notes + " " + e.facts.keyName() + " "
                                       + (e.facts.bpm > 0.0 ? std::to_string (roundToInt (e.facts.bpm)) : std::string()));
        bool ok = true;
        for (auto& w : words)
        {
            if (w.empty()) continue;
            if (w.size() >= 2 && w.find_first_not_of ("0123456789") == std::string::npos)
            {
                // a number is a tempo: within 3 BPM
                if (e.facts.bpm <= 0.0 || std::abs (e.facts.bpm - std::stod (w)) > 3.0) { ok = false; break; }
            }
            else if (! contains (hay, w)) { ok = false; break; }
        }
        if (ok) hits.push_back (&e);
    }
    using Key = std::variant<double, std::string>;
    auto key = [sortColumn] (const Entry& e) -> Key
    {
        switch (sortColumn)
        {
            case 0: return (double) e.stars;
            case 1: return lower (e.title);
            case 2: return e.kind;
            case 3: return lower (e.genre);
            case 4: return e.facts.bpm;
            case 5: return (double) (e.facts.keyRoot < 0 ? 99 : e.facts.keyRoot * 2 + (e.facts.minor ? 1 : 0));
            case 6: return e.facts.seconds;
            case 7: return (double) e.facts.lufs;
            default: return (double) e.added;
        }
    };
    std::stable_sort (hits.begin(), hits.end(), [&] (const Entry* a, const Entry* b)
    {
        const auto ka = key (*a), kb = key (*b);
        return forwards ? ka < kb : kb < ka;
    });
    std::vector<std::string> ids;
    for (auto* e : hits) ids.push_back (e->id);
    return ids;
}

std::vector<std::filesystem::path> Library::bandForgeTakes()
{
    const auto dir = files::playgroundDocuments() / "Band Forge";
    std::vector<std::filesystem::path> takes;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator (dir, ec); ! ec && it != std::filesystem::directory_iterator(); it.increment (ec))
        if (it->is_directory (ec)) takes.push_back (it->path());
    std::sort (takes.begin(), takes.end(), [] (const auto& a, const auto& b) { return files::toText (a.filename()) > files::toText (b.filename()); });
    return takes;
}
} // namespace hem::pg::library
