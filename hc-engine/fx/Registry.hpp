#pragma once
// The Playground plug-in catalogue. Ids are the persistence contract (sessions, presets): never rename them.
#include "hem/pg/Plugin.hpp"

#include <memory>

namespace hem::pg
{
class PluginRegistry
{
public:
    static const std::vector<const PluginInfo*>& all();
    static const PluginInfo* find (std::string_view id);
    /** Message thread (or an offline render thread). nullptr for an unknown id. */
    static std::unique_ptr<Plugin> create (std::string_view id);
};
} // namespace hem::pg
