#pragma once
// MASTER tab: master inserts, loudness (from Elite Master), the 3D meter, the master strip and
// WAV / MP3 export. Left panel: the loudness guide.
#include "../App.hpp"
#include "Spectrum3D.hpp"

namespace hem::pg { class EliteMaster; }

namespace hem::app
{
class MasterPage
{
public:
    explicit MasterPage (App&);
    ~MasterPage();

    void drawLeft (Ui&, Rect panel);
    void drawMain (Ui&, Rect panel);
    void hidden() { spectrum.setShowing (false); }

    /** EXPORT button / Ctrl+E: into the export folder under the FILE NAME, asks before replacing. */
    void startExport (bool mp3);
    /** File > Export WAV / MP3: a Save dialog picks the name and place. */
    void exportWithDialog (bool mp3);
    bool isExporting() const noexcept { return job != nullptr; }
    void resetFileName (const std::string& songName);
    void setSongName (const std::string& songName);

private:
    struct ExportJob;
    void runExport (const fs::path& target, bool mp3);
    void finished (bool ok, const std::string& err, float peak, float cleanGain, const fs::path& target, bool clean);
    pg::EliteMaster* findEliteMaster() const;
    fs::path exportFolder() const;

    App& app;
    Spectrum3D spectrum;
    std::unique_ptr<ExportJob> job;
    std::string fileName, lastAutoName, exportMessage;
    fs::path lastExport;
    double progress = 0.0;
    int finish = 0, bits = 1, rate = 1;
    float tail = 3.0f;
    bool beforeVersion = false;
};
} // namespace hem::app
