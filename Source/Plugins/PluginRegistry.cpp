#include "PluginRegistry.h"

namespace pg
{
const std::vector<const PluginInfo*>& PluginRegistry::all()
{
    static const std::vector<const PluginInfo*> list { &EliteVox::info, &EliteEq::info, &EliteComp::info, &EliteBass::info,
                                                       &EliteChannel::info, &EliteVerb::info, &EliteDelay::info, &EliteSwirl::info,
                                                       &EliteMaster::info };
    return list;
}

const PluginInfo* PluginRegistry::find (const juce::String& id)
{
    for (auto* i : all())
        if (id == i->id) return i;
    return nullptr;
}

std::unique_ptr<ElitePlugin> PluginRegistry::create (const juce::String& id)
{
    if (id == EliteVox::info.id)     return std::make_unique<EliteVox>();
    if (id == EliteEq::info.id)      return std::make_unique<EliteEq>();
    if (id == EliteComp::info.id)    return std::make_unique<EliteComp>();
    if (id == EliteBass::info.id)    return std::make_unique<EliteBass>();
    if (id == EliteChannel::info.id) return std::make_unique<EliteChannel>();
    if (id == EliteVerb::info.id)    return std::make_unique<EliteVerb>();
    if (id == EliteDelay::info.id)   return std::make_unique<EliteDelay>();
    if (id == EliteSwirl::info.id)   return std::make_unique<EliteSwirl>();
    if (id == EliteMaster::info.id)  return std::make_unique<EliteMaster>();
    return nullptr;
}

//==============================================================================
namespace
{
    using P = FactoryPreset;

    std::vector<FactoryPreset> voxPresets()
    {
        return {
            P { "Suno Kick Punch",          { { "mode", 1 }, { "squeeze", 9 }, { "gate", -45 } } },
            P { "Suno Snare Crack",         { { "mode", 1 }, { "squeeze", 8 }, { "gate", -42 } } },
            P { "Suno Lead Vocal Even",     { { "mode", 0 }, { "squeeze", 12 }, { "gate", -60 } } },
            P { "Suno Backing Vocals Tuck", { { "mode", 0 }, { "squeeze", 15 }, { "output", -3 } } },
            P { "808 Steady",               { { "mode", 2 }, { "squeeze", 10 } } },
            P { "Drum Bus Squeeze",         { { "mode", 3 }, { "squeeze", 6 } } } };
    }

    std::vector<FactoryPreset> eqPresets()
    {
        return {
            P { "Suno Kick: Weight + Click", { { "b1_type", 0 }, { "b1_freq", 30 }, { "b2_freq", 60 }, { "b2_gain", 3 }, { "b2_q", 1.0f },
                                               { "b3_freq", 350 }, { "b3_gain", -4 }, { "b3_q", 1.2f }, { "b5_freq", 3500 }, { "b5_gain", 3 },
                                               { "b6_freq", 10000 }, { "b6_gain", -2 } } },
            P { "Suno Snare: Body + Crack",  { { "b1_type", 0 }, { "b1_freq", 80 }, { "b2_freq", 200 }, { "b2_gain", 2.5f }, { "b2_q", 1.2f },
                                               { "b3_freq", 500 }, { "b3_gain", -3 }, { "b5_freq", 5000 }, { "b5_gain", 3 }, { "b6_freq", 12000 }, { "b6_gain", 1.5f } } },
            P { "Suno Hats: Tame Harsh",     { { "b1_type", 0 }, { "b1_freq", 300 }, { "b5_freq", 7000 }, { "b5_gain", -3 }, { "b5_q", 2.0f },
                                               { "b6_freq", 12000 }, { "b6_gain", 1 } } },
            P { "808: Clean Sub",            { { "b1_type", 0 }, { "b1_freq", 25 }, { "b2_freq", 55 }, { "b2_gain", 2 }, { "b3_freq", 250 }, { "b3_gain", -3 },
                                               { "b6_type", 1 }, { "b6_freq", 6000 } } },
            P { "Vocal: Clarity",            { { "b1_type", 0 }, { "b1_freq", 90 }, { "b3_freq", 300 }, { "b3_gain", -2.5f }, { "b4_freq", 3000 }, { "b4_gain", 2 },
                                               { "b4_q", 0.9f }, { "b6_freq", 12000 }, { "b6_gain", 2.5f } } },
            P { "Vocal: De-Mud Robot",       { { "b1_type", 0 }, { "b1_freq", 80 }, { "b2_freq", 180 }, { "b2_gain", -2 }, { "b3_freq", 450 }, { "b3_gain", -3 },
                                               { "b3_q", 1.4f }, { "b4_freq", 2500 }, { "b4_gain", 1.5f }, { "b5_freq", 6000 }, { "b5_gain", -2 }, { "b5_q", 2.0f } } },
            P { "Mix Bus: Smile",            { { "b1_freq", 80 }, { "b1_gain", 1.5f }, { "b3_freq", 400 }, { "b3_gain", -1 }, { "b6_freq", 10000 }, { "b6_gain", 1.5f } } } };
    }

    std::vector<FactoryPreset> compPresets()
    {
        return {
            P { "Drum Bus Glue",              { { "threshold", -18 }, { "ratio", 4 }, { "attack", 10 }, { "release", 100 }, { "mode", 1 }, { "gain", 2 } } },
            P { "Drum Bus Smash (Parallel)",  { { "threshold", -35 }, { "ratio", 10 }, { "attack", 1 }, { "release", 80 }, { "mode", 1 }, { "gain", 8 }, { "mix", 35 } } },
            P { "Vocal Smooth",               { { "threshold", -24 }, { "ratio", 3 }, { "attack", 5 }, { "release", 120 }, { "mode", 2 }, { "gain", 4 } } },
            P { "808 Control",                { { "threshold", -18 }, { "ratio", 4 }, { "attack", 20 }, { "release", 150 }, { "mode", 2 }, { "gain", 3 } } },
            P { "Mix Bus 2 dB",               { { "threshold", -12 }, { "ratio", 2 }, { "attack", 30 }, { "release", 200 }, { "mode", 0 }, { "gain", 1 } } } };
    }

    std::vector<FactoryPreset> bassPresets()
    {
        return {
            P { "808 Big",             { { "freq", 60 }, { "intensity", 60 }, { "output", -1 } } },
            P { "Small Speaker Boost", { { "freq", 70 }, { "intensity", 80 }, { "original", -6 } } },
            P { "Sub Tighten",         { { "freq", 45 }, { "intensity", 35 }, { "original", -3 } } },
            P { "Kick Weight",         { { "freq", 55 }, { "intensity", 40 } } } };
    }

    std::vector<FactoryPreset> channelPresets()
    {
        return {
            P { "Snare Cleanup", { { "gate_on", 1 }, { "gate_thr", -40 }, { "gate_floor", 25 }, { "gate_rel", 120 }, { "eq2_freq", 200 }, { "eq2_gain", 2 },
                                   { "eq3_freq", 5000 }, { "eq3_gain", 3 }, { "comp_thr", -18 }, { "comp_ratio", 4 }, { "comp_atk", 8 }, { "comp_rel", 120 } } },
            P { "Hats Tidy",     { { "gate_on", 1 }, { "gate_thr", -45 }, { "gate_floor", 15 }, { "eq1_freq", 300 }, { "eq1_gain", -12 },
                                   { "eq3_freq", 7000 }, { "eq3_gain", -2 }, { "comp_on", 0 } } },
            P { "Vocal Channel", { { "eq1_freq", 100 }, { "eq1_gain", -3 }, { "eq2_freq", 300 }, { "eq2_gain", -2 }, { "eq3_freq", 3000 }, { "eq3_gain", 2 },
                                   { "eq4_freq", 12000 }, { "eq4_gain", 2 }, { "comp_thr", -22 }, { "comp_ratio", 3 }, { "comp_mode", 1 } } },
            P { "Kick Channel",  { { "gate_on", 1 }, { "gate_thr", -35 }, { "gate_floor", 30 }, { "eq1_freq", 60 }, { "eq1_gain", 3 }, { "eq2_freq", 350 },
                                   { "eq2_gain", -4 }, { "eq3_freq", 4000 }, { "eq3_gain", 3 }, { "comp_thr", -16 }, { "comp_ratio", 4 }, { "comp_atk", 15 } } } };
    }

    std::vector<FactoryPreset> verbPresets()
    {
        return {
            P { "Drum Room (send)",     { { "type", 0 }, { "predelay", 5 }, { "size", 40 }, { "decay", 0.7f }, { "damping", 50 }, { "lowcut", 200 }, { "mix", 100 } } },
            P { "Vocal Plate (send)",   { { "type", 1 }, { "predelay", 30 }, { "size", 55 }, { "decay", 1.8f }, { "damping", 45 }, { "lowcut", 180 }, { "mix", 100 } } },
            P { "Big Hall (send)",      { { "type", 2 }, { "predelay", 40 }, { "size", 75 }, { "decay", 3.5f }, { "damping", 40 }, { "lowcut", 150 }, { "mix", 100 } } },
            P { "Snare Plate (insert)", { { "type", 1 }, { "predelay", 10 }, { "size", 45 }, { "decay", 1.2f }, { "mix", 18 } } },
            P { "Vocal Room (insert)",  { { "type", 0 }, { "predelay", 15 }, { "size", 50 }, { "decay", 0.9f }, { "mix", 15 } } } };
    }

    std::vector<FactoryPreset> delayPresets()
    {
        return {
            P { "1/4 Vocal Throw (send)", { { "sync", 1 }, { "division", 8 }, { "feedback", 30 }, { "lowcut", 200 }, { "highcut", 6000 }, { "mix", 100 }, { "duck", 50 } } },
            P { "1/8 Ping-Pong (send)",   { { "sync", 1 }, { "division", 5 }, { "feedback", 35 }, { "pingpong", 100 }, { "mix", 100 } } },
            P { "Slap Back (insert)",     { { "sync", 0 }, { "time", 110 }, { "feedback", 5 }, { "lowcut", 150 }, { "highcut", 5000 }, { "mix", 20 } } },
            P { "1/8 Dotted (insert)",    { { "sync", 1 }, { "division", 6 }, { "feedback", 40 }, { "mix", 20 }, { "duck", 30 } } },
            P { "Trap Echo Out",          { { "sync", 1 }, { "division", 8 }, { "feedback", 60 }, { "highcut", 4000 }, { "mix", 30 } } } };
    }

    std::vector<FactoryPreset> masterPresets()
    {
        return {
            P { "Streaming Safe (-14)", { { "input", 4 }, { "ceiling", -1 }, { "release", 80 }, { "character", 0 }, { "target", 0 } } },
            P { "Loud Trap (-9)",       { { "input", 10 }, { "ceiling", -1 }, { "release", 60 }, { "character", 1 }, { "target", 2 } } },
            P { "Warm Country",         { { "input", 6 }, { "ceiling", -1 }, { "release", 120 }, { "character", 2 }, { "target", 0 } } },
            P { "Club Banger (-7)",     { { "input", 13 }, { "ceiling", -0.5f }, { "release", 40 }, { "character", 1 }, { "target", 3 } } } };
    }

    std::vector<FactoryPreset> swirlPresets()
    {
        return {
            P { "Suno Swirl - Gentle",       { { "amount", 40 }, { "from", 4000 }, { "protect", 50 }, { "mode", 1 } } },
            P { "Suno Swirl - Strong",       { { "amount", 85 }, { "from", 3000 }, { "protect", 30 }, { "mode", 1 } } },
            P { "Cymbal Shimmer Wash",       { { "amount", 70 }, { "from", 6000 }, { "protect", 35 }, { "mode", 0 } } },
            P { "Vocal: Underwater Fix",     { { "amount", 60 }, { "from", 2500 }, { "protect", 45 }, { "mode", 1 } } },
            P { "Master Polish (subtle)",    { { "amount", 30 }, { "from", 5000 }, { "protect", 60 }, { "mode", 1 } } },
            P { "Hear What It Removes",      { { "amount", 70 }, { "from", 4000 }, { "protect", 40 }, { "mode", 1 }, { "listen", 1 } } },
            // the five "restoration recipes"
            P { "Recipe: Vocal Air Safe",    { { "amount", 45 }, { "from", 7500 }, { "protect", 55 }, { "mode", 1 }, { "shield", 85 } } },
            P { "Recipe: Heavy De-Shimmer",  { { "amount", 85 }, { "from", 5000 }, { "protect", 30 }, { "mode", 0 }, { "shield", 60 } } },
            P { "Recipe: Whistle Killer",    { { "amount", 35 }, { "from", 8000 }, { "protect", 50 }, { "mode", 1 }, { "whistle", 100 }, { "shield", 70 } } },
            P { "Recipe: Gentle Polish",     { { "amount", 25 }, { "from", 6000 }, { "protect", 65 }, { "mode", 1 }, { "whistle", 40 }, { "shield", 80 } } },
            P { "Recipe: Extreme Purge",     { { "amount", 100 }, { "from", 2500 }, { "protect", 15 }, { "mode", 0 }, { "whistle", 100 }, { "shield", 30 } } } };
    }

    juce::File& userRootOverride() { static juce::File f; return f; }
}

const std::vector<FactoryPreset>& Presets::factory (const juce::String& id)
{
    static const std::vector<FactoryPreset> vox = voxPresets(), eq = eqPresets(), comp = compPresets(), bass = bassPresets(),
                                            channel = channelPresets(), verb = verbPresets(), delay = delayPresets(), master = masterPresets(),
                                            swirl = swirlPresets(), none;
    if (id == EliteVox::info.id) return vox;
    if (id == EliteEq::info.id) return eq;
    if (id == EliteComp::info.id) return comp;
    if (id == EliteBass::info.id) return bass;
    if (id == EliteChannel::info.id) return channel;
    if (id == EliteVerb::info.id) return verb;
    if (id == EliteDelay::info.id) return delay;
    if (id == EliteMaster::info.id) return master;
    if (id == EliteSwirl::info.id) return swirl;
    return none;
}

void Presets::apply (ElitePlugin& plugin, const FactoryPreset& preset)
{
    auto& apvts = plugin.getAPVTS();
    for (auto* p : plugin.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != "bypass")
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (const auto& [id, value] : preset.values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
        else
            jassertfalse;   // a factory preset names a parameter this plugin does not have
}

juce::File Presets::userFolder (const juce::String& pluginId)
{
    auto root = userRootOverride();
    if (root == juce::File())
        root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Hybrid Elite Mafia").getChildFile ("Rework Playground").getChildFile ("Presets");
    return root.getChildFile (pluginId);
}

void Presets::setUserRootForTesting (const juce::File& root) { userRootOverride() = root; }

juce::StringArray Presets::userPresetNames (const juce::String& pluginId)
{
    juce::StringArray names;
    for (const auto& f : userFolder (pluginId).findChildFiles (juce::File::findFiles, false, "*.xml"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool Presets::saveUser (ElitePlugin& plugin, const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto folder = userFolder (plugin.getInfo().id);
    if (! folder.createDirectory()) return false;
    auto state = plugin.saveState();
    state.setProperty ("presetName", name.trim(), nullptr);
    if (auto xml = state.createXml())
        return xml->writeTo (folder.getChildFile (clean + ".xml"));
    return false;
}

bool Presets::loadUser (ElitePlugin& plugin, const juce::String& name)
{
    const auto file = userFolder (plugin.getInfo().id).getChildFile (juce::File::createLegalFileName (name) + ".xml");
    if (auto xml = juce::XmlDocument::parse (file))
    {
        const bool bypassed = plugin.getAPVTS().getRawParameterValue ("bypass")->load() > 0.5f;
        plugin.loadState (juce::ValueTree::fromXml (*xml));
        if (auto* b = plugin.getAPVTS().getParameter ("bypass"))
            b->setValueNotifyingHost (bypassed ? 1.0f : 0.0f);   // loading a preset never changes bypass
        return true;
    }
    return false;
}

bool Presets::deleteUser (const juce::String& pluginId, const juce::String& name)
{
    return userFolder (pluginId).getChildFile (juce::File::createLegalFileName (name) + ".xml").deleteFile();
}
} // namespace pg
