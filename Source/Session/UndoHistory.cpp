#include "UndoHistory.h"
#include "../Plugins/ElitePlugin.h"

namespace pg
{
juce::ValueTree UndoHistory::snapshot()
{
    auto t = engine.createSessionTree();
    t.removeProperty ("position", nullptr);
    return t;
}

void UndoHistory::rememberMedia()
{
    for (int t = 0; t < PlaygroundEngine::kMaxTracks; ++t)
        if (auto m = engine.getTrackMedia (t))
            media[m->getFile().getFullPathName()] = m;
}

void UndoHistory::reset()
{
    undoStack.clear();
    redoStack.clear();
    media.clear();
    current = snapshot();
    currentXml = current.toXmlString();
    pendingXml.clear();
    stableCount = 0;
    rememberMedia();
}

bool UndoHistory::poll()
{
    if (! current.isValid()) { reset(); return false; }
    auto now = snapshot();
    const auto xml = now.toXmlString();
    if (xml == currentXml) { pendingXml.clear(); stableCount = 0; return false; }
    if (xml != pendingXml) { pendingXml = xml; stableCount = 0; return false; }   // still moving
    if (++stableCount < settle) return false;

    undoStack.push_back (current);
    if ((int) undoStack.size() > maxSteps) undoStack.erase (undoStack.begin());
    redoStack.clear();
    current = now;
    currentXml = xml;
    pendingXml.clear();
    stableCount = 0;
    rememberMedia();
    return true;
}

juce::String UndoHistory::structureKey (const juce::ValueTree& s)
{
    juce::String k;
    for (const auto& c : s)
    {
        if (c.hasType ("TRACK")) k << "T" << c["index"].toString() << "=" << c["file"].toString() << ";";
        else if (c.hasType ("INSERT")) k << "I" << c["strip"].toString() << "/" << c["slot"].toString() << "=" << c["plugin"].toString() << ";";
    }
    return k;
}

void UndoHistory::restore (const juce::ValueTree& target)
{
    rememberMedia();
    const auto live = snapshot();
    lastStructural = structureKey (target) != structureKey (live);
    if (lastStructural)
    {
        if (beforeStructuralRestore) beforeStructuralRestore();
        const double pos = engine.getPositionSeconds();
        engine.applySessionTree (target, media);
        engine.setPositionSeconds (pos);
    }
    else
    {
        // values only: mixer parameters, track names / bus names, loop, and each plugin's settings
        auto& apvts = engine.getAPVTS();
        const auto mixer = target.getChildWithName ("MIXER").getChildWithName (apvts.state.getType());
        for (auto* p : engine.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto child = mixer.getChildWithProperty ("id", rp->getParameterID());
                if (child.isValid())
                {
                    const float v = rp->convertTo0to1 ((float) child["value"]);
                    if (std::abs (v - rp->getValue()) > 1.0e-7f) rp->setValueNotifyingHost (v);
                }
            }
        for (const auto& c : target)
        {
            if (c.hasType ("TRACK")) engine.setTrackName (c["index"], c["name"].toString());
            else if (c.hasType ("BUS")) engine.setBusName (c["index"], c["name"].toString());
            else if (c.hasType ("INSERT"))
                if (auto* p = engine.getPlugin (c["strip"], c["slot"]))
                    p->loadState (c.getChild (0));
        }
        engine.setLoop ((bool) target["loopOn"], (double) target["loopStart"], (double) target["loopEnd"]);
    }
    current = snapshot();
    currentXml = current.toXmlString();
    pendingXml.clear();
    stableCount = 0;
}

bool UndoHistory::undo()
{
    poll();   // a change that has not settled yet is still the "current" one
    if (pendingXml.isNotEmpty())
    {
        // the last move had not settled: record it so undo takes it back
        undoStack.push_back (current);
        current = snapshot();
        currentXml = current.toXmlString();
        pendingXml.clear();
    }
    if (undoStack.empty()) return false;
    auto target = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back (current);
    restore (target);
    return true;
}

bool UndoHistory::redo()
{
    if (redoStack.empty()) return false;
    auto target = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back (current);
    restore (target);
    return true;
}
} // namespace pg
