#pragma once
// BAND FORGE inside the SONG FORGE tab: pick a genre, key, tempo, length and the instruments, press
// FORGE, and the band is written and played by the modeled (or recorded) instruments on a background
// thread. Every instrument comes out as its own stem (24-bit WAV + a MIDI file) in
// Documents/Hybrid Elite Mafia/Rework Playground/Band Forge/<date>/ and can go straight into the session.
#include "../App.hpp"
#include "BandComposer.hpp"
#include "BandRender.hpp"

namespace hem::app
{
class BandForgePanel
{
public:
    explicit BandForgePanel (App&);
    ~BandForgePanel();

    void draw (Ui&, Rect area);
    void drawTakes (Ui&, Rect panel);

    std::function<void (const std::vector<fs::path>&, const std::string& genre, const fs::path& takeFolder)> onSendToLibrary;

    struct Take
    {
        std::string title;
        fs::path folder;
        std::vector<fs::path> stems;
        double bpm = 0.0, seconds = 0.0;
        std::vector<std::string> recorded, modeled;
        std::string genre, keyText;
        fs::path mix;
        std::string mixInfo;
    };
    static std::string sunoStyle (const Take&);

private:
    class Job;
    void forge();
    void finished (bool ok, const std::string& error, Take take);
    void addTake (int index);
    void forSuno (int index);
    void genreChanged (bool resetInstruments);
    void saveInstrumentChoice();
    pg::band::Request makeRequest() const;

    App& app;
    int genre = 0, key = 7, mode = 0, length = 0, mixChoice = 2;
    float tempo = 0.0f;
    bool autoAdd = true;
    std::vector<pg::band::Instrument> shown;
    std::vector<bool> chosen;
    std::shared_ptr<const pg::band::SoundPack> soundPack;
    std::unique_ptr<Job> job;
    std::vector<Take> takes;
    int selectedTake = 0;
    std::string progressText;
    float takesScroll = 0.0f;
};
} // namespace hem::app
