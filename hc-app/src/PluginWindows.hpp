#pragma once
// =============================================================================
//  Floating plug-in windows: any number open at once, one per plug-in instance.
//  Each is its own OpenGL window with The Hybrid Core faceplate (FxPanel: knobs, switches,
//  meters, BYPASS), a preset bar on top (factory / my presets, < >, SAVE) and, for Elite EQ and
//  Elite Swirl Killer, their live display. Drawn by the main loop, UI thread only.
// =============================================================================
#include "App.hpp"

#include <map>
#include <memory>

namespace hem::app
{
class PluginWindow;

class PluginWindows
{
public:
    explicit PluginWindows (App&);
    ~PluginWindows();

    /** Opens (or brings to front) the window of `plugin`. */
    void open (pg::Plugin& plugin, const std::string& where);
    void close (pg::Plugin* plugin);
    void closeAll();
    bool isOpen (const pg::Plugin* plugin) const { return windows.count (plugin) > 0; }
    int count() const noexcept { return (int) windows.size(); }
    /** Draws every open window (closes the ones the user closed). Leaves no GL context current. */
    void frame (double seconds);
    /** Off-screen picture of the first open window (CI). */
    bool snapshot (const fs::path& png);

private:
    App& app;
    std::map<const pg::Plugin*, std::unique_ptr<PluginWindow>> windows;
    int cascade = 0;
};
} // namespace hem::app
