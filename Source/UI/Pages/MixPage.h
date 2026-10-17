#pragma once

#include "UI/AppContext.h"
#include "UI/Widgets.h"

namespace pg::ui
{
/** One insert slot of a strip: empty (+), or the plugin's short name on its accent colour. */
class InsertSlot final : public juce::Component, public juce::SettableTooltipClient
{
public:
    InsertSlot (AppContext& c, int strip, int slot) : ctx (c), stripIndex (strip), slotIndex (slot) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    void showSlotMenu();
    AppContext& ctx;
    int stripIndex, slotIndex;
};

/** A mixer strip: inserts, sends, output, pan, fader + meter, mute / solo. */
class ChannelStrip final : public juce::Component
{
public:
    ChannelStrip (AppContext&, int strip);
    ~ChannelStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void updateMeter();
    int getStrip() const noexcept { return strip; }

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    AppContext& ctx;
    const int strip;
    juce::Label name;
    std::vector<std::unique_ptr<InsertSlot>> slots;
    juce::ComboBox sendABus, sendBBus, output;
    juce::Slider sendA, sendB, pan, fader;
    juce::TextButton mute { "M" }, solo { "S" };
    juce::Label faderValue;
    StripMeter meter;
    std::vector<std::unique_ptr<SA>> sliderAtts;
    std::vector<std::unique_ptr<BA>> buttonAtts;
    std::vector<std::unique_ptr<CA>> comboAtts;
};

/** MIX tab: every used track, the 6 buses, and the master pinned on the right. */
class MixPage final : public juce::Component, private juce::Timer
{
public:
    explicit MixPage (AppContext&);
    ~MixPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void rebuild();

    static constexpr int kStripWidth = 104;

private:
    void timerCallback() override;
    AppContext& ctx;
    juce::Viewport viewport;
    juce::Component strips;
    std::vector<std::unique_ptr<ChannelStrip>> trackStrips;
    std::unique_ptr<ChannelStrip> masterStrip;
};

/** Left panel on the MIX tab: the plugin library. Click a plugin to put it on the selected strip. */
class PluginLibrary final : public juce::Component
{
public:
    explicit PluginLibrary (AppContext&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    int rowAt (juce::Point<int>) const;
    juce::Rectangle<int> rowBounds (int i) const;
    AppContext& ctx;
    int hover = -1;
};
} // namespace pg::ui
