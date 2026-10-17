#pragma once

#include "Plugins/ElitePlugin.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace pg::ui
{
/** Floating plugin windows: any number open at once, one per plugin instance. Message thread. */
class PluginWindows
{
public:
    PluginWindows();
    ~PluginWindows();

    /** Opens (or brings to front) the window of `plugin`. */
    void open (ElitePlugin& plugin, const juce::String& where);
    void close (ElitePlugin* plugin);
    void closeAll();
    bool isOpen (ElitePlugin* plugin) const { return windows.count (plugin) > 0; }
    int count() const noexcept { return (int) windows.size(); }

private:
    class Window;
    std::map<ElitePlugin*, std::unique_ptr<Window>> windows;
    int cascade = 0;
};
} // namespace pg::ui
