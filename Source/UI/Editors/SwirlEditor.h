#pragma once

#include "EliteEditor.h"

namespace pg::ui
{
/**
    Elite Swirl Killer window: the standard Elite layout plus a 3D time / frequency / level view.
      X - time (the last ~1.5 s, newest at the front)
      Y - frequency (40 Hz .. 20 kHz, log)
      Z - level, stereo width, or left / right position (click "HEIGHT")
      colour - how much swirl was found (blue = clean .. red = swirl being removed)
    Drag to turn it, double-click to reset the angle. Message thread only: frames come from the
    plugin's lock-free FIFO in tick().
*/
class SwirlEditor final : public EliteEditor
{
public:
    explicit SwirlEditor (EliteSwirl&);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

protected:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    int displayHeight() const override { return 380; }
    void tick() override;

private:
    static constexpr int kHistory = 64;
    static juce::Colour heat (float v);
    juce::Rectangle<float> heightButton() const;
    juce::Rectangle<float> cameraButton (int i) const;
    void setCamera (int i);

    EliteSwirl& swirl;
    std::vector<EliteSwirl::Frame> history = std::vector<EliteSwirl::Frame> ((size_t) kHistory);
    int newest = 0;
    std::array<EliteSwirl::Frame, 64> incoming;
    float yaw = -0.62f, pitch = 0.62f;
    float dragYaw = 0.0f, dragPitch = 0.0f;
    int heightMode = 0;   // 0 level, 1 stereo width, 2 left / right
    int camera = 0;       // 0 3D, 1 top, 2 side, 3 front (vocal axis); -1 = turned by hand
    bool dragging = false;
    // per-paint scratch (message thread)
    struct Quad { float depth; int t, b; };
    std::vector<Quad> quads;
    std::vector<juce::Point<float>> projected;
    std::vector<float> depthOf;
};
} // namespace pg::ui
