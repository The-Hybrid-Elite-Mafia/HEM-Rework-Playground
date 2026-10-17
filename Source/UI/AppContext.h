#pragma once

#include "Engine/PlaygroundEngine.h"
#include "UI/PlaygroundLookAndFeel.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace pg::ui
{
class WaveCache;
class PluginWindows;

/**
    What every page of the main window shares. Owned by MainComponent; message thread only.
    Every structural change (plugins in / out, tracks in / out) goes through these calls so the
    floating plugin windows are closed before a plugin is deleted.
*/
struct AppContext
{
    PlaygroundEngine& engine;
    juce::AudioDeviceManager& devices;
    juce::PropertiesFile& settings;
    WaveCache& waves;
    PluginWindows& windows;

    std::function<void (const juce::String&)> status;          // bottom strip message
    std::function<void()> structureChanged;                     // tracks / inserts / names changed: pages refresh
    std::function<void (const juce::Array<juce::File>&)> importFiles;
    std::function<void (int tab)> showTab;

    int selectedStrip = 0;   // strip that "Plugins" menu inserts go to

    // structural operations (close windows first, then change the engine)
    ElitePlugin* insertPlugin (int strip, int slot, const juce::String& id);
    void removePlugin (int strip, int slot);
    void movePlugin (int strip, int from, int to);
    void openPlugin (int strip, int slot);
    void removeTrack (int track);
    /** First empty slot of a strip, or -1. */
    int firstFreeSlot (int strip) const;
    /** Insert into the first free slot of `strip` and open its window. */
    void addPluginToStrip (int strip, const juce::String& id);
    /** Popup menu of the 8 Elite plugins for a slot (respects master-only). */
    void showInsertMenu (int strip, int slot, juce::Component* target);

    static juce::File songsFolder();          // Documents/Hybrid Elite Mafia/Rework Playground
};

namespace tabs { enum { edit = 0, mix, forge, master, count }; }
} // namespace pg::ui
