#pragma once

#include "UI/Pages/MixPage.h"
#include "Engine/Exporter.h"

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
    void startExport();
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
    juce::TextButton folderButton { "FOLDER..." }, exportButton { "EXPORT WAV" }, revealButton { "SHOW FILE" };
    juce::ComboBox finish, bits, rate;
    juce::Slider tail;
    juce::ToggleButton beforeVersion { "BEFORE VERSION (plugins off)" };
    juce::Rectangle<int> slotsArea, loudArea, exportArea;
    std::unique_ptr<ExportJob> job;
    double progress = 0.0;
    juce::String exportMessage, lastAutoName;
    juce::File lastExport;
    Guide guide;
};
} // namespace pg::ui
