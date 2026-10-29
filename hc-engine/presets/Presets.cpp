#include "Presets.hpp"

#include "pugixml.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hem::pg
{
namespace
{
    std::filesystem::path& userRootOverride() { static std::filesystem::path p; return p; }

    std::string formatValue (float v)
    {
        char buf[48];
        std::snprintf (buf, sizeof buf, "%.9g", (double) v);   // 9 significant digits: a float reads back exactly
        return buf;
    }
}

std::filesystem::path Presets::appDataFolder()
{
#if defined(_WIN32)
    const char* base = std::getenv ("APPDATA");
    std::filesystem::path root = base != nullptr ? std::filesystem::path (base) : std::filesystem::temp_directory_path();
#else
    const char* xdg = std::getenv ("XDG_CONFIG_HOME");
    const char* home = std::getenv ("HOME");
    std::filesystem::path root = xdg != nullptr ? std::filesystem::path (xdg)
                               : home != nullptr ? std::filesystem::path (home) / ".config" : std::filesystem::temp_directory_path();
#endif
    return root / "Hybrid Elite Mafia" / "Rework Playground";
}

const std::vector<FactoryPreset>& Presets::factory (std::string_view pluginId) { return legacy::factory (pluginId); }

void Presets::apply (Plugin& plugin, const FactoryPreset& preset)
{
    const auto& base = legacy::defaults0100 (plugin.getInfo().id);
    for (auto& p : plugin.parameters().all())
    {
        if (p->id() == "bypass") continue;
        const auto it = std::find_if (base.begin(), base.end(), [&] (auto& kv) { return kv.first == p->id(); });
        if (it != base.end()) p->setValue (it->second);
        else p->resetToDefault();
    }
    for (const auto& [id, value] : preset.values)
        plugin.setParameter (id, value);
}

std::string Presets::legalFileName (const std::string& name)
{
    std::string s;
    for (char c : name)
        if (std::string_view ("\"#@,;:<>*^|?\\/").find (c) == std::string_view::npos) s += c;
    if (s.size() > 128)
    {
        const auto dot = s.find_last_of ('.');
        if (dot != std::string::npos && (int) dot > std::max (0, (int) s.size() - 12))
            s = s.substr (0, 128 - (s.size() - dot)) + s.substr (dot);
        else
            s = s.substr (0, 128);
    }
    return s;
}

std::filesystem::path Presets::userFolder (std::string_view pluginId)
{
    const auto root = userRootOverride().empty() ? appDataFolder() / "Presets" : userRootOverride();
    return root / std::string (pluginId);
}

void Presets::setUserRootForTesting (const std::filesystem::path& root) { userRootOverride() = root; }

std::vector<std::string> Presets::userPresetNames (std::string_view pluginId)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator (userFolder (pluginId), ec))
        if (e.is_regular_file() && e.path().extension() == ".xml")
            names.push_back (e.path().stem().string());
    std::sort (names.begin(), names.end());
    return names;
}

namespace
{
    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t\r\n");
        if (a == std::string::npos) return {};
        const auto b = s.find_last_not_of (" \t\r\n");
        return s.substr (a, b - a + 1);
    }
}

bool Presets::saveUser (Plugin& plugin, const std::string& name)
{
    const auto clean = legalFileName (trim (name));
    if (clean.empty()) return false;
    const auto folder = userFolder (plugin.getInfo().id);
    std::error_code ec;
    std::filesystem::create_directories (folder, ec);
    if (ec) return false;
    std::ofstream out (folder / (clean + ".xml"), std::ios::binary | std::ios::trunc);
    if (! out) return false;
    out << stateXml::write (plugin.saveState(), trim (name));
    return (bool) out;
}

bool Presets::loadUser (Plugin& plugin, const std::string& name)
{
    std::ifstream in (userFolder (plugin.getInfo().id) / (legalFileName (name) + ".xml"), std::ios::binary);
    if (! in) return false;
    std::stringstream ss; ss << in.rdbuf();
    PluginState state;
    if (! stateXml::read (ss.str(), plugin.getInfo().id, state)) return false;
    const float bypass = plugin.getParameter ("bypass");
    plugin.loadState (state);
    plugin.setParameter ("bypass", bypass);   // loading a preset never changes bypass
    return true;
}

bool Presets::deleteUser (std::string_view pluginId, const std::string& name)
{
    std::error_code ec;
    return std::filesystem::remove (userFolder (pluginId) / (legalFileName (name) + ".xml"), ec);
}

//==============================================================================
namespace stateXml
{
    std::string typeNameFor (std::string_view pluginId)
    {
        std::string t (pluginId);
        for (auto& c : t) c = c == '-' ? '_' : (char) std::toupper ((unsigned char) c);
        return t;
    }

    std::string write (const PluginState& state, const std::string& presetName)
    {
        pugi::xml_document doc;
        auto decl = doc.append_child (pugi::node_declaration);
        decl.append_attribute ("version") = "1.0";
        decl.append_attribute ("encoding") = "UTF-8";
        auto root = doc.append_child (typeNameFor (state.pluginId).c_str());
        root.append_attribute ("pluginId") = state.pluginId.c_str();
        if (! presetName.empty()) root.append_attribute ("presetName") = presetName.c_str();
        for (auto& [id, v] : state.values)
        {
            auto p = root.append_child ("PARAM");
            p.append_attribute ("id") = id.c_str();
            p.append_attribute ("value") = formatValue (v).c_str();
        }
        std::ostringstream os;
        doc.save (os, "  ", pugi::format_default, pugi::encoding_utf8);
        return os.str();
    }

    bool read (const std::string& xml, std::string_view expectedPluginId, PluginState& out, std::string* presetName)
    {
        pugi::xml_document doc;
        if (! doc.load_string (xml.c_str())) return false;
        auto root = doc.document_element();
        if (std::string_view (root.name()) != typeNameFor (expectedPluginId)) return false;
        out.pluginId = std::string (expectedPluginId);
        out.values.clear();
        for (auto p : root.children ("PARAM"))
            if (p.attribute ("id") && p.attribute ("value"))
                out.values.emplace_back (p.attribute ("id").value(), (float) std::strtod (p.attribute ("value").value(), nullptr));
        if (presetName != nullptr) *presetName = root.attribute ("presetName").value();
        return true;
    }
}
} // namespace hem::pg
