#pragma once

#include "UI/AppContext.h"
#include "UI/WaveCache.h"

namespace pg::ui
{
/** View state shared by the track list (left panel) and the timeline (main panel). */
struct EditView
{
    double pxPerSecond = 40.0;
    double startSeconds = 0.0;
    int scrollY = 0;
    int rowHeight = 64;
    bool autoFit = true;
    std::function<void()> changed;
    void notify() { if (changed) changed(); }
};

/** One track header in the left panel: colour, name, mute / solo, plugin count. */
class TrackRow final : public juce::Component
{
public:
    TrackRow (AppContext&, int track);
    ~TrackRow() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    int getTrack() const noexcept { return track; }

private:
    void showMenu();
    AppContext& ctx;
    const int track;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
};

class TrackList final : public juce::Component
{
public:
    TrackList (AppContext&, EditView&);
    void rebuild();
    void layoutRows();
    void paint (juce::Graphics&) override;
    void resized() override { layoutRows(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static constexpr int kHeaderHeight = 40;

private:
    AppContext& ctx;
    EditView& view;
    juce::TextButton importButton { "+ IMPORT" };
    std::vector<std::unique_ptr<TrackRow>> rows;
};

/** EDIT tab main panel: ruler (bars + time, loop range), waveform lanes and the play-head. */
class Timeline final : public juce::Component, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    Timeline (AppContext&, EditView&);
    ~Timeline() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void fileDragExit (const juce::StringArray&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fitToWidth();
    void zoom (double factor, double aroundSeconds);
    static constexpr int kRulerHeight = 40;

private:
    void timerCallback() override;
    double xToSeconds (float x) const { return view.startSeconds + (double) x / view.pxPerSecond; }
    float secondsToX (double s) const { return (float) ((s - view.startSeconds) * view.pxPerSecond); }
    void updateScrollBar();
    juce::Array<int> usedTracks() const;

    AppContext& ctx;
    EditView& view;
    juce::ScrollBar hScroll { false };
    struct HListener : juce::ScrollBar::Listener
    {
        Timeline& t;
        explicit HListener (Timeline& tl) : t (tl) {}
        void scrollBarMoved (juce::ScrollBar*, double start) override { t.view.startSeconds = start; t.view.autoFit = false; t.view.notify(); }
    } hListener { *this };
    float lastPlayheadX = -1.0f;
    double dragStartSeconds = -1.0;
    bool draggingLoop = false, rulerClick = false;
    bool dropHover = false;
};
} // namespace pg::ui
