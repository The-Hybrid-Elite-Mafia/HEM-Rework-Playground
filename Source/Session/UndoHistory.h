#pragma once

#include "../Engine/PlaygroundEngine.h"
#include <functional>
#include <map>
#include <vector>

namespace pg
{
/** Undo / redo for the whole session (mixer, inserts and their settings, tracks).

    Works by snapshots: the app calls poll() a few times a second on the message thread. When the
    session has changed and then stayed still for `settlePolls` polls (a knob turn or fader move has
    finished), the state from before the change becomes one undo step. Playhead position is ignored.

    Restoring: if the same tracks and the same plugins are in the same slots, only values are written
    back (plugin windows stay open). Otherwise the whole session tree is applied, re-using audio that
    is already decoded (this history keeps a reference to every file it has seen, so an undone
    "remove track" gets its audio back without reading the disk). Message thread only. */
class UndoHistory
{
public:
    explicit UndoHistory (PlaygroundEngine& e, int settlePolls = 2, int maxSteps = 100) : engine (e), settle (settlePolls), maxSteps (maxSteps) {}

    /** Forget all history and take the current session as the starting point (new / opened session). */
    void reset();
    /** Call regularly (e.g. 4 times a second). Returns true when a new undo step was recorded. */
    bool poll();

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    /** Returns false when there is nothing to undo / redo. */
    bool undo();
    bool redo();
    /** Second value: true if the restore had to rebuild tracks / plugins (the UI should refresh everything). */
    bool lastRestoreWasStructural() const noexcept { return lastStructural; }
    /** Called just before a restore that replaces tracks / plugins (close plugin windows here). */
    std::function<void()> beforeStructuralRestore;

private:
    juce::ValueTree snapshot();
    static juce::String structureKey (const juce::ValueTree&);
    void restore (const juce::ValueTree&);
    void rememberMedia();

    PlaygroundEngine& engine;
    const int settle, maxSteps;
    juce::ValueTree current;       // last settled state
    juce::String currentXml, pendingXml;
    int stableCount = 0;
    std::vector<juce::ValueTree> undoStack, redoStack;
    std::map<juce::String, SampleData::Ptr> media;
    bool lastStructural = false;
};
} // namespace pg
