#pragma once

#include "UI/AppContext.h"
#include "SongForge/AceStepClient.h"
#include "UI/Pages/BandForgePanel.h"
#include "UI/Pages/LibraryPanel.h"

namespace pg::ui
{
/**
    SONG FORGE tab, three modes (buttons at the top):
    BAND FORGE: an instrumental band written and played by our own code (BandForgePanel).
    LIBRARY: Steven's private shelf of his own beats and songs (LibraryPanel).
    ACE-STEP: write a style + lyrics, ACE-Step 1.5 (running on this computer) makes the song,
    the takes go into the session as tracks. Shares ACE-Step's folder / address with Hybrid Studio
    (Documents settings file "SongForge.settings"), so a PC that already runs Song Forge there needs
    no new setup. Message thread only.
*/
class ForgePage final : public juce::Component, private juce::Timer
{
public:
    explicit ForgePage (AppContext&);
    ~ForgePage() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** Left panel: the takes of whichever mode is showing. */
    juce::Component& getTakesPanel() { return leftHolder; }

    enum class Mode { band = 0, ace, library };
    void setMode (Mode);
    Mode getMode() const noexcept { return mode; }
    void setBandMode (bool b) { setMode (b ? Mode::band : Mode::ace); }
    bool isBandMode() const noexcept { return mode == Mode::band; }
    BandForgePanel& getBandPanel() noexcept { return band; }
    LibraryPanel& getLibraryPanel() noexcept { return library; }

    void generate();
    void cancel();
    void showSetup();
    void startEngine();
    void addTake (int index);

private:
    class TakesPanel final : public juce::Component, private juce::ListBoxModel
    {
    public:
        explicit TakesPanel (ForgePage&);
        ~TakesPanel() override { list.setModel (nullptr); }
        void resized() override;
        void paint (juce::Graphics&) override;
        void refresh() { list.updateContent(); list.repaint(); if (owner.takes.size() > 0 && list.getSelectedRow() < 0) list.selectRow (0); }
        int selected() const { return list.getSelectedRow(); }
    private:
        int getNumRows() override { return owner.takes.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { owner.addTake (row); }
        ForgePage& owner;
        juce::ListBox list;
        juce::TextButton addButton { "ADD TO SESSION" }, allButton { "ADD ALL" }, folderButton { "OPEN FOLDER" };
    };

    void timerCallback() override;
    void finished (ace::Result);
    void saveTexts();
    /** After a restart: show the takes of the newest Song Forge folder again (their files are still on disk). */
    void reloadLastTakes();
    juce::String server() const;
    juce::String apiKey() const;
    juce::File aceFolder() const;
    ace::Request makeRequest() const;

    AppContext& ctx;
    std::unique_ptr<juce::PropertiesFile> aceSettings;   // shared with Hybrid Studio

    juce::Label engineLabel, progressLabel;
    juce::TextButton startButton { "START ENGINE" }, setupButton { "SET UP" }, generateButton { "GENERATE" }, cancelButton { "CANCEL" };
    juce::TextEditor style, lyrics;
    juce::Slider seconds, bpm, takeCount;
    juce::ComboBox key, timeSig, language;
    juce::ToggleButton smart { "SMART" }, instrumental { "INSTRUMENTAL" };

    ace::HealthWatch health;
    std::unique_ptr<ace::Job> job;
    juce::Array<ace::Take> takes;
    juce::File takesFolder;
    bool engineUp = false;
    juce::uint32 engineStartedAt = 0;
    juce::Rectangle<int> styleCaption, lyricsCaption, optionsArea;
    TakesPanel takesPanel { *this };

    struct LeftHolder final : juce::Component
    {
        void resized() override { for (auto* c : getChildren()) c->setBounds (getLocalBounds()); }
    } leftHolder;
    BandForgePanel band { ctx };
    LibraryPanel library { ctx };
    juce::TextButton bandTab { "BAND FORGE" }, aceTab { "SONG FORGE  (ACE-STEP)" }, libraryTab { "LIBRARY" };
    Mode mode = Mode::band;
    std::vector<juce::Component*> aceControls();
};
} // namespace pg::ui
