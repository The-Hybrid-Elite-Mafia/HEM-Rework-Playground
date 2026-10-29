#pragma once
// =============================================================================
//  Undo / redo for the whole session (mixer, inserts and their settings, tracks).
//
//  Works by snapshots: the app calls poll() a few times a second on the message thread. When the
//  session has changed and then stayed still for `settlePolls` polls (a knob turn or fader move has
//  finished), the state from before the change becomes one undo step. Play-head position is ignored.
//
//  Restoring: if the same tracks and the same plug-ins are in the same slots, only values are
//  written back (plug-in windows stay open). Otherwise the whole session is applied, re-using audio
//  already decoded (every file seen is kept, so an undone "remove track" gets its audio back without
//  reading the disk). Message thread only.
// =============================================================================
#include "PlaygroundEngine.hpp"

#include <functional>

namespace hem::pg
{
class UndoHistory
{
public:
    explicit UndoHistory (PlaygroundEngine& e, int settlePolls = 2, int maxSteps = 100) : engine (e), settle (settlePolls), maxSteps (maxSteps) {}

    void reset();
    /** Returns true when a new undo step was recorded. */
    bool poll();

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool undo();
    bool redo();
    bool lastRestoreWasStructural() const noexcept { return lastStructural; }
    /** Called just before a restore that replaces tracks / plug-ins (close plug-in windows here). */
    std::function<void()> beforeStructuralRestore;

private:
    Tree snapshot();
    static std::string structureKey (const Tree&);
    void restore (const Tree&);
    void rememberMedia();

    PlaygroundEngine& engine;
    const int settle, maxSteps;
    Tree current;                    // last settled state
    std::optional<Tree> pending;     // a change still moving
    int stableCount = 0;
    std::vector<Tree> undoStack, redoStack;
    std::map<std::string, SampleData::Ptr> media;
    bool lastStructural = false;
};
} // namespace hem::pg
