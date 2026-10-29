#include "UndoHistory.hpp"
#include "SessionFile.hpp"

#include <cmath>

namespace hem::pg
{
Tree UndoHistory::snapshot()
{
    auto t = engine.createSessionTree();
    t.remove ("position");
    return t;
}

void UndoHistory::rememberMedia()
{
    for (int t = 0; t < PlaygroundEngine::kMaxTracks; ++t)
        if (auto m = engine.getTrackMedia (t))
            media[SessionFile::toText (m->getFile())] = m;
}

void UndoHistory::reset()
{
    undoStack.clear();
    redoStack.clear();
    media.clear();
    current = snapshot();
    pending.reset();
    stableCount = 0;
    rememberMedia();
}

bool UndoHistory::poll()
{
    if (! current.isValid()) { reset(); return false; }
    auto now = snapshot();
    if (now == current) { pending.reset(); stableCount = 0; return false; }
    if (! pending || now != *pending) { pending = std::move (now); stableCount = 0; return false; }   // still moving
    if (++stableCount < settle) return false;

    undoStack.push_back (current);
    if ((int) undoStack.size() > maxSteps) undoStack.erase (undoStack.begin());
    redoStack.clear();
    current = std::move (now);
    pending.reset();
    stableCount = 0;
    rememberMedia();
    return true;
}

std::string UndoHistory::structureKey (const Tree& s)
{
    std::string k;
    for (const auto& c : s.children())
    {
        if (c.hasType ("TRACK")) k += "T" + c.get ("index") + "=" + c.get ("file") + ";";
        else if (c.hasType ("INSERT")) k += "I" + c.get ("strip") + "/" + c.get ("slot") + "=" + c.get ("plugin") + ";";
    }
    return k;
}

void UndoHistory::restore (const Tree& target)
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
        // values only: mixer controls, track / bus names, loop, and each plug-in's settings
        const auto& mixerTree = target.child ("MIXER").child (PlaygroundEngine::kMixerType);
        for (const auto& p : engine.mixer().all())
        {
            const auto& c = mixerTree.childWith ("id", p->id());
            if (c.isValid())
            {
                const float v = p->spec().range.valueTo01 ((float) c.getDouble ("value"));
                if (std::abs (v - p->getNormalised()) > 1.0e-7f) p->setNormalised (v);
            }
        }
        for (const auto& c : target.children())
        {
            if (c.hasType ("TRACK")) engine.setTrackName (c.getInt ("index"), c.get ("name"));
            else if (c.hasType ("BUS")) engine.setBusName (c.getInt ("index"), c.get ("name"));
            else if (c.hasType ("INSERT"))
                if (auto* p = engine.getPlugin (c.getInt ("strip"), c.getInt ("slot")))
                {
                    PluginState state;
                    if (c.childAt (0).isValid() && stateXml::read (c.childAt (0).toXml(), c.get ("plugin"), state))
                        p->loadState (state);
                }
        }
        engine.setLoop (target.getBool ("loopOn"), target.getDouble ("loopStart"), target.getDouble ("loopEnd"));
    }
    current = snapshot();
    pending.reset();
    stableCount = 0;
}

bool UndoHistory::undo()
{
    poll();   // a change that has not settled yet is still the "current" one
    if (pending)
    {
        // the last move had not settled: record it so undo takes it back
        undoStack.push_back (current);
        current = snapshot();
        pending.reset();
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
} // namespace hem::pg
