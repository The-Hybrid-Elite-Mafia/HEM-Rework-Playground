#pragma once
// =============================================================================
//  Plug-in presets: built-in (factory) presets and the user's own saved presets.
//  User presets live in %APPDATA%\Hybrid Elite Mafia\Rework Playground\Presets\<plugin id>\<name>.xml,
//  the same place and file format the 0.10.0 app used, so presets saved there still load.
//  Message thread only.
// =============================================================================
#include "hem/pg/Plugin.hpp"

#include <filesystem>

namespace hem::pg
{
struct FactoryPreset
{
    std::string name;
    std::vector<std::pair<std::string, float>> values;   // control id -> real (not 0..1) value
};

class Presets
{
public:
    static const std::vector<FactoryPreset>& factory (std::string_view pluginId);
    /** Applies a built-in preset: unlisted controls go to their starting value; bypass is kept. */
    static void apply (Plugin&, const FactoryPreset&);

    static std::filesystem::path userFolder (std::string_view pluginId);
    static std::vector<std::string> userPresetNames (std::string_view pluginId);
    static bool saveUser (Plugin&, const std::string& name);
    static bool loadUser (Plugin&, const std::string& name);
    static bool deleteUser (std::string_view pluginId, const std::string& name);
    /** Lets tests point the user folder somewhere temporary (empty path = the real folder). */
    static void setUserRootForTesting (const std::filesystem::path& root);

    /** A name made safe for a file (the characters "#@,;:<>*^|?\/ removed, at most 128 long). */
    static std::string legalFileName (const std::string& name);
    /** The folder all HEM Rework Playground data lives in (%APPDATA%\Hybrid Elite Mafia\Rework Playground). */
    static std::filesystem::path appDataFolder();
};

/** One plug-in's settings as XML (<ELITE_VERB pluginId=".."><PARAM id=".." value=".."/>...), as 0.10.0 wrote them. */
namespace stateXml
{
    std::string write (const PluginState& state, const std::string& presetName = {});
    /** Reads a plug-in state. False if the text is not a state for `expectedPluginId`. */
    bool read (const std::string& xml, std::string_view expectedPluginId, PluginState& out, std::string* presetName = nullptr);
    std::string typeNameFor (std::string_view pluginId);   // "elite-verb" -> "ELITE_VERB"
}

namespace legacy
{
    /** The 0.10.0 factory presets (temporary, until the research-based presets replace them). */
    const std::vector<FactoryPreset>& factory (std::string_view pluginId);
    /** Every control's 0.10.0 starting value. */
    const std::vector<std::pair<std::string, float>>& defaults0100 (std::string_view pluginId);
}
} // namespace hem::pg
