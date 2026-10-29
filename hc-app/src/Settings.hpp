#pragma once
// =============================================================================
//  The Playground's own settings (window, export choices, Song Forge / Band Forge picks...):
//  %APPDATA%\Hybrid Elite Mafia\Rework Playground\settings.json, every value kept as text.
//  On the first start of this version the 0.10.0 settings file
//  (%APPDATA%\Hybrid Elite Mafia\Rework Playground.settings) is read once, so every choice
//  carries over. The old file is never changed or deleted.
//
//  PropertiesFile reads and writes the small XML settings files JUCE apps keep, in the same
//  format: SongForge.settings is shared with Hybrid Studio (ACE-Step folder, address, key).
//  Message thread only.
// =============================================================================
#include <filesystem>
#include <map>
#include <string>

namespace hem::app
{
namespace fs = std::filesystem;

class PropertiesFile
{
public:
    explicit PropertiesFile (fs::path file) : path (std::move (file)) {}
    /** Missing file = empty (not an error). */
    bool load();
    /** Writes the file in JUCE's format (other apps reading it keep working). */
    bool save() const;
    std::string get (const std::string& key, const std::string& fallback = {}) const;
    void set (const std::string& key, const std::string& value) { values[key] = value; }
    const std::map<std::string, std::string>& all() const noexcept { return values; }
    const fs::path& file() const noexcept { return path; }

private:
    fs::path path;
    std::map<std::string, std::string> values;
};

class Settings
{
public:
    explicit Settings (fs::path file = defaultFile(), fs::path legacy = legacyFile());
    static fs::path defaultFile();
    static fs::path legacyFile();          // 0.10.0: %APPDATA%\Hybrid Elite Mafia\Rework Playground.settings
    static fs::path songForgeFile();       // shared with Hybrid Studio: %APPDATA%\Hybrid Elite Mafia\SongForge.settings

    /** Reads the file; on the very first start imports the 0.10.0 settings. */
    void load();
    /** Writes now if anything changed. */
    bool saveIfNeeded();
    int importedCount() const noexcept { return imported; }

    std::string get (const std::string& key, const std::string& fallback = {}) const;
    int getInt (const std::string& key, int fallback = 0) const;
    double getDouble (const std::string& key, double fallback = 0.0) const;
    bool getBool (const std::string& key, bool fallback = false) const;
    bool has (const std::string& key) const { return values.count (key) > 0; }

    void set (const std::string& key, const std::string& value);
    void set (const std::string& key, const char* value) { set (key, std::string (value)); }
    void set (const std::string& key, int value);
    void set (const std::string& key, double value);
    void set (const std::string& key, bool value);

private:
    fs::path path, legacyPath;
    std::map<std::string, std::string> values;
    bool dirty = false;
    int imported = 0;
};
} // namespace hem::app
