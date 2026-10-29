#pragma once
// Modal dialogs drawn on the GL window (rename, save preset, "save changes?", file exists...).
#include "Ui.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hem::app
{
struct DialogField
{
    std::string label;
    std::string value;
    bool multiline = false;
    std::string placeholder;
};

struct DialogSpec
{
    std::string title;
    std::string message;
    std::vector<DialogField> fields;
    std::vector<std::string> buttons { "OK" };
    int enterButton = 0;      // ENTER presses this one
    int escapeButton = -1;    // ESC presses this one (-1 = the last button)
    Rgba accent = colours::glow;
    float width = 560.0f;
};

using DialogDone = std::function<void (int button, const std::vector<std::string>& values)>;

/** Opens a dialog on top of everything; `done` runs once with the button index and the field texts. */
void showDialog (Ui& ui, DialogSpec spec, DialogDone done);

/** A dialog drawn by the caller: `body` draws inside the box each frame and returns false to close. */
void showPanel (Ui& ui, std::string title, float width, float height, Rgba accent, std::function<bool (Ui&, Rect body)> body);
} // namespace hem::app
