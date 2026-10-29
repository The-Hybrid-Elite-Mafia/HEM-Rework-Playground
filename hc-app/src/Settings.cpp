#include "Settings.hpp"

#include "Presets.hpp"
#include "hem/pg/Files.hpp"
#include "hem/pg/Json.hpp"
#include "pugixml.hpp"

#include <charconv>
#include <cstdlib>
#include <sstream>

namespace hem::app
{
//==============================================================================
bool PropertiesFile::load()
{
    values.clear();
    std::error_code ec;
    if (! fs::exists (path, ec)) return true;
    pugi::xml_document doc;
    if (! doc.load_file (path.c_str())) return false;
    for (auto v : doc.child ("PROPERTIES").children ("VALUE"))
    {
        const std::string name = v.attribute ("name").as_string();
        if (name.empty() || ! v.attribute ("val")) continue;   // values stored as XML (audio device) are skipped
        values[name] = v.attribute ("val").as_string();
    }
    return true;
}

bool PropertiesFile::save() const
{
    // the same layout JUCE writes: a PROPERTIES element with one VALUE per key
    pugi::xml_document doc;
    // keep anything we don't understand (XML values of the other app)
    pugi::xml_document old;
    std::error_code ec;
    const bool hadOld = fs::exists (path, ec) && old.load_file (path.c_str());
    auto root = doc.append_child ("PROPERTIES");
    if (hadOld)
        for (auto v : old.child ("PROPERTIES").children ("VALUE"))
            if (! v.attribute ("val")) root.append_copy (v);
    for (const auto& [k, val] : values)
    {
        auto v = root.append_child ("VALUE");
        v.append_attribute ("name") = k.c_str();
        v.append_attribute ("val") = val.c_str();
    }
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n";
    doc.save (out, "  ", pugi::format_indent | pugi::format_no_declaration, pugi::encoding_utf8);
    fs::create_directories (path.parent_path(), ec);
    return pg::files::writeTextAtomic (path, out.str());
}

std::string PropertiesFile::get (const std::string& key, const std::string& fallback) const
{
    const auto it = values.find (key);
    return it != values.end() ? it->second : fallback;
}

//==============================================================================
Settings::Settings (fs::path file, fs::path legacy) : path (std::move (file)), legacyPath (std::move (legacy)) {}

fs::path Settings::defaultFile() { return pg::Presets::appDataFolder() / "settings.json"; }
fs::path Settings::legacyFile() { return pg::Presets::appDataFolder().parent_path() / "Rework Playground.settings"; }
fs::path Settings::songForgeFile() { return pg::Presets::appDataFolder().parent_path() / "SongForge.settings"; }

void Settings::load()
{
    values.clear();
    if (auto text = pg::files::readText (path))
    {
        if (const auto j = pg::Json::parse (*text); j && j->isObject())
        {
            for (const auto& [k, v] : j->members()) values[k] = v.toString();
            return;
        }
    }
    // first start of this version: carry the 0.10.0 choices over (the old file is left as it is)
    PropertiesFile legacy (legacyPath);
    if (legacy.load())
        for (const auto& [k, v] : legacy.all()) { values[k] = v; ++imported; }
    dirty = true;
}

bool Settings::saveIfNeeded()
{
    if (! dirty) return true;
    auto j = pg::Json::object();
    for (const auto& [k, v] : values) j.set (k, pg::Json (v));
    std::error_code ec;
    fs::create_directories (path.parent_path(), ec);
    const bool ok = pg::files::writeTextAtomic (path, j.dump (true));
    if (ok) dirty = false;
    return ok;
}

std::string Settings::get (const std::string& key, const std::string& fallback) const
{
    const auto it = values.find (key);
    return it != values.end() ? it->second : fallback;
}

int Settings::getInt (const std::string& key, int fallback) const
{
    const auto it = values.find (key);
    if (it == values.end() || it->second.empty()) return fallback;
    const std::string& s = it->second;
    if (s == "true") return 1;
    if (s == "false") return 0;
    return (int) std::strtol (s.c_str(), nullptr, 10);
}

double Settings::getDouble (const std::string& key, double fallback) const
{
    const auto it = values.find (key);
    if (it == values.end() || it->second.empty()) return fallback;
    double v = fallback;
    const auto& s = it->second;
    const auto r = std::from_chars (s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() ? v : fallback;
}

bool Settings::getBool (const std::string& key, bool fallback) const
{
    const auto it = values.find (key);
    if (it == values.end() || it->second.empty()) return fallback;
    const auto& s = it->second;
    return s == "1" || s == "true" || s == "yes";
}

void Settings::set (const std::string& key, const std::string& value)
{
    auto it = values.find (key);
    if (it != values.end() && it->second == value) return;
    values[key] = value;
    dirty = true;
}

void Settings::set (const std::string& key, int value) { set (key, std::to_string (value)); }

void Settings::set (const std::string& key, double value)
{
    char buf[64];
    const auto r = std::to_chars (buf, buf + sizeof buf, value);
    set (key, std::string (buf, r.ptr));
}

void Settings::set (const std::string& key, bool value) { set (key, std::string (value ? "1" : "0")); }
} // namespace hem::app
