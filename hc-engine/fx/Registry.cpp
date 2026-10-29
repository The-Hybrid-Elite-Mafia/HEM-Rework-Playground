#include "Registry.hpp"
#include "EliteVox.hpp"
#include "EliteEq.hpp"
#include "EliteComp.hpp"
#include "EliteBass.hpp"
#include "EliteChannel.hpp"
#include "EliteVerb.hpp"
#include "EliteDelay.hpp"
#include "EliteSwirl.hpp"
#include "EliteMaster.hpp"
#include "EliteTransient.hpp"
#include "EliteMultiband.hpp"
#include "EliteWidth.hpp"
#include "EliteHush.hpp"
#include "EliteDeEss.hpp"
#include "EliteDoubler.hpp"
#include "EliteConsole.hpp"
#include "EliteLofi.hpp"
#include "EliteTune.hpp"
#include "EliteBalance.hpp"

namespace hem::pg
{
const std::vector<const PluginInfo*>& PluginRegistry::all()
{
    static const std::vector<const PluginInfo*> list { &EliteVox::info, &EliteEq::info, &EliteComp::info, &EliteBass::info, &EliteChannel::info, &EliteVerb::info, &EliteDelay::info, &EliteSwirl::info, &EliteMaster::info, &EliteTransient::info, &EliteMultiband::info, &EliteWidth::info, &EliteHush::info, &EliteDeEss::info, &EliteDoubler::info, &EliteConsole::info, &EliteLofi::info, &EliteTune::info, &EliteBalance::info };
    return list;
}

const PluginInfo* PluginRegistry::find (std::string_view id)
{
    for (auto* i : all()) if (id == i->id) return i;
    return nullptr;
}

std::unique_ptr<Plugin> PluginRegistry::create (std::string_view id)
{
    if (id == EliteVox::info.id) return std::make_unique<EliteVox>();
    if (id == EliteEq::info.id) return std::make_unique<EliteEq>();
    if (id == EliteComp::info.id) return std::make_unique<EliteComp>();
    if (id == EliteBass::info.id) return std::make_unique<EliteBass>();
    if (id == EliteChannel::info.id) return std::make_unique<EliteChannel>();
    if (id == EliteVerb::info.id) return std::make_unique<EliteVerb>();
    if (id == EliteDelay::info.id) return std::make_unique<EliteDelay>();
    if (id == EliteSwirl::info.id) return std::make_unique<EliteSwirl>();
    if (id == EliteMaster::info.id) return std::make_unique<EliteMaster>();
    if (id == EliteTransient::info.id) return std::make_unique<EliteTransient>();
    if (id == EliteMultiband::info.id) return std::make_unique<EliteMultiband>();
    if (id == EliteWidth::info.id) return std::make_unique<EliteWidth>();
    if (id == EliteHush::info.id) return std::make_unique<EliteHush>();
    if (id == EliteDeEss::info.id) return std::make_unique<EliteDeEss>();
    if (id == EliteDoubler::info.id) return std::make_unique<EliteDoubler>();
    if (id == EliteConsole::info.id) return std::make_unique<EliteConsole>();
    if (id == EliteLofi::info.id) return std::make_unique<EliteLofi>();
    if (id == EliteTune::info.id) return std::make_unique<EliteTune>();
    if (id == EliteBalance::info.id) return std::make_unique<EliteBalance>();
    return nullptr;
}
} // namespace hem::pg
