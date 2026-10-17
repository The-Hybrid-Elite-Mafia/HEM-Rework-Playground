#include "Library/Library.h"

namespace pg::library
{
namespace
{
juce::var factsToVar (const AudioFacts& f)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("seconds", f.seconds);
    o->setProperty ("bpm", f.bpm);
    o->setProperty ("tempoConfidence", f.tempoConfidence);
    o->setProperty ("keyRoot", f.keyRoot);
    o->setProperty ("minor", f.minor);
    o->setProperty ("keyConfidence", f.keyConfidence);
    o->setProperty ("lufs", f.lufs);
    o->setProperty ("peakDb", f.peakDb);
    return juce::var (o);
}

AudioFacts factsFromVar (const juce::var& v)
{
    AudioFacts f;
    if (! v.isObject()) return f;
    f.seconds = (double) v.getProperty ("seconds", 0.0);
    f.bpm = (double) v.getProperty ("bpm", 0.0);
    f.tempoConfidence = (float) (double) v.getProperty ("tempoConfidence", 0.0);
    f.keyRoot = (int) v.getProperty ("keyRoot", -1);
    f.minor = (bool) v.getProperty ("minor", false);
    f.keyConfidence = (float) (double) v.getProperty ("keyConfidence", 0.0);
    f.lufs = (float) (double) v.getProperty ("lufs", -100.0);
    f.peakDb = (float) (double) v.getProperty ("peakDb", -100.0);
    return f;
}

juce::String timeText (double s)
{
    const int t = juce::roundToInt (s);
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

const char* const kGenreWords[][2] = {
    { "outlaw", "Outlaw Country" }, { "country", "Outlaw Country" }, { "bluegrass", "Bluegrass" }, { "folk", "Folk" },
    { "gospel", "Gospel" }, { "blues", "Blues" }, { "pop trap", "Pop Trap" }, { "poptrap", "Pop Trap" },
    { "trap", "Trap" }, { "cinematic", "Cinematic" }, { "spoken", "Cinematic" },
};
} // namespace

juce::String Entry::summary() const
{
    juce::StringArray bits { kind };
    if (facts.bpm > 0.0) bits.add (juce::String (facts.bpm, facts.bpm == std::floor (facts.bpm) ? 0 : 1) + " BPM");
    if (facts.keyRoot >= 0) bits.add (facts.keyName());
    if (facts.seconds > 0.0) bits.add (timeText (facts.seconds));
    return bits.joinIntoString ("  -  ");
}

Library::Library (juce::File f) : folder (std::move (f)) {}

juce::File Library::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hybrid Elite Mafia")
        .getChildFile ("Rework Playground").getChildFile ("Library");
}

const juce::StringArray& Library::kinds()
{
    static const juce::StringArray k { "Beat", "Song", "Vocal", "Stem", "Other" };
    return k;
}

bool Library::load (juce::String& error)
{
    entries.clear();
    const auto index = folder.getChildFile ("library.json");
    if (! index.existsAsFile()) return true;
    const auto json = juce::JSON::parse (index);
    if (! json.isObject()) { error = "library.json is damaged (" + index.getFullPathName() + ")"; return false; }
    if (auto* arr = json["entries"].getArray())
        for (auto& v : *arr)
        {
            Entry e;
            e.id = v["id"].toString();
            e.file = v["file"].toString();
            e.title = v["title"].toString();
            e.kind = v.getProperty ("kind", "Beat").toString();
            e.genre = v["genre"].toString();
            e.madeFrom = v["madeFrom"].toString();
            e.notes = v["notes"].toString();
            e.stars = juce::jlimit (0, 5, (int) v.getProperty ("stars", 0));
            e.added = (juce::int64) v.getProperty ("added", 0);
            e.measured = (bool) v.getProperty ("measured", false);
            e.facts = factsFromVar (v["facts"]);
            if (e.id.isNotEmpty() && e.file.isNotEmpty()) entries.add (e);
        }
    return true;
}

bool Library::save (juce::String& error) const
{
    if (! folder.createDirectory()) { error = "Can't create " + folder.getFullPathName(); return false; }
    juce::Array<juce::var> arr;
    for (auto& e : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", e.id);
        o->setProperty ("file", e.file);
        o->setProperty ("title", e.title);
        o->setProperty ("kind", e.kind);
        o->setProperty ("genre", e.genre);
        o->setProperty ("madeFrom", e.madeFrom);
        o->setProperty ("notes", e.notes);
        o->setProperty ("stars", e.stars);
        o->setProperty ("added", e.added);
        o->setProperty ("measured", e.measured);
        o->setProperty ("facts", factsToVar (e.facts));
        arr.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("app", "HEM Rework Playground");
    root->setProperty ("version", 1);
    root->setProperty ("entries", arr);
    const auto index = folder.getChildFile ("library.json");
    juce::TemporaryFile temp (index);
    if (! temp.getFile().replaceWithText (juce::JSON::toString (juce::var (root)))) { error = "Can't write " + index.getFullPathName(); return false; }
    if (! temp.overwriteTargetFileWithTemporary()) { error = "Can't replace " + index.getFullPathName(); return false; }
    return true;
}

juce::String Library::addFile (const juce::File& source, juce::String& error)
{
    if (! source.existsAsFile()) { error = "Not found: " + source.getFullPathName(); return {}; }
    const auto audioDir = folder.getChildFile ("Audio");
    if (! audioDir.createDirectory()) { error = "Can't create " + audioDir.getFullPathName(); return {}; }
    auto dest = audioDir.getChildFile (juce::File::createLegalFileName (source.getFileName()));
    if (dest.exists()) dest = dest.getNonexistentSibling (false);
    if (! source.copyFileTo (dest)) { error = "Can't copy " + source.getFileName() + " into the library"; return {}; }

    Entry e;
    e.id = juce::Uuid().toDashedString();
    e.file = dest.getRelativePathFrom (folder).replaceCharacter ('\\', '/');
    e.title = source.getFileNameWithoutExtension();
    e.added = juce::Time::currentTimeMillis();
    const auto lower = source.getFileNameWithoutExtension().toLowerCase();
    for (auto& [word, genre] : kGenreWords)
        if (lower.contains (word)) { e.genre = genre; break; }
    auto has = [&lower] (std::initializer_list<const char*> words) { for (auto* w : words) if (lower.contains (w)) return true; return false; };
    if (has ({ "vocal", "acapella", "a cappella" })) e.kind = "Vocal";
    else if (has ({ "instrumental", "beat" })) e.kind = "Beat";
    else if (has ({ "stem", "(drums)", "(bass)", "(guitar)", "(keys)", "(synth)", "(backing" })) e.kind = "Stem";
    else e.kind = "Song";
    entries.add (e);
    return e.id;
}

bool Library::remove (const juce::String& id, juce::String& error)
{
    for (int i = 0; i < entries.size(); ++i)
        if (entries.getReference (i).id == id)
        {
            const auto f = fileOf (entries.getReference (i));
            if (f.existsAsFile())
            {
                const auto removed = folder.getChildFile ("Removed");
                removed.createDirectory();
                auto dest = removed.getChildFile (f.getFileName());
                if (dest.exists()) dest = dest.getNonexistentSibling (false);
                if (! f.moveFileTo (dest)) { error = "Can't move " + f.getFileName() + " (is it playing or open somewhere?)"; return false; }
            }
            entries.remove (i);
            return true;
        }
    error = "Not in the library";
    return false;
}

Entry* Library::find (const juce::String& id)
{
    for (auto& e : entries) if (e.id == id) return &e;
    return nullptr;
}

const Entry* Library::find (const juce::String& id) const
{
    for (auto& e : entries) if (e.id == id) return &e;
    return nullptr;
}

juce::StringArray Library::query (const juce::String& search, const juce::String& kind, int sortColumn, bool forwards) const
{
    const auto words = juce::StringArray::fromTokens (search.toLowerCase(), " ", "\"");
    std::vector<const Entry*> hits;
    for (auto& e : entries)
    {
        if (kind.isNotEmpty() && e.kind != kind) continue;
        const juce::String hay = (e.title + " " + e.kind + " " + e.genre + " " + e.notes + " " + e.facts.keyName() + " "
                                  + (e.facts.bpm > 0.0 ? juce::String (juce::roundToInt (e.facts.bpm)) : juce::String())).toLowerCase();
        bool ok = true;
        for (auto& w : words)
        {
            if (w.isEmpty()) continue;
            if (w.containsOnly ("0123456789") && w.length() >= 2)
            {
                // a number is a tempo: within 3 BPM
                if (e.facts.bpm <= 0.0 || std::abs (e.facts.bpm - w.getDoubleValue()) > 3.0) { ok = false; break; }
            }
            else if (! hay.contains (w)) { ok = false; break; }
        }
        if (ok) hits.push_back (&e);
    }
    auto key = [sortColumn] (const Entry& e) -> juce::var
    {
        switch (sortColumn)
        {
            case 0: return e.stars;
            case 1: return e.title.toLowerCase();
            case 2: return e.kind;
            case 3: return e.genre.toLowerCase();
            case 4: return e.facts.bpm;
            case 5: return e.facts.keyRoot < 0 ? 99 : e.facts.keyRoot * 2 + (e.facts.minor ? 1 : 0);
            case 6: return e.facts.seconds;
            case 7: return (double) e.facts.lufs;
            default: return (double) e.added;
        }
    };
    std::stable_sort (hits.begin(), hits.end(), [&] (const Entry* a, const Entry* b)
    {
        const auto ka = key (*a), kb = key (*b);
        const bool less = ka.isString() ? ka.toString() < kb.toString() : (double) ka < (double) kb;
        const bool more = ka.isString() ? kb.toString() < ka.toString() : (double) kb < (double) ka;
        return forwards ? less : more;
    });
    juce::StringArray ids;
    for (auto* e : hits) ids.add (e->id);
    return ids;
}

juce::Array<juce::File> Library::bandForgeTakes()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Hybrid Elite Mafia")
                         .getChildFile ("Rework Playground").getChildFile ("Band Forge");
    auto takes = dir.findChildFiles (juce::File::findDirectories, false);
    std::sort (takes.begin(), takes.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
    return takes;
}
} // namespace pg::library
