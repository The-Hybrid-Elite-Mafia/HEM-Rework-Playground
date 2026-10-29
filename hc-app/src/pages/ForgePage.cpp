#include "ForgePage.hpp"
#include "BandForgePanel.hpp"
#include "LibraryPanel.hpp"
#include "../platform/Platform.hpp"

#include "hem/pg/Files.hpp"
#include "media/MediaLoader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace hem::app
{
namespace
{
    constexpr const char* kServerKey = "server";
    constexpr const char* kApiKeyKey = "apiKey";
    constexpr const char* kFolderKey = "aceFolder";
    constexpr const char* kDownloadPage = "https://github.com/ACE-Step/ACE-Step-1.5/blob/main/docs/en/INSTALL.md";
    const Rgba kForge = argb (0xffe0a526);
    const Rgba kCopper = argb (0xffd9822b);

    std::vector<std::string> keyChoices()
    {
        std::vector<std::string> k { "Key: Auto" };
        for (auto* n : { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" })
        {
            k.push_back (std::string (n) + " major");
            k.push_back (std::string (n) + " minor");
        }
        return k;
    }
    const std::vector<std::string> kTimeSigs { "Time: Auto", "4/4", "3/4", "6/8", "2/4" };
    const char* kTimeSigValue[] { "", "4", "3", "6", "2" };
    const std::pair<const char*, const char*> kLanguages[] { { "en", "English" }, { "es", "Spanish" }, { "fr", "French" }, { "de", "German" },
                                                             { "it", "Italian" }, { "pt", "Portuguese" }, { "ja", "Japanese" }, { "ko", "Korean" },
                                                             { "zh", "Chinese" }, { "ru", "Russian" } };

    std::string stamp()
    {
        const std::time_t t = std::time (nullptr);
        std::tm tm {};
#if defined(_WIN32)
        localtime_s (&tm, &t);
#else
        localtime_r (&t, &tm);
#endif
        char b[32];
        std::strftime (b, sizeof b, "%Y-%m-%d %H-%M-%S", &tm);
        return b;
    }
}

/** A knob with its value in a dark box underneath (0.10.0's rotary sliders with a text box). */
bool valueKnob (Ui& ui, Id id, Rect cell, float& value, float lo, float hi, float step, float resetTo, Rgba accent, const std::string& text)
{
    Rect c = cell;
    const Rect box = centred (takeBottom (c, 20.0f), std::min (c.w, 110.0f), 20.0f);
    const float d = std::min (c.w, c.h);
    float n = (value - lo) / (hi - lo);
    bool changed = false;
    if (ui.knob (id, centred (c, d, d), n, (resetTo - lo) / (hi - lo), accent))
    {
        float v = lo + n * (hi - lo);
        if (step > 0.0f) v = lo + std::round ((v - lo) / step) * step;
        v = std::clamp (v, lo, hi);
        changed = v != value;
        value = v;
    }
    ui.p.fillRound (box, 3.0f, colours::well);
    ui.p.text (text, box, 12.0f, true, brighter (accent, 0.5f), HAlign::centre, VAlign::middle, TextLook::neon);
    return changed;
}

//==============================================================================
ForgePage::ForgePage (App& a) : app (a), aceSettings (Settings::songForgeFile())
{
    aceSettings.load();
    style = app.settings.get ("forgeStyle");
    lyrics = app.settings.get ("forgeLyrics");
    seconds = (float) std::clamp (app.settings.getDouble ("forgeSeconds", 180.0), 10.0, 600.0);
    bpm = (float) std::clamp (app.settings.getDouble ("forgeBpm", 0.0), 0.0, 300.0);
    takeCount = (float) std::clamp (app.settings.getDouble ("forgeTakes", 2.0), 1.0, 4.0);
    key = std::clamp (app.settings.getInt ("forgeKey", 0), 0, 24);
    timeSig = std::clamp (app.settings.getInt ("forgeTimeSig", 0), 0, 4);
    language = std::clamp (app.settings.getInt ("forgeLanguage", 0), 0, 9);
    smart = app.settings.getBool ("forgeSmart", false);
    instrumental = app.settings.getBool ("forgeInstrumental", false);
    progressText = "Write a STYLE (and lyrics), then press GENERATE. The finished takes appear on the left.";
    health.setServer (server(), apiKey());
    band = std::make_unique<BandForgePanel> (app);
    libraryPanel = std::make_unique<LibraryPanel> (app);
    band->onSendToLibrary = [this] (const std::vector<fs::path>& files, const std::string& genre, const fs::path& take)
    {
        const std::string kind = files.size() == 1 ? "Beat" : "Stem";
        if (libraryPanel->addFiles (files, genre, take, kind, "Band Forge  " + pg::files::toText (take.filename())) > 0) setMode (Mode::library);
    };
    const int saved = app.settings.getInt ("forgeMode", app.settings.getBool ("forgeBandMode", true) ? 0 : 1);
    mode = (Mode) std::clamp (saved, 0, 2);
    reloadLastTakes();
}

ForgePage::~ForgePage() { job.reset(); }

void ForgePage::setMode (Mode m)
{
    mode = m;
    app.settings.set ("forgeMode", (int) m);
}

std::string ForgePage::server() const { return aceSettings.get (kServerKey, pg::ace::kDefaultServer); }
std::string ForgePage::apiKey() const { return aceSettings.get (kApiKeyKey); }
fs::path ForgePage::aceFolder() const { return pg::files::toPath (aceSettings.get (kFolderKey)); }

//==============================================================================
void ForgePage::drawLeft (Ui& ui, Rect panel)
{
    if (mode == Mode::band) band->drawTakes (ui, panel);
    else if (mode == Mode::library) libraryPanel->drawKinds (ui, panel);
    else drawTakes (ui, panel);
}

void ForgePage::drawMain (Ui& ui, Rect panel)
{
    Rect all = panel;
    Rect tabsRow = reduced (takeTop (all, 40.0f), 14.0f, 4.0f);
    const std::pair<Mode, const char*> modes[] { { Mode::band, "BAND FORGE" }, { Mode::ace, "SONG FORGE  (ACE-STEP)" }, { Mode::library, "LIBRARY" } };
    const float widths[] { std::min (220.0f, tabsRow.w / 3.0f), std::min (260.0f, tabsRow.w / 2.0f), std::min (200.0f, tabsRow.w) };
    const char* tips[] { "BAND FORGE: guitar, banjo, fiddle, harmonica, drums... written and played right here, no internet",
                         "SONG FORGE: whole songs with vocals from ACE-Step running on this computer",
                         "LIBRARY: your own beats and songs (Band Forge parts taken through Suno and back), kept on this computer" };
    for (int i = 0; i < 3; ++i)
    {
        const Rect b = reduced (takeLeft (tabsRow, widths[i]), 2.0f, 0.0f);
        const bool on = mode == modes[i].first;
        if (ui.button (makeId ("forge.mode", (std::uint64_t) i), b, modes[i].second, ButtonLook::plate, kCopper, true, on)) setMode (modes[i].first);
        ui.tooltip (b, tips[i]);
    }
    if (mode == Mode::band) band->draw (ui, all);
    else if (mode == Mode::library) libraryPanel->draw (ui, all);
    else drawAce (ui, all);
}

void ForgePage::drawAce (Ui& ui, Rect all)
{
    auto& p = ui.p;
    const bool engineUp = health.isUp();
    Rect r = reduced (all, 14.0f, 10.0f);
    Rect top = takeTop (r, 36.0f);
    const Rect setupB = reduced (takeRight (top, 100.0f), 2.0f);
    const Rect startB = reduced (takeRight (top, 160.0f), 2.0f);
    if (ui.button (makeId ("ace.setup"), setupB, "SET UP")) showSetup();
    ui.tooltip (setupB, "Where ACE-Step is installed and its address");
    if (ui.button (makeId ("ace.start"), startB, "START ENGINE")) startEngine();
    ui.tooltip (startB, "Starts ACE-Step's server (start_api_server.bat) in its own window. Keep that window open while you make songs.");
    takeLeft (top, 24.0f);
    ui.led (top.x - 12.0f, top.cy(), 6.0f, engineUp ? colours::hemGreen : colours::meterRed, true);
    std::string label;
    if (engineUp) label = "ACE-STEP ENGINE READY";
    else if (engineStartedAt >= 0.0 && ui.now() - engineStartedAt < 15.0 * 60.0) label = "ACE-STEP ENGINE STARTING...  (the first start can take a few minutes)";
    else label = "ACE-STEP ENGINE NOT RUNNING  -  press START ENGINE";
    p.text (label, top, 14.0f, true, engineUp ? brighter (colours::hemGreen, 0.3f) : colours::paper);
    takeTop (r, 8.0f);

    Rect right = takeRight (r, std::min (420.0f, r.w / 3.0f));
    takeRight (r, 14.0f);
    p.panelText ("STYLE  (genre, instruments, mood, voice  -  e.g. dark trap, 140 bpm, 808s, eerie piano, male rap vocal)", takeTop (r, 20.0f), 13.0f,
                 HAlign::left, VAlign::middle, brighter (kForge, 0.4f));
    FieldStyle fs;
    fs.multiline = true;
    fs.accent = kForge;
    fs.fontHeightL = 15.0f;
    if (ui.textField (makeId ("ace.style"), takeTop (r, std::max (60.0f, r.h / 5.0f)), style, fs)) app.settings.set ("forgeStyle", style);
    takeTop (r, 8.0f);
    p.panelText ("LYRICS  ([Verse] [Chorus] [Bridge] tags; empty = instrumental)", takeTop (r, 20.0f), 13.0f, HAlign::left, VAlign::middle,
                 brighter (kForge, 0.4f));
    fs.enabled = ! instrumental;
    if (ui.textField (makeId ("ace.lyrics"), r, lyrics, fs)) app.settings.set ("forgeLyrics", lyrics);

    p.fillRound (expanded (right, 8.0f, 6.0f), 6.0f, alpha (colours::panel, 0.55f));
    Rect knobs = takeTop (right, std::min (130.0f, right.h / 3.0f));
    const float kw = knobs.w / 3.0f;
    char t[32];
    std::snprintf (t, sizeof t, "%d s", (int) seconds);
    if (valueKnob (ui, makeId ("ace.seconds"), reduced (takeLeft (knobs, kw), 6.0f, 0.0f), seconds, 10.0f, 600.0f, 1.0f, 180.0f, kForge, t))
        app.settings.set ("forgeSeconds", (double) seconds);
    ui.tooltip (Rect { knobs.x - kw, knobs.y, kw, knobs.h }, "Song length in seconds");
    const std::string bpmText = bpm < 1.0f ? "AUTO" : std::to_string ((int) bpm) + " BPM";
    if (valueKnob (ui, makeId ("ace.bpm"), reduced (takeLeft (knobs, kw), 6.0f, 0.0f), bpm, 0.0f, 300.0f, 1.0f, 0.0f, kForge, bpmText))
        app.settings.set ("forgeBpm", (double) bpm);
    ui.tooltip (Rect { knobs.x - kw, knobs.y, kw, knobs.h }, "Tempo (0 = ACE-Step picks)");
    if (valueKnob (ui, makeId ("ace.takes"), reduced (knobs, 6.0f, 0.0f), takeCount, 1.0f, 4.0f, 1.0f, 2.0f, kForge,
                   std::to_string ((int) takeCount) + " takes"))
        app.settings.set ("forgeTakes", (double) takeCount);
    ui.tooltip (knobs, "Versions per press. 8 GB graphics cards: 2 at most.");
    takeTop (right, 10.0f);
    if (ui.combo (makeId ("ace.key"), reduced (takeTop (right, 30.0f), 2.0f), keyChoices(), key, kForge)) app.settings.set ("forgeKey", key);
    Rect row = takeTop (right, 34.0f);
    std::vector<std::string> langs;
    for (auto& l : kLanguages) langs.push_back (l.second);
    if (ui.combo (makeId ("ace.time"), reduced (takeLeft (row, row.w * 0.5f), 2.0f), kTimeSigs, timeSig, kForge)) app.settings.set ("forgeTimeSig", timeSig);
    if (ui.combo (makeId ("ace.lang"), reduced (row, 2.0f), langs, language, kForge)) app.settings.set ("forgeLanguage", language);
    takeTop (right, 6.0f);
    row = takeTop (right, 34.0f);
    const Rect smartR = reduced (takeLeft (row, row.w * 0.5f), 2.0f);
    if (ui.ledToggle (makeId ("ace.smart"), smartR, smart, "SMART", kForge)) app.settings.set ("forgeSmart", smart);
    ui.tooltip (smartR, "ACE-Step plans the song first: better structure, slower. Needs more than 6 GB of graphics memory - leave OFF on 8 GB laptops.");
    const Rect instR = reduced (row, 2.0f);
    if (ui.ledToggle (makeId ("ace.instr"), instR, instrumental, "INSTRUMENTAL", kForge)) app.settings.set ("forgeInstrumental", instrumental);
    ui.tooltip (instR, "No vocals: the lyrics are ignored");
    takeTop (right, 12.0f);
    row = takeTop (right, 56.0f);
    const Rect cancelB = reduced (takeRight (row, 110.0f), 2.0f);
    if (ui.button (makeId ("ace.generate"), reduced (row, 2.0f), "GENERATE", ButtonLook::plate, kForge, job == nullptr)) generate();
    if (ui.button (makeId ("ace.cancel"), cancelB, "CANCEL", ButtonLook::plate, kForge, job != nullptr) && job) job->cancel();
    takeTop (right, 8.0f);
    p.wrapped (progressText, takeTop (right, std::min (90.0f, right.h)), 13.5f, false, colours::paper, 5);
}

void ForgePage::drawTakes (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    Rect r = reduced (panel, 8.0f);
    p.panelText ("TAKES", reduced (takeTop (r, 34.0f), 2.0f, 0.0f), 15.0f);
    Rect bottom = takeBottom (r, 110.0f);
    p.fillRound (r, 3.0f, alpha (colours::well, 0.85f));
    p.pushClip (r);
    for (int i = 0; i < (int) takes.size(); ++i)
    {
        const Rect row { r.x, r.y + (float) i * 34.0f, r.w, 34.0f };
        if (row.y > r.bottom()) break;
        const auto pr = ui.behave (makeId ("ace.take", (std::uint64_t) i), row);
        if (pr.clicked || pr.doubleClick) selectedTake = i;
        if (pr.doubleClick) addTake (i);
        if (selectedTake == i) p.fill (row, alpha (kForge, 0.3f));
        const auto& t = takes[(std::size_t) i];
        p.text ("TAKE " + std::to_string (i + 1), { row.x + 8, row.y, 70, row.h }, 13.0f, true, brighter (kForge, 0.4f));
        std::string info;
        const int secs = (int) std::lround (t.seconds);
        char b[48];
        if (secs > 0) { std::snprintf (b, sizeof b, "%d:%02d  ", secs / 60, secs % 60); info += b; }
        if (t.bpm > 0.0) info += std::to_string ((int) std::lround (t.bpm)) + " BPM  ";
        info += t.key;
        p.text (info, { row.x + 78, row.y, row.w - 82, row.h }, 12.0f, false, colours::paper);
    }
    p.popClip();
    const bool any = ! takes.empty();
    takeTop (bottom, 6.0f);
    const Rect addB = takeTop (bottom, 34.0f);
    if (ui.button (makeId ("ace.add"), addB, "ADD TO SESSION", ButtonLook::plate, kForge, any)) addTake (std::max (0, selectedTake));
    ui.tooltip (addB, "Put the selected take into the session as a new track");
    takeTop (bottom, 4.0f);
    Rect row = takeTop (bottom, 34.0f);
    if (ui.button (makeId ("ace.all"), trimRight (takeLeft (row, row.w * 0.5f), 2.0f), "ADD ALL", ButtonLook::plate, kForge, any))
        for (int i = 0; i < (int) takes.size(); ++i) addTake (i);
    std::error_code ec;
    if (ui.button (makeId ("ace.folder"), trimLeft (row, 2.0f), "OPEN FOLDER", ButtonLook::plate, kForge, fs::is_directory (takesFolder, ec)))
        platform::reveal (takesFolder);
}

//==============================================================================
void ForgePage::generate()
{
    if (job != nullptr) return;
    pg::ace::Request req;
    req.style = style;
    req.lyrics = lyrics;
    req.instrumental = instrumental;
    req.seconds = seconds;
    req.bpm = (int) std::lround (bpm);
    req.key = key > 0 ? keyChoices()[(std::size_t) key] : std::string();
    req.timeSignature = kTimeSigValue[std::clamp (timeSig, 0, 4)];
    req.language = kLanguages[std::clamp (language, 0, 9)].first;
    req.takes = (int) std::lround (takeCount);
    req.smart = smart;
    bool blank = true;
    for (char c : req.style) if (c != ' ' && c != '\n' && c != '\t') { blank = false; break; }
    if (blank) { progressText = "Type a STYLE first (genre, instruments, mood, voice)."; return; }
    if (! health.isUp()) progressText = "The engine does not answer yet - trying anyway...";
    takesFolder = App::songsFolder() / "Song Forge" / pg::files::toPath (stamp());
    std::error_code ec;
    fs::create_directories (takesFolder, ec);
    job = std::make_unique<pg::ace::Job> (req, server(), apiKey(), takesFolder,
                                          [this] (const std::string& text) { progressText = text; },
                                          [this] (pg::ace::Result r) { finished (std::move (r)); });
    job->start();
    app.status ("SONG FORGE: making " + std::to_string (req.takes) + " take(s), " + std::to_string ((int) req.seconds) + " s each...");
}

void ForgePage::reloadLastTakes()
{
    const fs::path root = App::songsFolder() / "Song Forge";
    std::error_code ec;
    if (! fs::is_directory (root, ec)) return;
    fs::path newest;
    std::vector<fs::path> newestFiles;
    for (const auto& d : fs::directory_iterator (root, ec))
    {
        if (! d.is_directory (ec)) continue;
        std::vector<fs::path> files;
        for (const auto& f : fs::directory_iterator (d.path(), ec))
            if (f.is_regular_file (ec) && pg::files::toText (f.path().filename()).rfind ("SF Take ", 0) == 0 && pg::MediaLoader::isAudioFile (f.path()))
                files.push_back (f.path());
        // folder names are "YYYY-MM-DD HH-MM-SS": the largest name is the newest
        if (! files.empty() && (newest.empty() || d.path().filename() > newest.filename())) { newest = d.path(); newestFiles = std::move (files); }
    }
    if (newest.empty()) return;
    std::sort (newestFiles.begin(), newestFiles.end());
    takes.clear();
    for (auto& f : newestFiles)
    {
        pg::ace::Take t;
        t.file = f;
        t.caption = "from " + pg::files::toText (newest.filename());
        takes.push_back (t);
    }
    takesFolder = newest;
    selectedTake = 0;
    progressText = std::to_string (takes.size()) + " take(s) from " + pg::files::toText (newest.filename())
                   + " are on the left. Pick one and press ADD TO SESSION.";
}

void ForgePage::finished (pg::ace::Result r)
{
    job.reset();
    if (! r.ok)
    {
        progressText = r.cancelled ? std::string ("Cancelled.") : "Could not make the song: " + r.error;
        app.status ("SONG FORGE: " + (r.cancelled ? std::string ("cancelled") : r.error));
        return;
    }
    takes = r.takes;
    selectedTake = 0;
    const int secs = (int) std::lround (r.secondsTaken);
    char b[64];
    std::snprintf (b, sizeof b, " take(s) ready in %d:%02d.", secs / 60, secs % 60);
    progressText = std::to_string (takes.size()) + b + " Pick one on the left and press ADD TO SESSION.";
    app.status ("SONG FORGE: " + std::to_string (takes.size()) + " take(s) saved in " + pg::files::toText (takesFolder));
}

void ForgePage::addTake (int index)
{
    if (index < 0 || index >= (int) takes.size()) return;
    const auto& t = takes[(std::size_t) index];
    std::error_code ec;
    if (! fs::exists (t.file, ec)) { app.status ("SONG FORGE: the take file is gone (" + pg::files::toText (t.file) + ")"); return; }
    if (t.bpm > 0.0 && app.engine.getNumUsedTracks() == 0) app.setMixValue (E::Ids::bpm, (float) t.bpm);   // first song in: take its tempo
    app.importFiles ({ t.file });
}

void ForgePage::showSetup()
{
    DialogSpec d;
    d.title = "SONG FORGE - set up ACE-Step";
    d.message = "SONG FORGE uses ACE-Step 1.5 (free) running on THIS computer.\n\n"
                "If Hybrid Studio's Song Forge already works on this PC, nothing to do here: both apps share the same setting.\n\n"
                "Otherwise: download the Windows portable package, unpack it (e.g. C:\\ACE-Step-1.5), pick that folder below, SAVE, then START ENGINE.";
    d.fields = { { "ACE-STEP FOLDER (the one with start_api_server.bat)", pg::files::toText (aceFolder()) },
                 { "SERVER ADDRESS", server() },
                 { "API KEY (leave empty unless you set one)", apiKey() } };
    d.buttons = { "SAVE", "BROWSE...", "GET ACE-STEP", "CANCEL" };
    d.accent = kForge;
    d.width = 640.0f;
    showDialog (app.ui(), d, [this] (int b, const std::vector<std::string>& v)
    {
        if (b == 2) { platform::openUrl (kDownloadPage); return; }
        if (b == 1)
        {
            if (auto f = platform::pickFolder (app.hwnd(), "Pick the ACE-Step folder (with start_api_server.bat)", aceFolder()))
            {
                aceSettings.set (kFolderKey, pg::files::toText (*f));
                aceSettings.save();
            }
            showSetup();
            return;
        }
        if (b != 0 || v.size() < 3) return;
        auto trim = [] (std::string s)
        {
            while (! s.empty() && (s.back() == ' ' || s.back() == '"')) s.pop_back();
            while (! s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase (s.begin());
            return s;
        };
        std::string s = trim (v[1]);
        if (s.empty()) s = pg::ace::kDefaultServer;
        if (s.rfind ("http", 0) != 0) s = "http://" + s;
        aceSettings.set (kServerKey, s);
        aceSettings.set (kApiKeyKey, trim (v[2]));
        aceSettings.set (kFolderKey, trim (v[0]));
        aceSettings.save();
        health.setServer (server(), apiKey());
    });
}

void ForgePage::startEngine()
{
    if (health.isUp()) { app.status ("SONG FORGE: the engine is already running"); return; }
    const fs::path folder = aceFolder();
    std::error_code ec;
    if (folder.empty() || ! fs::exists (folder / "start_api_server.bat", ec))
    {
        app.status ("SONG FORGE: tell me where ACE-Step is first (SET UP)");
        showSetup();
        return;
    }
    const bool ok = platform::startInOwnWindow (folder, "start_api_server.bat", "ACE-Step engine");
    engineStartedAt = app.ui().now();
    app.status (ok ? std::string ("SONG FORGE: starting ACE-Step in its own window - the light turns green when it is ready")
                   : "SONG FORGE: could not start " + pg::files::toText (folder / "start_api_server.bat"));
}
} // namespace hem::app
