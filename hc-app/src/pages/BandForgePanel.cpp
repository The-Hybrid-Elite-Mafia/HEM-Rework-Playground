#include "BandForgePanel.hpp"
#include "../platform/Platform.hpp"

#include "hem/pg/Files.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace hem::app
{
using namespace pg::band;

bool valueKnob (Ui& ui, Id id, Rect cell, float& value, float lo, float hi, float step, float resetTo, Rgba accent, const std::string& text);

namespace
{
    const Rgba kBand = argb (0xffd9822b);   // copper: Band Forge's colour next to Song Forge's gold
    const char* kKeys[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    constexpr double kRenderRate = 48000.0;

    std::string instrumentsKey (int g) { return "bandInstruments" + std::to_string (g); }

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

    std::string join (const std::vector<std::string>& v, const char* sep)
    {
        std::string s;
        for (std::size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; }
        return s;
    }

    std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
    std::string upper (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; }
}

//==============================================================================
/** Writes and plays the song on its own thread. Everything it touches is its own copy. */
class BandForgePanel::Job
{
public:
    using Done = std::function<void (bool, const std::string&, Take)>;

    Job (Request r, fs::path where, std::shared_ptr<const SoundPack> pack, MixFinish finish, Done d)
        : request (std::move (r)), folder (std::move (where)), sounds (std::move (pack)), mixFinish (finish), done (std::move (d))
    {
        thread = std::thread ([this] { run(); });
    }

    ~Job()
    {
        alive->store (false);
        cancelFlag.store (true);
        if (thread.joinable()) thread.join();
    }

    void cancel() { cancelFlag.store (true); }
    float getProgress() const noexcept { return progress.load(); }
    std::string getStage() const { const std::scoped_lock sl (stageLock); return stage; }

private:
    void setStage (const std::string& s) { const std::scoped_lock sl (stageLock); stage = s; }

    void run()
    {
        Take take;
        std::string error;
        bool ok = false;
        setStage ("Writing the song...");
        const Song song = compose (request);
        take.title = song.describe();
        take.genre = genreName (song.request.genre);
        take.keyText = std::string (theory::keyName (song.request.keyRoot)) + (song.request.minor ? " minor" : " major");
        take.bpm = song.bpm;
        take.seconds = song.lengthSeconds (0.0);
        take.folder = folder;
        const int parts = (int) song.parts.size();
        const SoundPack* pack = sounds != nullptr && sounds->isOpen() ? sounds.get() : nullptr;
        auto playing = [this, &song, parts, pack] (int i)
        {
            const auto inst = song.parts[(std::size_t) i].instrument;
            setStage ("Playing " + std::string (instrumentName (inst)) + (pack != nullptr && pack->has (inst) ? " (recorded)" : " (modeled)")
                      + "  (" + std::to_string (i + 1) + " of " + std::to_string (parts) + ")...");
        };
        std::vector<Stem> stems;
        if (parts > 0) playing (0);
        progress = 0.05f;
        if (parts == 0) error = "No instruments picked.";
        else if (! renderSong (song, kRenderRate, stems, [&] (float p)
                               {
                                   progress = 0.05f + 0.85f * p;
                                   const int idx = std::min (parts - 1, (int) std::lround (p * (float) parts));
                                   if (idx >= 0) playing (idx);
                               }, &cancelFlag, pack)
                 || cancelFlag.load())
            error = "Cancelled.";
        else
        {
            setStage ("Saving the stems...");
            take.stems = writeStems (song, stems, kRenderRate, folder, error);
            for (auto& st : stems) (st.recorded ? take.recorded : take.modeled).push_back (instrumentName (st.instrument));
            ok = error.empty() && ! take.stems.empty();
            if (ok && mixFinish != MixFinish::none)
            {
                setStage (mixFinish == MixFinish::mastered ? "Mastering the mix (-14 LUFS)..." : "Saving the clean mix...");
                progress = 0.93f;
                const fs::path mixFile = folder / (mixFinish == MixFinish::mastered ? "Band Forge mix (MASTERED).wav" : "Band Forge mix (CLEAN).wav");
                if (writeMix (stems, kRenderRate, mixFinish, mixFile, take.mixInfo, error)) take.mix = mixFile;
                else ok = false;
            }
            progress = 1.0f;
        }
        auto cb = done;
        auto flag = alive;
        Worker::toUi ([cb, flag, ok, error, take] { if (flag->load() && cb) cb (ok, error, take); });
    }

    Request request;
    fs::path folder;
    std::shared_ptr<const SoundPack> sounds;
    MixFinish mixFinish;
    Done done;
    std::atomic<bool> cancelFlag { false };
    std::atomic<float> progress { 0.0f };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    mutable std::mutex stageLock;
    std::string stage;
    std::thread thread;
};

//==============================================================================
BandForgePanel::BandForgePanel (App& a) : app (a)
{
    genre = std::clamp (app.settings.getInt ("bandGenre", 0), 0, kNumGenres - 1);
    key = std::clamp (app.settings.getInt ("bandKey", 7), 0, 11);
    mode = app.settings.getBool ("bandMinor", false) ? 1 : 0;
    length = app.settings.getBool ("bandShort", false) ? 1 : 0;
    tempo = (float) std::clamp (app.settings.getDouble ("bandBpm", 0.0), 0.0, 200.0);
    autoAdd = app.settings.getBool ("bandAutoAdd", true);
    mixChoice = std::clamp (app.settings.getInt ("bandMix", 2), 0, 2);
    soundPack = SoundPack::findInstalled();
    progressText = "Pick a style, key and instruments, then press FORGE. Each instrument comes out as its own track "
                   "(plus a MIDI file) - no internet, nothing leaves this computer.\n"
                   + (soundPack != nullptr && soundPack->isOpen() ? "Real recorded instruments: " + soundPack->describe() + ". The rest are modeled."
                                                                  : std::string ("HEM Sound Pack not found, so every instrument is modeled."));
    genreChanged (false);
}

BandForgePanel::~BandForgePanel() { job.reset(); }

void BandForgePanel::genreChanged (bool)
{
    const auto g = (Genre) genre;
    shown = availableInstruments (g);
    std::vector<int> picked;
    const std::string saved = app.settings.get (instrumentsKey (genre));
    if (! saved.empty())
    {
        std::stringstream ss (saved);
        std::string item;
        while (std::getline (ss, item, ',')) if (! item.empty()) picked.push_back (std::atoi (item.c_str()));
    }
    else
        for (auto i : defaultInstruments (g)) picked.push_back ((int) i);
    chosen.assign (shown.size(), false);
    for (std::size_t i = 0; i < shown.size(); ++i)
        chosen[i] = std::find (picked.begin(), picked.end(), (int) shown[i]) != picked.end();
}

void BandForgePanel::saveInstrumentChoice()
{
    std::vector<std::string> on;
    for (std::size_t i = 0; i < shown.size(); ++i) if (chosen[i]) on.push_back (std::to_string ((int) shown[i]));
    app.settings.set (instrumentsKey (genre), join (on, ","));
}

Request BandForgePanel::makeRequest() const
{
    Request r;
    r.genre = (Genre) genre;
    r.keyRoot = key;
    r.minor = mode == 1;
    r.bpm = tempo >= 40.0f ? tempo : 0.0;
    r.fullSong = length != 1;
    for (std::size_t i = 0; i < shown.size(); ++i) if (chosen[i]) r.instruments.push_back (shown[i]);
    std::random_device rd;
    r.seed = (std::uint32_t) (rd() & 0x7fffffffu) | 1u;   // every press = a new take
    return r;
}

void BandForgePanel::forge()
{
    if (job != nullptr) return;
    const auto req = makeRequest();
    if (req.instruments.empty()) { progressText = "Tick at least one instrument first."; return; }
    const fs::path folder = App::songsFolder() / "Band Forge" / pg::files::toPath (stamp() + " " + genreName (req.genre));
    soundPack = SoundPack::findInstalled();   // looked up every time, so a pack installed while the app runs is used
    const auto finish = mixChoice == 0 ? MixFinish::none : mixChoice == 1 ? MixFinish::clean : MixFinish::mastered;
    job = std::make_unique<Job> (req, folder, soundPack, finish, [this] (bool ok, const std::string& err, Take t) { finished (ok, err, std::move (t)); });
    app.status ("BAND FORGE: making a " + std::string (genreName (req.genre)) + " band...");
}

void BandForgePanel::finished (bool ok, const std::string& error, Take take)
{
    job.reset();
    if (! ok)
    {
        progressText = ! error.empty() ? error : std::string ("Something went wrong making the band.");
        app.status ("BAND FORGE: " + error);
        return;
    }
    takes.insert (takes.begin(), take);
    selectedTake = 0;
    const int secs = (int) std::lround (take.seconds);
    char b[32];
    std::snprintf (b, sizeof b, "%d:%02d", secs / 60, secs % 60);
    progressText = "Done: " + take.title + ", " + b + ", " + std::to_string (take.stems.size()) + " instruments.\n"
                   + (take.recorded.empty() ? std::string()
                                            : "Recorded: " + join (take.recorded, ", ")
                                                  + (take.modeled.empty() ? std::string() : ".  Modeled: " + join (take.modeled, ", ")) + ".\n")
                   + (take.mix.empty() ? std::string() : "Mix: " + pg::files::toText (take.mix.filename()) + " (" + take.mixInfo + ").\n")
                   + "Don't like it? Press FORGE again for a new take.";
    app.status ("BAND FORGE: " + std::to_string (take.stems.size()) + " stems saved in " + pg::files::toText (take.folder));
    if (autoAdd) addTake (0);
}

std::string BandForgePanel::sunoStyle (const Take& t)
{
    std::vector<std::string> inst;
    for (auto& n : t.recorded) inst.push_back (lower (n));
    for (auto& n : t.modeled) inst.push_back (lower (n));
    return lower (t.genre) + " instrumental, " + std::to_string ((int) std::lround (t.bpm)) + " BPM, " + t.keyText
           + (inst.empty() ? std::string() : ", " + join (inst, ", ")) + ", live band feel, warm mix";
}

void BandForgePanel::forSuno (int index)
{
    if (index < 0 || index >= (int) takes.size()) return;
    const auto& t = takes[(std::size_t) index];
    const auto style = sunoStyle (t);
    platform::copyText (app.hwnd(), style);
    std::error_code ec;
    const fs::path show = ! t.mix.empty() && fs::exists (t.mix, ec) ? t.mix : t.folder;
    if (fs::exists (show, ec)) platform::reveal (show);
    progressText = "Suno style copied:\n" + style + "\n"
                   + (! t.mix.empty() ? "Upload " + pg::files::toText (t.mix.filename()) + " in Suno and paste the style. "
                                      : std::string ("No mix file for this take (pick a MIX choice before FORGE); the stems are in its folder. "))
                   + "Bring what Suno makes back into the LIBRARY.";
    app.status ("BAND FORGE: Suno style copied to the clipboard");
}

void BandForgePanel::addTake (int index)
{
    if (index < 0 || index >= (int) takes.size()) return;
    const auto& t = takes[(std::size_t) index];
    std::vector<fs::path> present;
    std::error_code ec;
    for (auto& f : t.stems) if (fs::exists (f, ec)) present.push_back (f);
    if (present.empty()) { app.status ("BAND FORGE: the stem files are gone (" + pg::files::toText (t.folder) + ")"); return; }
    if (t.bpm > 0.0 && app.engine.getNumUsedTracks() == 0) app.setMixValue (E::Ids::bpm, (float) t.bpm);   // empty session: take the band's tempo
    app.importFiles (present);
}

//==============================================================================
void BandForgePanel::draw (Ui& ui, Rect area)
{
    auto& p = ui.p;
    const bool idle = job == nullptr;
    Rect r = reduced (area, 14.0f, 6.0f);
    p.panelText ("BAND FORGE  -  a band written and played on this computer", takeTop (r, 30.0f), 15.0f, HAlign::left, VAlign::middle,
                 brighter (kBand, 0.4f));
    takeTop (r, 6.0f);
    Rect right = takeRight (r, std::min (420.0f, r.w / 3.0f));
    takeRight (r, 14.0f);

    std::vector<std::string> genres;
    for (int g = 0; g < kNumGenres; ++g) genres.push_back (genreName ((Genre) g));
    std::vector<std::string> keys;
    for (auto* k : kKeys) keys.push_back (std::string ("Key: ") + k);

    Rect row = takeTop (r, 34.0f);
    const Rect genreR = reduced (takeLeft (row, row.w * 0.5f), 2.0f);
    if (ui.combo (makeId ("band.genre"), genreR, genres, genre, kBand, false, idle))
    {
        app.settings.set ("bandGenre", genre);
        genreChanged (true);
    }
    ui.tooltip (genreR, "The style of band");
    if (ui.combo (makeId ("band.length"), reduced (row, 2.0f), { "Full song (intro, verses, choruses, outro)", "Short (about a minute)" }, length, kBand,
                  false, idle))
        app.settings.set ("bandShort", length == 1);
    takeTop (r, 4.0f);
    row = takeTop (r, 34.0f);
    const Rect keyR = reduced (takeLeft (row, row.w * 0.5f), 2.0f);
    if (ui.combo (makeId ("band.key"), keyR, keys, key, kBand, false, idle)) app.settings.set ("bandKey", key);
    ui.tooltip (keyR, "Pick the key your vocal is in (Stem Scope tells you the key of a song)");
    if (ui.combo (makeId ("band.mode"), reduced (row, 2.0f), { "Major (happy, open)", "Minor (dark, sad)" }, mode, kBand, false, idle))
        app.settings.set ("bandMinor", mode == 1);
    takeTop (r, 10.0f);
    p.panelText ("INSTRUMENTS  (each one becomes its own track)", takeTop (r, 22.0f), 13.0f, HAlign::left, VAlign::middle, brighter (kBand, 0.4f));
    const int cols = r.w > 520.0f ? 3 : 2;
    const int rows = std::max (1, ((int) shown.size() + cols - 1) / cols);
    const float rowH = std::clamp (std::floor (r.h / (float) rows), 26.0f, 40.0f);
    for (std::size_t i = 0; i < shown.size(); ++i)
    {
        const int cx = (int) i % cols, cy = (int) i / cols;
        const Rect b { r.x + (float) cx * r.w / (float) cols, r.y + (float) cy * rowH, r.w / (float) cols - 4.0f, rowH - 4.0f };
        const bool rec = soundPack != nullptr && soundPack->has (shown[i]);
        bool on = chosen[i];
        if (ui.ledToggle (makeId ("band.inst", (std::uint64_t) shown[i]), b, on, upper (instrumentName (shown[i])) + (rec ? "  REC" : ""), kBand, false, idle))
        {
            chosen[i] = on;
            saveInstrumentChoice();
        }
        ui.tooltip (b, rec ? "Played from real recordings (HEM Sound Pack)" : "Modeled instrument (made by code, no samples)");
    }

    // right: tempo, options, FORGE
    p.fillRound (expanded (right, 8.0f, 6.0f), 6.0f, alpha (colours::panel, 0.55f));
    const float kh = std::min (130.0f, right.h / 3.0f);
    const Rect tempoCell = centred (takeTop (right, kh), 130.0f, kh);
    char t[48];
    if (tempo >= 40.0f) std::snprintf (t, sizeof t, "%d BPM", (int) tempo);
    else std::snprintf (t, sizeof t, "AUTO %d BPM", (int) std::lround (defaultBpm ((Genre) genre)));
    if (valueKnob (ui, makeId ("band.tempo"), tempoCell, tempo, 0.0f, 200.0f, 1.0f, 0.0f, kBand, t))
        app.settings.set ("bandBpm", tempo >= 40.0f ? (double) tempo : 0.0);
    ui.tooltip (tempoCell, "Tempo. All the way down = the style's usual tempo");
    takeTop (right, 8.0f);
    const Rect autoR = reduced (takeTop (right, 30.0f), 2.0f);
    if (ui.ledToggle (makeId ("band.autoadd"), autoR, autoAdd, "PUT IT IN THE SESSION WHEN DONE", kBand)) app.settings.set ("bandAutoAdd", autoAdd);
    ui.tooltip (autoR, "When the band is done, every instrument goes into the session as its own track");
    takeTop (right, 4.0f);
    const Rect mixR = reduced (takeTop (right, 32.0f), 2.0f);
    if (ui.combo (makeId ("band.mix"), mixR, { "STEMS ONLY", "STEMS + CLEAN MIX (no mastering, -3 dB peaks)", "STEMS + MASTERED MIX (loud, -14 LUFS)" },
                  mixChoice, kBand, false, idle))
        app.settings.set ("bandMix", mixChoice);
    ui.tooltip (mixR, "Also save the whole band as one WAV next to the stems. CLEAN: just the sum, for mastering somewhere else. "
                      "MASTERED: finished by Elite Master to streaming loudness (-14 LUFS, peaks under -1 dB).");
    takeTop (right, 8.0f);
    row = takeTop (right, 56.0f);
    const Rect cancelB = reduced (takeRight (row, 110.0f), 2.0f);
    const Rect forgeB = reduced (row, 2.0f);
    if (ui.button (makeId ("band.forge"), forgeB, "FORGE", ButtonLook::plate, kBand, idle)) forge();
    ui.tooltip (forgeB, "Write and play a new band. Every press is a new take with different parts.");
    if (ui.button (makeId ("band.cancel"), cancelB, "CANCEL", ButtonLook::plate, kBand, ! idle) && job) job->cancel();
    takeTop (right, 8.0f);
    const Rect bar = reduced (takeTop (right, 10.0f), 2.0f, 0.0f);
    if (! idle)
    {
        ui.progress (bar, job->getProgress(), kBand);
        progressText = job->getStage();
    }
    takeTop (right, 6.0f);
    p.wrapped (progressText, takeTop (right, std::min (130.0f, right.h)), 13.5f, false, colours::paper, 7);
}

void BandForgePanel::drawTakes (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    Rect r = reduced (panel, 8.0f);
    p.panelText ("BAND TAKES", reduced (takeTop (r, 34.0f), 2.0f, 0.0f), 15.0f);
    Rect bottom = takeBottom (r, 110.0f);
    p.fillRound (r, 3.0f, alpha (colours::well, 0.85f));
    const float rowH = 44.0f;
    const float content = (float) takes.size() * rowH;
    takesScroll = std::clamp (takesScroll, 0.0f, std::max (0.0f, content - r.h));
    p.pushClip (r);
    for (int i = 0; i < (int) takes.size(); ++i)
    {
        const Rect row { r.x, r.y + (float) i * rowH - takesScroll, r.w, rowH };
        if (row.bottom() < r.y || row.y > r.bottom()) continue;
        const auto pr = ui.behave (makeId ("band.take", (std::uint64_t) i), row);
        if (pr.clicked || pr.doubleClick) selectedTake = i;
        if (pr.doubleClick) addTake (i);
        if (selectedTake == i) p.fill (row, alpha (kBand, 0.3f));
        const auto& t = takes[(std::size_t) i];
        p.text ("TAKE " + std::to_string (takes.size() - (std::size_t) i), { row.x + 8, row.y + 2, row.w - 12, rowH * 0.5f }, 13.0f, true,
                brighter (kBand, 0.4f));
        const int secs = (int) std::lround (t.seconds);
        char b[48];
        std::snprintf (b, sizeof b, "  %d:%02d  ", secs / 60, secs % 60);
        p.text (t.title + b + std::to_string (t.stems.size()) + " tracks", { row.x + 8, row.y + rowH * 0.5f, row.w - 12, rowH * 0.5f - 2 }, 11.5f,
                false, colours::paper);
    }
    p.popClip();
    if (const float w = ui.wheel (r); w != 0.0f) takesScroll -= w * 60.0f;
    const bool any = ! takes.empty();
    takeTop (bottom, 6.0f);
    const Rect addB = takeTop (bottom, 34.0f);
    if (ui.button (makeId ("band.add"), addB, "ADD TO SESSION", ButtonLook::plate, kBand, any)) addTake (std::max (0, selectedTake));
    ui.tooltip (addB, "Put every instrument of the selected take into the session");
    takeTop (bottom, 4.0f);
    Rect pair = takeTop (bottom, 30.0f);
    const Rect sunoB = trimRight (takeLeft (pair, pair.w * 0.5f), 2.0f);
    if (ui.button (makeId ("band.suno"), sunoB, "FOR SUNO", ButtonLook::plate, kBand, any)) forSuno (std::max (0, selectedTake));
    ui.tooltip (sunoB, "Copies a Suno style for this take and shows its mix: upload the mix in Suno and paste the style");
    const Rect libB = trimLeft (pair, 2.0f);
    if (ui.button (makeId ("band.tolib"), libB, "TO LIBRARY", ButtonLook::plate, kBand, any) && onSendToLibrary)
    {
        const int sel = std::clamp (selectedTake, 0, (int) takes.size() - 1);
        const auto& t = takes[(std::size_t) sel];
        std::vector<fs::path> files;
        std::error_code ec;
        if (! t.mix.empty() && fs::exists (t.mix, ec)) files.push_back (t.mix);
        else files = t.stems;
        onSendToLibrary (files, t.genre, t.folder);
    }
    ui.tooltip (libB, "Put this take's mix (or its stems) in your LIBRARY");
    takeTop (bottom, 4.0f);
    if (ui.button (makeId ("band.folder"), takeTop (bottom, 30.0f), "OPEN FOLDER", ButtonLook::plate, kBand))
    {
        std::error_code ec;
        const int sel = selectedTake;
        if (sel >= 0 && sel < (int) takes.size() && fs::is_directory (takes[(std::size_t) sel].folder, ec)) platform::reveal (takes[(std::size_t) sel].folder);
        else
        {
            const fs::path f = App::songsFolder() / "Band Forge";
            fs::create_directories (f, ec);
            platform::reveal (f);
        }
    }
}
} // namespace hem::app
