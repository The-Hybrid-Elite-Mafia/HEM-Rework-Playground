#pragma once
// SONG FORGE tab, three modes:
//   BAND FORGE  an instrumental band written and played by our own code (BandForgePanel)
//   LIBRARY     Steven's private shelf of his own beats and songs (LibraryPanel)
//   ACE-STEP    style + lyrics -> ACE-Step 1.5 running on this computer makes the song; the takes go
//               into the session. Shares ACE-Step's folder / address with Hybrid Studio (SongForge.settings).
#include "../App.hpp"
#include "AceStepClient.hpp"

namespace hem::app
{
class BandForgePanel;
class LibraryPanel;

class ForgePage
{
public:
    explicit ForgePage (App&);
    ~ForgePage();

    enum class Mode { band = 0, ace, library };
    void setMode (Mode);
    Mode getMode() const noexcept { return mode; }
    LibraryPanel& library() noexcept { return *libraryPanel; }

    void drawLeft (Ui&, Rect panel);
    void drawMain (Ui&, Rect panel);

private:
    void drawAce (Ui&, Rect panel);
    void drawTakes (Ui&, Rect panel);
    void generate();
    void showSetup();
    void startEngine();
    void addTake (int index);
    void reloadLastTakes();
    void finished (pg::ace::Result);
    std::string server() const;
    std::string apiKey() const;
    fs::path aceFolder() const;

    App& app;
    PropertiesFile aceSettings;
    pg::ace::HealthWatch health;
    std::unique_ptr<pg::ace::Job> job;
    std::vector<pg::ace::Take> takes;
    fs::path takesFolder;
    int selectedTake = 0;
    double engineStartedAt = -1.0;
    std::string style, lyrics, progressText;
    float seconds = 180.0f, bpm = 0.0f, takeCount = 2.0f;
    int key = 0, timeSig = 0, language = 0;
    bool smart = false, instrumental = false;
    Mode mode = Mode::band;
    std::unique_ptr<BandForgePanel> band;
    std::unique_ptr<LibraryPanel> libraryPanel;
};
} // namespace hem::app
