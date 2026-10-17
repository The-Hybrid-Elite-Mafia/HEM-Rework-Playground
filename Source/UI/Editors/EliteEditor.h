#pragma once

#include "Plugins/PluginRegistry.h"
#include "UI/PlaygroundLookAndFeel.h"

namespace pg::ui
{
/** What an Elite plugin window shows. Everything binds to the plugin's APVTS. */
struct EditorSpec
{
    struct Knob  { juce::String id, label; bool big = false; };
    struct Meter
    {
        enum Kind { inputPeak, outputPeak, reduction, level, value };
        juce::String label; Kind kind; int readout = -1; float range = 24.0f;   // dB shown full scale
        juce::String unit = "dB";
    };

    juce::String tabsParam;                    // a choice parameter shown in the 4 tab slots (optional)
    std::vector<std::vector<Knob>> knobRows;   // rows of knobs in the grey panel
    std::vector<std::pair<juce::String, juce::String>> toggles;   // (param id, label) LED switches in the panel
    std::vector<std::pair<juce::String, juce::String>> choices;   // (param id, label) drop-downs in the panel
    std::vector<Meter> meters;                 // bars behind the glass
    int width = 980, height = 640;

    /** The layout for a plugin (hand-made for each Elite plugin, automatic for anything else). */
    static EditorSpec forPlugin (ElitePlugin&);
};

/**
    Plugin window built from the main faceplate's pieces:

        [ lightning frame ]
        [ title strip:  (logo)  ELITE VOCALIS  blurb ............ (o) BYPASS ]
        [ chrome row:   [plate: preset v  < >  SAVE]   [glass: meters]    ]
        [ chrome row:   [ tab ] [ tab ] [ tab ] [ tab ]   (optional)       ]
        [ grey panel:   knobs in chrome sockets, value glass under each    ]
        [ chrome strip: HYBRID ELITE MAFIA - REWORK PLAYGROUND             ]

    The static pieces are rendered once per size into a cached image; a 30 Hz timer repaints only
    the meters and value readouts. Message thread only.
*/
class EliteEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EliteEditor (ElitePlugin&);
    ~EliteEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Registers createEditor for every Elite plugin. Call once at app start. */
    static void install();

protected:
    /** Plugin-specific display drawn inside `area` of the panel (EQ curve etc). Default: none. */
    virtual void paintDisplay (juce::Graphics&, juce::Rectangle<float>) {}
    virtual int displayHeight() const { return 0; }
    /** Called by the 30 Hz UI timer (pull display data here). */
    virtual void tick() {}
    juce::Rectangle<float> getDisplayArea() const noexcept { return displayArea; }

    ElitePlugin& plugin;
    EditorSpec spec;
    PlaygroundLookAndFeel lnf;

private:
    struct KnobUi
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        juce::RangedAudioParameter* param = nullptr;
        juce::String label;
        juce::Rectangle<float> labelArea, valueArea;
        juce::String shown;
    };
    struct ToggleUi
    {
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct ChoiceUi
    {
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
        juce::String label;
        juce::Rectangle<float> labelArea;
    };

    void timerCallback() override;
    void renderBackground();
    void paintMeters (juce::Graphics&);
    void rebuildPresetMenu();
    void presetChosen();
    void stepPreset (int delta);
    void savePresetAs();
    /** Double-click on a knob returns it to the value of the preset that is loaded (not the factory default). */
    void armDoubleClickToCurrent();
    float u() const noexcept { return (float) getWidth() / (float) spec.width; }

    juce::Image background;
    juce::Image logo;
    juce::Rectangle<float> titleArea, presetPlate, statusArea, glassArea, panelArea, bottomArea, displayArea, logoArea, nameArea;
    std::vector<juce::Rectangle<float>> chromeRows, gaps;
    float pieceScale = 0.3f;

    std::vector<KnobUi> knobs;
    std::vector<ToggleUi> toggles;
    std::vector<ChoiceUi> choices;
    std::vector<std::unique_ptr<juce::TextButton>> tabs;
    std::unique_ptr<juce::ParameterAttachment> tabAtt;
    juce::ToggleButton bypass { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    juce::String currentPreset { "Default" };
    juce::StringArray menuNames;   // index = combo item id - 1 for presets

    std::vector<float> meterShown;
    std::vector<float> meterHold;
};
} // namespace pg::ui
