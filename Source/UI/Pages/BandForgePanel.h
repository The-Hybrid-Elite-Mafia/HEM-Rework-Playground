#pragma once

#include "UI/AppContext.h"
#include "Band/BandComposer.h"
#include "Band/BandRender.h"

namespace pg::ui
{
/**
    BAND FORGE inside the SONG FORGE tab: pick a genre, key, tempo, length and the instruments, press
    FORGE, and the band is written and played by the modeled instruments on a background thread.
    Every instrument comes out as its own stem (24-bit WAV, plus a MIDI file) in
    Documents/Hybrid Elite Mafia/Rework Playground/Band Forge/<date>/ and can go straight into the session.
    Message thread only (the render runs on its own thread and reports back through callAsync).
*/
class BandForgePanel final : public juce::Component, private juce::Timer
{
public:
    explicit BandForgePanel (AppContext&);
    ~BandForgePanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Left panel: the band takes made so far (this run of the app). */
    juce::Component& getTakesPanel() { return takesPanel; }

    /** Set by the SONG FORGE page: TO LIBRARY hands a take's mix (or stems) to the LIBRARY. */
    std::function<void (const juce::Array<juce::File>&, const juce::String& genre, const juce::File& takeFolder)> onSendToLibrary;

    void forge();
    void cancel();
    void addTake (int index);
    /** Copies a Suno style for the take to the clipboard and shows its mix (or folder). */
    void forSuno (int index);
    bool isBusy() const noexcept { return job != nullptr; }

    struct Take
    {
        juce::String title;            // "Outlaw Country - G major - 96 BPM"
        juce::File folder;
        juce::Array<juce::File> stems;
        double bpm = 0.0, seconds = 0.0;
        juce::StringArray recorded, modeled;   // instrument names by how they were played
        juce::String genre, keyText;           // "Outlaw Country", "G major"
        juce::File mix;                        // full mix WAV, if one was asked for
        juce::String mixInfo;
    };

    /** "outlaw country instrumental, 96 BPM, G major, fiddle, ..." */
    static juce::String sunoStyle (const Take&);

private:
    class Job;
    class TakesPanel final : public juce::Component, private juce::ListBoxModel
    {
    public:
        explicit TakesPanel (BandForgePanel&);
        ~TakesPanel() override { list.setModel (nullptr); }
        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh();
        int selected() const { return list.getSelectedRow(); }
    private:
        int getNumRows() override { return owner.takes.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { owner.addTake (row); }
        BandForgePanel& owner;
        juce::ListBox list;
        juce::TextButton addButton { "ADD TO SESSION" }, folderButton { "OPEN FOLDER" }, sunoButton { "FOR SUNO" }, libraryButton { "TO LIBRARY" };
    };

    void timerCallback() override;
    void genreChanged (bool resetInstruments);
    band::Request makeRequest() const;
    void finished (bool ok, const juce::String& error, Take take);
    void saveInstrumentChoice();

    AppContext& ctx;
    juce::ComboBox genre, key, mode, length;
    juce::Slider tempo;
    juce::OwnedArray<juce::ToggleButton> instrumentBoxes;
    std::vector<band::Instrument> shownInstruments;
    juce::ComboBox mixChoice;
    juce::ToggleButton autoAdd { "PUT IT IN THE SESSION WHEN DONE" };
    juce::TextButton forgeButton { "FORGE" }, cancelButton { "CANCEL" };
    juce::Label progressLabel;
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::Rectangle<int> instrumentsCaption, optionsArea, headerArea;

    std::shared_ptr<const band::SoundPack> soundPack;
    std::unique_ptr<Job> job;
    juce::Array<Take> takes;
    TakesPanel takesPanel { *this };
};
} // namespace pg::ui
