#pragma once
#include "song/SongData.h"
#include "song/PianoRollData.h"
#include <vector>

// Snapshot-based undo: before any discrete edit (a drag starting, a note
// deleted, a slider grabbed, a track/section added/removed), the caller
// pushes a copy of the current instruments+sections+arrangement state.
// Undo/redo just swap that state in and out. Simple and correct; not the
// most memory-efficient approach for a huge song, but plenty fine at this
// project's scale.
class UndoManager
{
public:
    void PushUndo(const std::vector<Instrument>& instruments, const std::vector<Section>& sections,
                  const std::vector<int32_t>& arrangement)
    {
        undoStack_.push_back({ instruments, sections, arrangement });
        if (undoStack_.size() > maxEntries_) undoStack_.erase(undoStack_.begin());
        redoStack_.clear();
    }

    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    void Undo(std::vector<Instrument>& instruments, std::vector<Section>& sections, std::vector<int32_t>& arrangement)
    {
        if (undoStack_.empty()) return;
        redoStack_.push_back({ instruments, sections, arrangement });
        Snapshot snap = std::move(undoStack_.back());
        undoStack_.pop_back();
        instruments = std::move(snap.instruments);
        sections = std::move(snap.sections);
        arrangement = std::move(snap.arrangement);
    }

    void Redo(std::vector<Instrument>& instruments, std::vector<Section>& sections, std::vector<int32_t>& arrangement)
    {
        if (redoStack_.empty()) return;
        undoStack_.push_back({ instruments, sections, arrangement });
        Snapshot snap = std::move(redoStack_.back());
        redoStack_.pop_back();
        instruments = std::move(snap.instruments);
        sections = std::move(snap.sections);
        arrangement = std::move(snap.arrangement);
    }

    // Clears history. Call after a Load -- undoing past a freshly loaded
    // song into whatever was open before it would be confusing.
    void Clear()
    {
        undoStack_.clear();
        redoStack_.clear();
    }

private:
    struct Snapshot
    {
        std::vector<Instrument> instruments;
        std::vector<Section> sections;
        std::vector<int32_t> arrangement;
    };

    std::vector<Snapshot> undoStack_;
    std::vector<Snapshot> redoStack_;
    size_t maxEntries_ = 50;
};
