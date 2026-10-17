#pragma once

#include "UI/Pages/MixPage.h"
#include "Engine/Exporter.h"
#include "UI/Spectrum3D.h"

namespace pg::ui
{
/** MASTER tab: master inserts, loudness (from Elite Master), master fader, and WAV export. */
class MasterPage final : public juce::Component, private juce::Timer
{
public:
    explicit MasterPage (AppContext&);
    ~MasterPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void rebuild();
    /** EXPORT button / Ctrl+E: into the export folder under the FILE NAME, asks before replacing. */
    void startExport (bool mp3 = false);
    /** File > Export WAV / MP3: a Save dialog to pick the name and place. */
    void exportWithDialog (bool mp3);
    void runExport (const juce::File& target, bool mp3);
    /** New / opened session: the file name follows the song name again. */
    void resetFileName (const juce::String& songName);
    void visibilityChanged() override;
    bool isExporting() const noexcept { return job != nullptr; }
    void setSongName (const juce::String& n) { if (fileName.getText().isEmpty() || fileName.getText() == lastAutoName) fileName.setText (lastAutoName = n + " - master", false); }

    /** Left panel: loudness guide. */
    juce::Component& getGuidePanel() { return guide; }

private:
    struct ExportJob;
    struct Guide final : juce::Component { void paint (juce::Graphics&) override; };

    void timerCallback() override;
    EliteMaster* findEliteMaster() const;
    juce::File exportFolder() const;

    AppContext& ctx;
    std::vector<std::unique_ptr<InsertSlot>> slots;
    std::unique_ptr<ChannelStrip> masterStrip;
    juce::TextButton matchButton { "MATCH TARGET" }, resetButton { "RESET METERS" }, addMasterButton { "ADD ELITE MASTER" };
    juce::TextEditor fileName;
    juce::TextButton folderButton { "FOLDER..." }, exportButton { "EXPORT WAV" }, mp3Button { "EXPORT MP3" }, revealButton { "SHOW FILE" };
    juce::ComboBox finish, bits, rate;
    juce::Slider tail;
    juce::ToggleButton beforeVersion { "BEFORE VERSION (plugins off)" };
    juce::Rectangle<int> slotsArea, loudArea, exportArea, scopeArea;
    std::unique_ptr<ExportJob> job;
    double progress = 0.0;
    juce::String exportMessage, lastAutoName;
    juce::File lastExport;
    Guide guide;
    Spectrum3D spectrum { ctx.engine };   // the 3D meter (after ctx: it needs the engine)
};
} // namespace pg::ui
