#include "LibraryPanel.hpp"
#include "../platform/Platform.hpp"

#include "BandTypes.hpp"
#include "hem/pg/Files.hpp"
#include "media/MediaLoader.hpp"

#include <cctype>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <ctime>

namespace hem::app
{
namespace
{
    const Rgba kLib = argb (0xff4fb3d9);   // library blue next to Band Forge's copper
    enum Col { cStars = 1, cTitle, cKind, cGenre, cBpm, cKey, cLength, cLufs, cAdded };
    struct Column { const char* name; int id; float width; };
    constexpr Column kColumns[] { { "RATING", cStars, 70 }, { "TITLE", cTitle, 300 }, { "KIND", cKind, 70 }, { "GENRE", cGenre, 120 },
                                  { "BPM", cBpm, 64 }, { "KEY", cKey, 90 }, { "LENGTH", cLength, 66 }, { "LUFS", cLufs, 60 }, { "ADDED", cAdded, 110 } };
    constexpr float kRowH = 30.0f, kHeadH = 26.0f;

    std::string timeText (double s)
    {
        const int t = (int) std::lround (s);
        char b[16];
        std::snprintf (b, sizeof b, "%d:%02d", t / 60, t % 60);
        return b;
    }

    std::string starText (int n) { return n <= 0 ? std::string ("-") : std::to_string (n) + (n == 1 ? " STAR" : " STARS"); }

    std::string dateText (std::int64_t ms)
    {
        const std::time_t t = (std::time_t) (ms / 1000);
        std::tm tm {};
#if defined(_WIN32)
        localtime_s (&tm, &t);
#else
        localtime_r (&t, &tm);
#endif
        char b[16];
        std::strftime (b, sizeof b, "%Y-%m-%d", &tm);
        return b;
    }

    bool isAudio (const fs::path& f)
    {
        std::string e = pg::files::toText (f.extension());
        for (auto& c : e) c = (char) std::tolower ((unsigned char) c);
        return e == ".wav" || e == ".mp3" || e == ".flac" || e == ".aif" || e == ".aiff" || e == ".ogg" || e == ".m4a";
    }

    std::string upper (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; }

    std::string num (double v, int decimals)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.*f", decimals, v);
        return b;
    }
}

//==============================================================================
LibraryPanel::LibraryPanel (App& a) : app (a)
{
    worker = std::make_unique<Worker> (1);
    std::string err;
    if (! lib.load (err)) app.status ("LIBRARY: " + err);
    refresh();
    // anything added but never measured (the app closed while measuring) gets measured now
    for (auto& e : lib.all()) if (! e.measured) measure (e.id);
}

LibraryPanel::~LibraryPanel()
{
    alive->store (false);
    stopping->store (true);
    worker->stop();
    if (dirty) { std::string err; lib.save (err); }
    if (! previewId.empty()) app.engine.stopPreview();
}

int LibraryPanel::addFiles (const std::vector<fs::path>& files, const std::string& genreHint, const fs::path& from, const std::string& kindHint,
                            const std::string& titleHint)
{
    int added = 0;
    std::string lastId, err;
    for (auto& f : files)
    {
        if (! isAudio (f)) continue;
        const auto id = lib.addFile (f, err);
        if (id.empty()) { app.status ("LIBRARY: " + err); continue; }
        if (auto* e = lib.find (id))
        {
            if (! genreHint.empty()) e->genre = genreHint;
            if (! from.empty()) e->madeFrom = pg::files::toText (from);
            if (! kindHint.empty()) e->kind = kindHint;
            if (! titleHint.empty() && files.size() == 1) e->title = titleHint;
        }
        measure (id);
        lastId = id;
        ++added;
    }
    if (added > 0)
    {
        if (! lib.save (err)) app.status ("LIBRARY: " + err);
        if (! kindFilter.empty() && lib.find (lastId) != nullptr && lib.find (lastId)->kind != kindFilter) kindFilter.clear();
        refresh();
        select (lastId);
        app.status ("LIBRARY: added " + std::to_string (added) + (added == 1 ? " file" : " files") + ", measuring tempo and key...");
    }
    return added;
}

void LibraryPanel::measure (const std::string& id)
{
    const auto* e = lib.find (id);
    if (e == nullptr) return;
    const fs::path file = lib.fileOf (*e);
    ++pendingMeasures;
    auto alivePtr = alive;
    auto stopPtr = stopping;
    worker->add ([this, alivePtr, stopPtr, id, file]
    {
        if (! alivePtr->load()) return;
        std::string err;
        pg::library::AudioFacts facts;
        bool ok = false;
        if (auto data = pg::MediaLoader::decode (file, err))
        {
            pg::AudioBuffer b (2, data->getNumFrames());
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, data->channel (c), data->getNumFrames());
            facts = pg::library::measure (b, data->getSourceRate(), stopPtr.get());
            ok = alivePtr->load();
        }
        Worker::toUi ([this, alivePtr, id, facts, ok, err]
        {
            if (! alivePtr->load()) return;
            --pendingMeasures;
            if (auto* entry = lib.find (id))
            {
                if (ok) { entry->facts = facts; entry->measured = true; }
                else app.status ("LIBRARY: can't read " + entry->title + (err.empty() ? std::string() : " (" + err + ")"));
                dirty = true;
                refresh();
            }
        });
    });
}

void LibraryPanel::refresh()
{
    const auto keep = currentId();
    rows = lib.query (search, kindFilter, sortColumn, sortForwards);
    selectedRow = -1;
    for (int i = 0; i < (int) rows.size(); ++i) if (rows[(std::size_t) i] == keep) selectedRow = i;
    if (selectedRow < 0 && ! rows.empty()) selectedRow = 0;
}

void LibraryPanel::select (const std::string& id)
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(std::size_t) i] == id)
        {
            selectedRow = i;
            const float y = (float) i * kRowH;
            if (y < scroll) scroll = y;
        }
}

std::string LibraryPanel::currentId() const { return selectedRow >= 0 && selectedRow < (int) rows.size() ? rows[(std::size_t) selectedRow] : std::string(); }
pg::library::Entry* LibraryPanel::current() { return lib.find (currentId()); }

void LibraryPanel::togglePreview()
{
    const auto id = currentId();
    auto* e = current();
    if (e == nullptr) return;
    if (previewId == id && app.engine.getPreviewMedia() != nullptr)
    {
        if (app.engine.isPreviewPlaying()) app.engine.stopPreview();
        else app.engine.resumePreview();
        return;
    }
    decodingPreview = id;
    const fs::path file = lib.fileOf (*e);
    auto alivePtr = alive;
    worker->add ([this, alivePtr, id, file]
    {
        if (! alivePtr->load()) return;
        std::string err;
        auto data = pg::MediaLoader::decode (file, err);
        Worker::toUi ([this, alivePtr, id, data, err]
        {
            if (! alivePtr->load() || decodingPreview != id) return;
            decodingPreview.clear();
            if (data == nullptr) { app.status ("LIBRARY: can't play it (" + err + ")"); return; }
            app.engine.startPreview (data);
            previewId = id;
        });
    });
}

void LibraryPanel::addToSession()
{
    if (auto* e = current())
    {
        const fs::path f = lib.fileOf (*e);
        std::error_code ec;
        if (! fs::exists (f, ec)) { app.status ("LIBRARY: the file is gone (" + pg::files::toText (f) + ")"); return; }
        app.importFiles ({ f });
    }
}

void LibraryPanel::removeSelected()
{
    const auto id = currentId();
    if (id.empty()) return;
    if (previewId == id) { app.engine.stopPreview(); previewId.clear(); }
    std::string err;
    const std::string name = lib.find (id)->title;
    if (! lib.remove (id, err)) { app.status ("LIBRARY: " + err); return; }
    lib.save (err);
    refresh();
    app.status ("LIBRARY: took out " + name + " (the file is in Library/Removed)");
}

//==============================================================================
void LibraryPanel::drawKinds (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    p.panelText ("LIBRARY  (private)", reduced (withHeight (panel, 40.0f), 10.0f, 0.0f), 15.0f);
    std::vector<std::string> names { "EVERYTHING" }, filters { "" };
    for (auto& k : pg::library::Library::kinds()) { names.push_back (upper (k) + "S"); filters.push_back (k); }
    float y = panel.y + 48.0f;
    for (std::size_t i = 0; i < names.size(); ++i, y += 40.0f)
    {
        int count = 0;
        for (auto& e : lib.all()) if (filters[i].empty() || e.kind == filters[i]) ++count;
        const Rect r { panel.x + 8.0f, y, panel.w - 16.0f, 36.0f };
        const bool on = kindFilter == filters[i];
        const auto pr = ui.behave (makeId ("lib.kind", i), r);
        p.fillRound (r, 4.0f, on ? alpha (kLib, 0.3f) : alpha (colours::well, pr.hover ? 0.85f : 0.6f));
        p.panelText (names[i], reduced (r, 10.0f, 0.0f), 13.0f, HAlign::left, VAlign::middle, on ? brighter (kLib, 0.5f) : colours::paper);
        p.panelText (std::to_string (count), reduced (r, 10.0f, 0.0f), 13.0f, HAlign::right, VAlign::middle, colours::paperDim);
        if (pr.clicked) { kindFilter = filters[i]; refresh(); }
    }
    p.wrapped ("Your own beats and songs: Band Forge parts you took through Suno and brought back. Kept on this computer only.",
               { panel.x + 10.0f, y + 10.0f, panel.w - 20.0f, 120.0f }, 12.0f, false, colours::paperDim, 6);
    if (ui.button (makeId ("lib.folder"), reduced ({ panel.x, panel.bottom() - 40.0f, panel.w, 40.0f }, 8.0f, 4.0f), "OPEN LIBRARY FOLDER", ButtonLook::plate, kLib))
    {
        std::error_code ec;
        fs::create_directories (lib.getFolder(), ec);
        platform::reveal (lib.getFolder());
    }
}

void LibraryPanel::draw (Ui& ui, Rect area)
{
    auto& p = ui.p;
    // periodic save (edits are written within a second, never on every key press)
    if (dirty && ui.now() - lastSave > 1.0)
    {
        std::string err;
        if (! lib.save (err)) app.status ("LIBRARY: " + err);
        dirty = false;
        lastSave = ui.now();
    }

    Rect r = reduced (area, 14.0f, 6.0f);
    p.panelText ("LIBRARY  -  your own beats and songs, only on this computer", takeTop (r, 30.0f), 15.0f, HAlign::left, VAlign::middle,
                 brighter (kLib, 0.4f));
    takeTop (r, 6.0f);
    Rect right = takeRight (r, std::min (380.0f, r.w / 3.0f));
    takeRight (r, 14.0f);

    Rect row = takeTop (r, 34.0f);
    const Rect addB = reduced (takeRight (row, 150.0f), 2.0f);
    if (ui.button (makeId ("lib.add"), addB, "ADD FILES...", ButtonLook::plate, kLib))
    {
        const fs::path downloads = pg::files::documentsFolder().parent_path() / "Downloads";
        auto files = platform::openFiles (app.hwnd(), "Add to the library", downloads, { { "Audio files", pg::MediaLoader::wildcard() } }, true);
        if (! files.empty()) addFiles (files);
    }
    ui.tooltip (addB, "Copy songs / beats / stems into the library (you can also drop files here)");
    FieldStyle fs;
    fs.accent = kLib;
    fs.fontHeightL = 14.0f;
    fs.placeholder = "Search: a word, a genre, a key (\"minor\"), or a tempo (\"140\")";
    if (ui.textField (makeId ("lib.search"), reduced (row, 2.0f), search, fs)) refresh();
    takeTop (r, 6.0f);
    const Rect statusR = takeBottom (r, 22.0f);
    std::string status = lib.size() == 0 ? std::string ("Nothing here yet. Drop your Suno downloads on this page, or press ADD FILES.")
                                         : std::to_string (rows.size()) + " of " + std::to_string (lib.size()) + " shown"
                                               + (pendingMeasures > 0 ? "  -  measuring " + std::to_string (pendingMeasures) + "..." : std::string());
    p.text (status, statusR, 12.5f, false, colours::paperDim);

    //---------------------------------------------------------------- table
    const Rect table = r;
    p.fillRound (table, 3.0f, alpha (colours::well, 0.85f));
    float total = 0.0f;
    for (auto& c : kColumns) total += c.width;
    const float stretch = table.w / total;
    Rect head = withHeight (table, kHeadH);
    p.fill (head, argb (0xff181c22));
    float x = table.x;
    std::vector<std::pair<float, float>> colX;
    for (auto& c : kColumns)
    {
        const float w = c.width * stretch;
        colX.push_back ({ x, w });
        const Rect hr { x, head.y, w, head.h };
        const auto pr = ui.behave (makeId ("lib.head", (std::uint64_t) c.id), hr);
        if (pr.clicked)
        {
            const int col = c.id - 1;
            if (sortColumn == col) sortForwards = ! sortForwards;
            else { sortColumn = col; sortForwards = c.id == cTitle || c.id == cKind || c.id == cGenre; }
            refresh();
        }
        std::string label = c.name;
        if (sortColumn == c.id - 1) label += sortForwards ? "  +" : "  -";
        p.text (label, reduced (hr, 6.0f, 0.0f), 11.5f, true, pr.hover ? colours::white : brighter (kLib, 0.4f));
        x += w;
    }
    const Rect body = trimTop (table, kHeadH);
    const float content = (float) rows.size() * kRowH;
    scroll = std::clamp (scroll, 0.0f, std::max (0.0f, content - body.h));
    p.pushClip (body);
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const Rect rr { body.x, body.y + (float) i * kRowH - scroll, body.w, kRowH };
        if (rr.bottom() < body.y || rr.y > body.bottom()) continue;
        const auto* e = lib.find (rows[(std::size_t) i]);
        if (e == nullptr) continue;
        const auto pr = ui.behave (makeId ("lib.row", (std::uint64_t) i), rr);
        if (pr.clicked || pr.doubleClick) selectedRow = i;
        if (pr.doubleClick) togglePreview();
        const bool sel = i == selectedRow;
        p.fill (rr, sel ? alpha (kLib, 0.28f) : (i % 2 ? argb (0xff12161c) : argb (0xff0f1217)));
        if (rows[(std::size_t) i] == previewId && app.engine.isPreviewPlaying()) p.fill (withWidth (rr, 3.0f), kLib, true);
        for (std::size_t c = 0; c < std::size (kColumns); ++c)
        {
            const Rect cell { colX[c].first, rr.y, colX[c].second, rr.h };
            std::string t;
            HAlign just = HAlign::left;
            switch (kColumns[c].id)
            {
                case cStars:  t = e->stars > 0 ? std::string ((std::size_t) e->stars, '*') : std::string(); break;
                case cTitle:  t = e->title; break;
                case cKind:   t = e->kind; break;
                case cGenre:  t = e->genre; break;
                case cBpm:    t = ! e->measured ? "..." : e->facts.bpm > 0.0 ? num (e->facts.bpm, e->facts.bpm == std::floor (e->facts.bpm) ? 0 : 1) : "-"; just = HAlign::right; break;
                case cKey:    t = ! e->measured ? "..." : e->facts.keyRoot >= 0 ? e->facts.keyName() : "-"; break;
                case cLength: t = e->facts.seconds > 0.0 ? timeText (e->facts.seconds) : "..."; just = HAlign::right; break;
                case cLufs:   t = e->measured && e->facts.lufs > -70.0f ? num (e->facts.lufs, 1) : "-"; just = HAlign::right; break;
                case cAdded:  t = dateText (e->added); break;
                default: break;
            }
            p.text (t, reduced (cell, 6.0f, 0.0f), 13.0f, kColumns[c].id == cTitle, kColumns[c].id == cStars ? brighter (kLib, 0.5f) : colours::paper, just);
        }
    }
    p.popClip();
    if (const float w = ui.wheel (body); w != 0.0f) scroll = std::clamp (scroll - w * 90.0f, 0.0f, std::max (0.0f, content - body.h));

    // drop files on the page
    if (auto dropped = ui.takeDrop (area); ! dropped.empty()) addFiles (dropped);

    //---------------------------------------------------------------- details
    p.fillRound (expanded (right, 8.0f, 6.0f), 6.0f, alpha (colours::panel, 0.55f));
    Rect d = reduced (right, 8.0f, 4.0f);
    Rect facts = takeTop (d, 44.0f);
    auto* e = current();
    if (e == nullptr)
    {
        p.panelText ("Pick something on the list", facts, 13.0f, HAlign::left, VAlign::middle, colours::paperDim);
        return;
    }
    p.panelText (e->measured ? e->summary() : e->kind + "  -  measuring...", takeTop (facts, 22.0f), 14.0f, HAlign::left, VAlign::middle,
                 brighter (kLib, 0.5f));
    std::string more;
    if (e->measured && e->facts.lufs > -70.0f) more = num (e->facts.lufs, 1) + " LUFS   peak " + num (e->facts.peakDb, 1) + " dB";
    if (e->measured && e->facts.bpm > 0.0 && e->facts.tempoConfidence < 0.3f) more += "   (tempo is a rough guess)";
    p.panelText (more, facts, 12.0f, HAlign::left, VAlign::middle, colours::paperDim);

    auto line = [&d] (float h) { const Rect x = takeTop (d, h); takeTop (d, 6.0f); return x; };
    const Id base = makeId ("lib.detail");
    FieldStyle tf;
    tf.accent = kLib;
    tf.fontHeightL = 14.0f;
    std::string title = e->title;
    if (ui.textField (subId (base, 1), line (30.0f), title, tf))
    {
        bool blank = true;
        for (char c : title) if (c != ' ') { blank = false; break; }
        if (! blank) { e->title = title; dirty = true; }
    }
    Rect two = line (30.0f);
    {
        const auto& kinds = pg::library::Library::kinds();
        int k = 0;
        for (int i = 0; i < (int) kinds.size(); ++i) if (kinds[(std::size_t) i] == e->kind) k = i;
        if (ui.combo (subId (base, 2), trimRight (takeLeft (two, two.w * 0.5f), 3.0f), kinds, k, kLib)) { e->kind = kinds[(std::size_t) k]; dirty = true; }
        Rect g = trimLeft (two, 3.0f);
        const Rect pick = takeRight (g, 26.0f);
        std::string genre = e->genre;
        if (ui.textField (subId (base, 3), g, genre, tf)) { e->genre = genre; dirty = true; }
        if (ui.button (subId (base, 4), pick, "v", ButtonLook::dark, kLib))
        {
            std::vector<MenuItem> m;
            for (int gi = 0; gi < pg::band::kNumGenres; ++gi) m.push_back (MenuItem::item (pg::band::genreName ((pg::band::Genre) gi), gi + 1));
            const std::string id = e->id;
            ui.openMenu (std::move (m), pick.x, pick.bottom(), [this, id] (int r)
            {
                if (auto* en = lib.find (id)) { en->genre = pg::band::genreName ((pg::band::Genre) (r - 1)); dirty = true; }
            });
        }
        ui.tooltip (pick, "Pick a Band Forge genre (or type your own)");
    }
    two = line (30.0f);
    {
        std::vector<std::string> starItems { "NOT RATED" };
        for (int s = 1; s <= 5; ++s) starItems.push_back (starText (s));
        int st = std::clamp (e->stars, 0, 5);
        if (ui.combo (subId (base, 5), trimRight (takeLeft (two, two.w * 0.5f), 3.0f), starItems, st, kLib)) { e->stars = st; dirty = true; }
        Rect bpmB = trimLeft (two, 3.0f);
        const bool hasBpm = e->facts.bpm > 0.0;
        const Rect dbl = trimRight (takeLeft (bpmB, bpmB.w * 0.5f), 2.0f);
        if (ui.button (subId (base, 6), dbl, "BPM x2", ButtonLook::plate, kLib, hasBpm)) { e->facts.bpm *= 2.0; dirty = true; refresh(); }
        ui.tooltip (dbl, "The tempo read half as fast as it really is? Double it.");
        const Rect half = trimLeft (bpmB, 2.0f);
        if (ui.button (subId (base, 7), half, "BPM / 2", ButtonLook::plate, kLib, hasBpm)) { e->facts.bpm *= 0.5; dirty = true; refresh(); }
        ui.tooltip (half, "The tempo read twice as fast as it really is? Halve it.");
    }
    two = line (30.0f);
    {
        auto takes = pg::library::Library::bandForgeTakes();
        std::vector<std::string> items { "NOT FROM A BAND FORGE TAKE" };
        int sel = 0;
        for (std::size_t i = 0; i < takes.size(); ++i)
        {
            items.push_back ("FROM: " + pg::files::toText (takes[i].filename()));
            if (pg::files::toText (takes[i]) == e->madeFrom) sel = (int) i + 1;
        }
        if (sel == 0 && ! e->madeFrom.empty())   // a take folder that has since moved
        {
            takes.push_back (pg::files::toPath (e->madeFrom));
            items.push_back ("FROM: " + pg::files::toText (takes.back().filename()) + " (moved?)");
            sel = (int) takes.size();
        }
        std::error_code ec;
        const Rect openB = trimLeft (takeRight (two, 100.0f), 4.0f);
        const bool takeExists = ! e->madeFrom.empty() && fs::is_directory (pg::files::toPath (e->madeFrom), ec);
        if (ui.button (subId (base, 8), openB, "OPEN TAKE", ButtonLook::plate, kLib, takeExists)) platform::reveal (pg::files::toPath (e->madeFrom));
        if (ui.combo (subId (base, 9), two, items, sel, kLib))
        {
            e->madeFrom = sel <= 0 || sel - 1 >= (int) takes.size() ? std::string() : pg::files::toText (takes[(std::size_t) sel - 1]);
            dirty = true;
        }
        ui.tooltip (two, "The Band Forge take this came from (the parts you took into Suno)");
    }
    Rect buttons = takeBottom (d, 36.0f);
    const Rect removeB = trimLeft (takeRight (buttons, 90.0f), 4.0f);
    const Rect showB = trimLeft (takeRight (buttons, 100.0f), 4.0f);
    if (ui.button (subId (base, 10), removeB, "REMOVE", ButtonLook::plate, kLib)) removeSelected();
    ui.tooltip (removeB, "Take it out of the library (the file is moved to Library/Removed, not deleted)");
    if (ui.button (subId (base, 11), showB, "SHOW FILE", ButtonLook::plate, kLib)) platform::reveal (lib.fileOf (*e));
    if (ui.button (subId (base, 12), buttons, "ADD TO SESSION", ButtonLook::plate, kLib)) addToSession();
    ui.tooltip (buttons, "Put it in the session as a track");
    if (current() == nullptr) return;   // removed just now
    e = current();
    takeBottom (d, 6.0f);
    Rect play = takeBottom (d, 44.0f);
    const bool mine = ! previewId.empty() && previewId == currentId();
    const bool playing = mine && app.engine.isPreviewPlaying();
    const std::string playText = decodingPreview == currentId() && ! decodingPreview.empty() ? "LOADING..." : (playing ? "STOP" : "PLAY");
    if (ui.button (subId (base, 13), takeLeft (play, 100.0f), playText, ButtonLook::plate, kLib, true, playing)) togglePreview();
    {
        double len = std::max (0.1, e->facts.seconds), pos = 0.0;
        if (mine)
            if (auto m = app.engine.getPreviewMedia())
            {
                len = std::max (0.1, (double) m->getNumFrames() / m->getSourceRate());
                pos = app.engine.getPreviewPosition();
            }
        float n = (float) std::clamp (pos / len, 0.0, 1.0);
        const Id sid = subId (base, 14);
        int gesture = 0;
        if (seekNorm >= 0.0f) n = seekNorm;
        if (ui.hslider (sid, centred (trimLeft (play, 8.0f), play.w - 8.0f, 16.0f), n, kLib, &gesture)) seekNorm = n;
        if (gesture == -1 && seekNorm >= 0.0f)
        {
            if (mine) app.engine.seekPreview (seekNorm * len);
            seekNorm = -1.0f;
        }
    }
    takeBottom (d, 6.0f);
    FieldStyle nf = tf;
    nf.multiline = true;
    std::string notes = e->notes;
    p.text ("NOTES  (what you did in Suno, which take, what to try next...)", takeTop (d, 16.0f), 10.5f, true, colours::paperDim);
    if (ui.textField (subId (base, 15), d, notes, nf)) { e->notes = notes; dirty = true; }
}
} // namespace hem::app
