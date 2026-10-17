#include "AppContext.h"
#include "UI/PluginWindows.h"

namespace pg::ui
{
using E = PlaygroundEngine;

juce::File AppContext::songsFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("Hybrid Elite Mafia").getChildFile ("Rework Playground");
    d.createDirectory();
    return d;
}

ElitePlugin* AppContext::insertPlugin (int strip, int slot, const juce::String& id)
{
    if (! engine.canHost (strip, id))
    {
        if (status) status (juce::String (PluginRegistry::find (id) != nullptr ? PluginRegistry::find (id)->name : "That plugin")
                            + " only goes on the MASTER (it has look-ahead).");
        return nullptr;
    }
    if (auto* old = engine.getPlugin (strip, slot)) windows.close (old);
    auto* p = engine.insertPlugin (strip, slot, id);
    if (structureChanged) structureChanged();
    return p;
}

void AppContext::removePlugin (int strip, int slot)
{
    if (auto* p = engine.getPlugin (strip, slot)) windows.close (p);
    engine.removePlugin (strip, slot);
    if (structureChanged) structureChanged();
}

void AppContext::movePlugin (int strip, int from, int to)
{
    if (! juce::isPositiveAndBelow (to, E::kSlots)) return;
    engine.swapSlots (strip, from, to);
    if (structureChanged) structureChanged();
}

void AppContext::openPlugin (int strip, int slot)
{
    if (auto* p = engine.getPlugin (strip, slot))
        windows.open (*p, engine.getStripName (strip) + "  (insert " + juce::String (slot + 1) + ")");
}

void AppContext::removeTrack (int track)
{
    for (int s = 0; s < E::kSlots; ++s)
        if (auto* p = engine.getPlugin (track, s)) windows.close (p);
    engine.removeTrack (track);
    if (selectedStrip == track) selectedStrip = E::kMaster;
    if (structureChanged) structureChanged();
}

int AppContext::firstFreeSlot (int strip) const
{
    for (int s = 0; s < E::kSlots; ++s)
        if (engine.getPlugin (strip, s) == nullptr) return s;
    return -1;
}

void AppContext::addPluginToStrip (int strip, const juce::String& id)
{
    // Elite Master always goes last on the master; everything else in the first free slot
    int slot = id == "elite-master" ? E::kSlots - 1 : firstFreeSlot (strip);
    if (slot < 0) { if (status) status (engine.getStripName (strip) + ": all 8 inserts are full"); return; }
    if (insertPlugin (strip, slot, id) != nullptr) openPlugin (strip, slot);
}

void AppContext::showInsertMenu (int strip, int slot, juce::Component* target)
{
    if (slot < 0) { if (status) status (engine.getStripName (strip) + ": all 8 inserts are full"); return; }
    juce::PopupMenu m;
    m.addSectionHeader (engine.getStripName (strip) + "  -  insert " + juce::String (slot + 1));
    const auto& all = PluginRegistry::all();
    for (int i = 0; i < (int) all.size(); ++i)
        m.addColouredItem (i + 1, juce::String (all[(size_t) i]->name) + "   -  " + all[(size_t) i]->blurb,
                           juce::Colour (all[(size_t) i]->accent), engine.canHost (strip, all[(size_t) i]->id));
    auto opts = juce::PopupMenu::Options();
    if (target != nullptr) opts = opts.withTargetComponent (target);
    m.showMenuAsync (opts, [this, strip, slot] (int r)
    {
        if (r <= 0) return;
        const auto* info = PluginRegistry::all()[(size_t) r - 1];
        if (insertPlugin (strip, slot, info->id) != nullptr) openPlugin (strip, slot);
    });
}
} // namespace pg::ui
