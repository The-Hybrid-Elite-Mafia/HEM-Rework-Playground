#pragma once

#include "ElitePlugins.h"
#include <memory>
#include <vector>

namespace pg
{
/** The Elite plugin catalogue. Ids are the persistence contract (sessions, presets); never rename them. */
class PluginRegistry
{
public:
    static const std::vector<const PluginInfo*>& all();
    static const PluginInfo* find (const juce::String& id);
    /** Message thread (or an offline render thread). nullptr for an unknown id. */
    static std::unique_ptr<ElitePlugin> create (const juce::String& id);
};

/** Built-in presets. A preset sets the listed parameters and resets every other one to default. */
struct FactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;   // parameter id -> real (not normalised) value
};

class Presets
{
public:
    static const std::vector<FactoryPreset>& factory (const juce::String& pluginId);
    /** Message thread. Applies a factory preset (unlisted parameters go to default; bypass is kept). */
    static void apply (ElitePlugin&, const FactoryPreset&);

    /** User presets: %APPDATA%/Hybrid Elite Mafia/Rework Playground/Presets/<plugin id>/<name>.xml */
    static juce::File userFolder (const juce::String& pluginId);
    static juce::StringArray userPresetNames (const juce::String& pluginId);
    static bool saveUser (ElitePlugin&, const juce::String& name);
    static bool loadUser (ElitePlugin&, const juce::String& name);
    static bool deleteUser (const juce::String& pluginId, const juce::String& name);
    /** Lets tests point the user folder somewhere temporary. */
    static void setUserRootForTesting (const juce::File& root);
};
} // namespace pg
