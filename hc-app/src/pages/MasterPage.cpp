#include "MasterPage.hpp"
#include "Strips.hpp"
#include "../platform/Platform.hpp"

#include "EliteMaster.hpp"
#include "export/Exporter.hpp"
#include "export/Mp3Encoder.hpp"
#include "hem/pg/Files.hpp"

#include <cmath>
#include <cstdio>
#include <thread>

namespace hem::app
{
namespace
{
    const Rgba kGold = argb (0xffcfa63a);
    const std::vector<std::string> kFinish { "MASTERED  -  loud, master chain on", "CLEAN  -  master chain off, peaks at -3 dB" };
    const std::vector<std::string> kBits { "16-bit (CD / streaming upload)", "24-bit (best for mastering)", "32-bit float" };
    const std::vector<std::string> kRates { "44.1 kHz", "48 kHz", "96 kHz" };

    std::string fmt1 (double v)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.1f", v);
        return b;
    }
}

//==============================================================================
struct MasterPage::ExportJob
{
    using Done = std::function<void (bool, std::string, float, float)>;
    ExportJob (std::unique_ptr<pg::OfflineExport> e, Done d) : exporter (std::move (e)), done (std::move (d))
    {
        thread = std::thread ([this]
        {
            std::string err;
            const bool ok = exporter->render ([this] (double p) { progress.store (p); }, [this] { return cancel.load(); }, err);
            const float peak = exporter->getPeak(), gain = exporter->getCleanGain();
            auto cb = done;
            auto flag = alive;
            Worker::toUi ([cb, flag, ok, err, peak, gain] { if (flag->load() && cb) cb (ok, err, peak, gain); });
        });
    }
    ~ExportJob()
    {
        alive->store (false);
        cancel.store (true);
        if (thread.joinable()) thread.join();
    }

    std::unique_ptr<pg::OfflineExport> exporter;
    Done done;
    std::atomic<double> progress { 0.0 };
    std::atomic<bool> cancel { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::thread thread;
};

//==============================================================================
MasterPage::MasterPage (App& a) : app (a), spectrum (a.engine)
{
    fileName = app.settings.get ("exportName", "My Song - master");
    finish = std::clamp (app.settings.getInt ("exportFinish", 0), 0, 1);
    bits = std::clamp (app.settings.getInt ("exportBits", 1), 0, 2);
    rate = std::clamp (app.settings.getInt ("exportRate", 1), 0, 2);
    tail = (float) std::clamp (app.settings.getDouble ("exportTail", 3.0), 0.0, 10.0);
}

MasterPage::~MasterPage() { job.reset(); }

fs::path MasterPage::exportFolder() const
{
    const fs::path f = pg::files::toPath (app.settings.get ("exportFolder"));
    std::error_code ec;
    return ! f.empty() && fs::is_directory (f, ec) ? f : App::songsFolder() / "Exports";
}

pg::EliteMaster* MasterPage::findEliteMaster() const
{
    for (int s = E::kSlots - 1; s >= 0; --s)
        if (auto* m = dynamic_cast<pg::EliteMaster*> (app.engine.getPlugin (E::kMaster, s))) return m;
    return nullptr;
}

void MasterPage::resetFileName (const std::string& songName)
{
    fileName = lastAutoName = songName + " - master";
    lastExport.clear();
    exportMessage.clear();
    progress = 0.0;
}

void MasterPage::setSongName (const std::string& songName)
{
    if (fileName.empty() || fileName == lastAutoName) fileName = lastAutoName = songName + " - master";
}

//==============================================================================
void MasterPage::drawLeft (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    Rect r = reduced (panel, 12.0f, 8.0f);
    p.panelText ("LOUDNESS GUIDE", takeTop (r, 32.0f), 15.0f);
    const std::pair<const char*, const char*> rows[] { { "-14 LUFS", "Spotify, YouTube, TikTok" }, { "-16 LUFS", "Apple Music" },
                                                       { "-9 LUFS", "Loud trap / rap" }, { "-7 LUFS", "Club / DJ" }, { "-1 dBTP", "Ceiling for streaming" } };
    for (auto& [a, b] : rows)
    {
        Rect row = takeTop (r, 44.0f);
        p.panelText (a, takeTop (row, 22.0f), 15.0f, HAlign::left, VAlign::middle, brighter (kGold, 0.4f));
        p.panelText (b, row, 12.0f, HAlign::left, VAlign::top, colours::paperDim);
    }
    takeTop (r, 10.0f);
    p.wrapped ("Play the loudest part, press MATCH TARGET, then export. Use BEFORE / AFTER (transport) to check the master really sounds "
               "better, not just louder.", r, 12.0f, false, colours::paperDim, 8);
}

void MasterPage::drawMain (Ui& ui, Rect panel)
{
    auto& p = ui.p;
    if (job != nullptr) progress = job->progress.load();

    Rect r = reduced (panel, 12.0f, 10.0f);
    const Rect stripR = takeLeft (r, kStripWidth + 10.0f);
    takeLeft (r, 14.0f);
    const Rect exportArea = takeRight (r, std::min (440.0f, r.w * 0.5f));
    takeRight (r, 14.0f);
    const Rect slotsArea = takeTop (r, std::min (r.h * 0.5f, 8.0f * 30.0f + 36.0f));
    takeTop (r, 12.0f);
    Rect loudArea = r;
    Rect buttons = takeBottom (loudArea, 40.0f);
    buttons.w = std::min (r.w, 420.0f);
    takeBottom (loudArea, 8.0f);
    const Rect scopeArea = takeRight (loudArea, std::max (loudArea.w * 3.0f / 5.0f, loudArea.w - 300.0f));
    takeRight (loudArea, 12.0f);

    auto box = [&p] (Rect b) { p.fillRound (b, 6.0f, alpha (colours::panel, 0.7f)); };
    box (expanded (slotsArea, 8, 4));
    {
        Rect u = expanded (unite (loudArea, scopeArea), 8, 4);
        u.h = panel.bottom() - 8.0f - u.y;
        box (u);
    }
    box (exportArea);

    drawChannelStrip (app, ui, stripR, E::kMaster);

    // master inserts
    p.panelText ("MASTER INSERTS", withHeight (slotsArea, 26.0f), 15.0f, HAlign::left, VAlign::middle, brighter (kGold, 0.4f));
    {
        Rect s = trimTop (slotsArea, 30.0f);
        const float h = s.h / (float) E::kSlots;
        for (int i = 0; i < E::kSlots; ++i) drawInsertSlot (app, ui, reduced (takeTop (s, h), 0.0f, 2.0f), E::kMaster, i);
    }

    // loudness
    Rect lr = loudArea;
    if (auto* m = findEliteMaster())
    {
        p.panelText ("LOUDNESS  (Elite Master)", takeTop (lr, 26.0f), 15.0f, HAlign::left, VAlign::middle, brighter (kGold, 0.4f));
        const int target = std::clamp ((int) std::lround (m->getParameter ("target")), 0, 3);
        auto val = [] (float v, const char* unit) { return v <= -69.0f ? std::string ("--") : fmt1 (v) + " " + unit; };
        const std::pair<std::string, std::string> rows[] {
            { "INTEGRATED", val (m->getReadout (pg::EliteMaster::integrated), "LUFS") },
            { "SHORT-TERM", val (m->getReadout (pg::EliteMaster::shortTerm), "LUFS") },
            { "TRUE PEAK", val (m->getReadout (pg::EliteMaster::truePeak), "dBTP") },
            { "LIMITING", fmt1 (m->getReadout (pg::EliteMaster::gr)) + " dB" },
            { "TARGET", pg::EliteMaster::targets()[(std::size_t) target] } };
        const float rh = std::min (40.0f, lr.h / 5.0f);
        for (int i = 0; i < 5; ++i)
        {
            Rect row = takeTop (lr, rh);
            p.panelText (rows[i].first, takeLeft (row, row.w * 0.45f), 13.0f, HAlign::left, VAlign::middle, colours::paperDim);
            p.text (rows[i].second, row, i == 0 ? 22.0f : 17.0f, true, i == 0 ? brighter (kGold, 0.5f) : colours::paper, HAlign::left,
                    VAlign::middle, i == 0 ? TextLook::neon : TextLook::plain);
        }
        const Rect mb = reduced (takeLeft (buttons, buttons.w * 0.5f), 2.0f);
        if (ui.button (makeId ("master.match"), mb, "MATCH TARGET"))
        {
            if (m->matchTarget()) app.status ("MASTER: input set to hit " + pg::EliteMaster::targets()[(std::size_t) target]);
            else app.status ("MASTER: play the song for a while first, so Elite Master can measure it");
        }
        ui.tooltip (mb, "Sets Elite Master's INPUT so the song lands on the chosen loudness target (play the loudest part first)");
        if (ui.button (makeId ("master.reset"), reduced (buttons, 2.0f), "RESET METERS")) m->resetMeters();
    }
    else
    {
        p.panelText ("No Elite Master on the master yet - add it for the final limiter and loudness meter.", trimBottom (lr, lr.h * 0.55f), 13.0f,
                     HAlign::centre, VAlign::bottom, colours::paperDim);
        if (ui.button (makeId ("master.add"), centred (loudArea, std::min (260.0f, loudArea.w), 44.0f), "ADD ELITE MASTER"))
        {
            app.insertPlugin (E::kMaster, E::kSlots - 1, "elite-master");
            app.openPlugin (E::kMaster, E::kSlots - 1);
        }
    }

    spectrum.draw (ui, scopeArea);

    // export
    Rect e = reduced (exportArea, 14.0f, 12.0f);
    p.panelText ("EXPORT WAV", takeTop (e, 30.0f), 17.0f, HAlign::left, VAlign::middle, brighter (kGold, 0.4f));
    p.panelText ("FILE NAME", takeTop (e, 18.0f), 11.5f, HAlign::left, VAlign::middle, colours::paperDim);
    Rect bottom = takeBottom (e, 44.0f + 8.0f + 12.0f + 44.0f);
    Rect b = takeTop (bottom, 44.0f);
    const Rect revealB = reduced (takeRight (b, 110.0f), 2.0f);
    const Rect mp3B = reduced (takeRight (b, b.w * 0.5f), 2.0f);
    const Rect exportB = reduced (b, 2.0f);
    const float gap = std::clamp (std::floor ((e.h - 212.0f) / 9.0f), 2.0f, 14.0f);
    auto row = [&e, gap] (float h) { const Rect x = takeTop (e, h); takeTop (e, gap); return x; };

    FieldStyle fs;
    fs.accent = kGold;
    fs.fontHeightL = 15.0f;
    if (ui.textField (makeId ("master.fileName"), row (32.0f), fileName, fs)) app.settings.set ("exportName", fileName);
    takeTop (e, gap);
    Rect folderRow = row (32.0f);
    const Rect folderB = takeLeft (folderRow, 130.0f);
    if (ui.button (makeId ("master.folder"), folderB, "FOLDER..."))
        if (auto f = platform::pickFolder (app.hwnd(), "Export folder", exportFolder())) app.settings.set ("exportFolder", pg::files::toText (*f));
    p.panelText ("SAVES TO:  " + pg::files::toText (exportFolder()), trimLeft (folderRow, 10.0f), 11.0f, HAlign::left, VAlign::middle, colours::paperDim);
    takeTop (e, gap);
    if (ui.combo (makeId ("master.finish"), row (30.0f), kFinish, finish, kGold)) app.settings.set ("exportFinish", finish);
    if (ui.combo (makeId ("master.bits"), row (30.0f), kBits, bits, kGold)) app.settings.set ("exportBits", bits);
    if (ui.combo (makeId ("master.rate"), row (30.0f), kRates, rate, kGold)) app.settings.set ("exportRate", rate);
    takeTop (e, gap);
    {
        Rect tr = row (28.0f);
        const Rect txt = takeRight (tr, 64.0f);
        float n = tail / 10.0f;
        if (ui.hslider (makeId ("master.tail"), tr, n, kGold))
        {
            tail = std::round (n * 20.0f) * 0.5f;
            app.settings.set ("exportTail", (double) tail);
        }
        p.text (fmt1 (tail) + " s", txt, 13.0f, true, colours::paper, HAlign::right);
        ui.tooltip (tr, "Extra time after the last track ends, so reverb and delay can ring out");
    }
    {
        const Rect br = row (30.0f);
        ui.ledToggle (makeId ("master.before"), br, beforeVersion, "BEFORE VERSION (plugins off)", colours::amber);
        ui.tooltip (br, "Export the BEFORE version: every plugin bypassed (to compare)");
    }

    if (ui.button (makeId ("master.export"), exportB, job != nullptr ? "CANCEL" : "EXPORT WAV", ButtonLook::plate, kGold, true, job != nullptr))
    {
        if (job != nullptr) job->cancel.store (true);
        else startExport (false);
    }
    if (ui.button (makeId ("master.mp3"), mp3B, "EXPORT MP3", ButtonLook::plate, kGold, job == nullptr)) startExport (true);
    ui.tooltip (mp3B, "MP3, 320 kbps (made by the MP3 encoder built into Windows). Uses 44.1 kHz if picked, otherwise 48 kHz.");
    std::error_code ec;
    const bool canReveal = ! lastExport.empty() && fs::exists (lastExport, ec);
    if (ui.button (makeId ("master.reveal"), revealB, "SHOW FILE", ButtonLook::plate, kGold, canReveal)) platform::reveal (lastExport);

    const Rect pb { exportB.x, exportB.bottom() + 8.0f, revealB.right() - exportB.x, 12.0f };
    ui.progress (pb, job != nullptr || progress > 0.0 ? (float) progress : 0.0f, kGold);
    p.wrapped (exportMessage, { pb.x, pb.bottom() + 4.0f, pb.w, 44.0f }, 12.5f, false, colours::paper, 3);
}

//==============================================================================
void MasterPage::startExport (bool mp3)
{
    if (job != nullptr) return;
    if (app.engine.getNumUsedTracks() == 0) { exportMessage = "Nothing to export yet: load your stems first."; return; }
    std::string name = pg::files::legalFileName (fileName);
    while (! name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) name = "Rework Playground mix";
    app.settings.set ("exportName", fileName);
    if (app.ui().fields()) app.ui().fields()->unfocus();   // SPACE is play / stop again

    const fs::path target = exportFolder() / pg::files::toPath (name + (mp3 ? ".mp3" : ".wav"));
    std::error_code ec;
    if (! fs::exists (target, ec)) { runExport (target, mp3); return; }
    // never overwrite an earlier export without asking
    const fs::path numbered = pg::files::nonexistentSibling (target);
    DialogSpec d;
    d.title = "File already exists";
    d.message = pg::files::toText (target.filename()) + " is already in the export folder.";
    d.buttons = { "SAVE AS \"" + pg::files::toText (numbered.stem()) + "\"", "REPLACE IT", "CANCEL" };
    d.accent = kGold;
    showDialog (app.ui(), d, [this, target, numbered, mp3] (int b, const std::vector<std::string>&)
    {
        if (b == 0) runExport (numbered, mp3);
        else if (b == 1) runExport (target, mp3);
    });
}

void MasterPage::exportWithDialog (bool mp3)
{
    if (job != nullptr) return;
    if (app.engine.getNumUsedTracks() == 0) { exportMessage = "Nothing to export yet: load your stems first."; return; }
    std::string name = pg::files::legalFileName (fileName);
    if (name.empty()) name = "Rework Playground mix";
    const std::string ext = mp3 ? "mp3" : "wav";
    auto f = platform::saveFile (app.hwnd(), mp3 ? "Export MP3" : "Export WAV", exportFolder() / pg::files::toPath (name + "." + ext),
                                 { { mp3 ? "MP3 files" : "WAV files", "*." + ext } }, ext);
    if (! f) return;
    fs::path target = *f;
    target.replace_extension ("." + ext);
    fileName = pg::files::toText (target.stem());
    app.settings.set ("exportName", fileName);
    runExport (target, mp3);
}

void MasterPage::runExport (const fs::path& target, bool mp3)
{
    if (job != nullptr) return;
    pg::ExportOptions o;
    o.file = target;
    o.bitDepth = bits == 0 ? 16 : (bits == 2 ? 32 : 24);
    o.sampleRate = rate == 0 ? 44100.0 : (rate == 2 ? 96000.0 : 48000.0);
    if (mp3)
    {
        o.mp3Kbps = pg::Mp3Encoder::kDefaultKbps;
        if (o.sampleRate > 48000.0) o.sampleRate = 48000.0;   // MP3 has no 96 kHz
    }
    o.tailSeconds = tail;
    o.beforeMode = beforeVersion;
    o.clean = finish == 1;

    const auto session = app.engine.createSessionTree();
    std::map<std::string, pg::SampleData::Ptr> media;
    for (int t = 0; t < E::kMaxTracks; ++t)
        if (auto m = app.engine.getTrackMedia (t))
            media[pg::files::toText (m->getFile())] = m;   // the decoded audio is reused: no second load from disk

    auto exporter = std::make_unique<pg::OfflineExport> (session, media, o);
    const bool clean = o.clean;
    job = std::make_unique<ExportJob> (std::move (exporter), [this, target, clean] (bool ok, std::string err, float peak, float gain)
    {
        finished (ok, err, peak, gain, target, clean);
    });
    char what[160];
    if (mp3) std::snprintf (what, sizeof what, " (MP3 %d kbps, %.1f kHz)...", o.mp3Kbps, o.sampleRate / 1000.0);
    else std::snprintf (what, sizeof what, " (%d-bit, %.1f kHz)...", o.bitDepth, o.sampleRate / 1000.0);
    exportMessage = "Exporting " + pg::files::toText (target.filename()) + (clean ? " CLEAN" : " MASTERED") + what;
    progress = 0.0;
}

void MasterPage::finished (bool ok, const std::string& err, float peak, float cleanGain, const fs::path& target, bool clean)
{
    job.reset();
    progress = ok ? 1.0 : 0.0;
    if (ok)
    {
        lastExport = target;
        exportMessage = "Saved " + pg::files::toText (target.filename()) + (clean ? " (CLEAN)" : " (MASTERED)") + ".  Peak "
                        + fmt1 (pg::params::gainToDb (peak)) + " dBFS"
                        + (clean && cleanGain < 0.999f ? ", turned down " + fmt1 (-pg::params::gainToDb (cleanGain)) + " dB to keep headroom" : std::string())
                        + (peak > 1.0f ? "  -  WARNING: it clips. Add Elite Master or lower the master fader." : "");
    }
    else
        exportMessage = err;
    app.status ("EXPORT: " + exportMessage);
}
} // namespace hem::app
