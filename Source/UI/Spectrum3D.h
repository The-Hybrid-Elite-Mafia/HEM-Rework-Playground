#pragma once

#include "Engine/PlaygroundEngine.h"
#include "Analysis/Waterfall.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace pg::ui
{
/** The 3D meter: a spectrum landscape of the master output, the same idea as The Hybrid Core's
    ANALYZER. Newest audio in front, about 2.7 seconds going back; low notes left, highs right;
    height and colour = level. Drag to turn it, double-click to put it back.

    Reads the engine's master tap only while it is on screen (UI thread); never touches DSP state. */
class Spectrum3D final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Spectrum3D (PlaygroundEngine&);
    ~Spectrum3D() override;

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override { visibilityChanged(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    const analysis::Waterfall& getWaterfall() const noexcept { return waterfall; }

    static constexpr float kDefaultYaw = -0.35f, kDefaultPitch = 0.55f;

private:
    void timerCallback() override;
    juce::Point<float> project (float x, float y, float z, juce::Rectangle<float> box) const noexcept;

    PlaygroundEngine& engine;
    analysis::Waterfall waterfall;
    std::vector<float> scratch = std::vector<float> ((size_t) PlaygroundEngine::kTapSize, 0.0f);
    float yaw = kDefaultYaw, pitch = kDefaultPitch, dragYaw = 0.0f, dragPitch = 0.0f;
    double preparedRate = 0.0;
    bool tapping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Spectrum3D)
};
} // namespace pg::ui
