#pragma once
// EDIT tab: the track list (left panel) and the timeline with waveforms, ruler, loop and play-head.
#include "../App.hpp"

namespace hem::app
{
class EditPage
{
public:
    explicit EditPage (App& a) : app (a) {}

    void drawLeft (Ui&, Rect panel);
    void drawMain (Ui&, Rect panel);
    void fitToWidth();
    void zoom (double factor, double aroundSeconds);
    void resetView() { autoFit = true; scrollY = 0.0f; }

private:
    void trackMenu (int track, float x, float y);
    std::vector<int> usedTracks() const;
    double xToSeconds (float x) const { return startSeconds + (double) (x - lanesX) / pxPerSecond; }
    float secondsToX (double s) const { return lanesX + (float) ((s - startSeconds) * pxPerSecond); }

    App& app;
    double pxPerSecond = 40.0, startSeconds = 0.0;
    float scrollY = 0.0f, rowHeight = 64.0f, lanesX = 0.0f, lanesW = 1000.0f;
    bool autoFit = true;
    double dragStartSeconds = 0.0;
    bool rulerDrag = false;
};
} // namespace hem::app
