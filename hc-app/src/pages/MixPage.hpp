#pragma once
// MIX tab: every used track, the 6 buses and the master pinned on the right; the plug-in library
// in the left panel (click a plug-in to put it on the selected strip).
#include "../App.hpp"

namespace hem::app
{
class MixPage
{
public:
    explicit MixPage (App& a) : app (a) {}
    void drawLeft (Ui&, Rect panel);
    void drawMain (Ui&, Rect panel);

private:
    App& app;
    float scrollX = 0.0f;
};
} // namespace hem::app
