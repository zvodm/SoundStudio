#pragma once
#include "song/PianoRollData.h"
#include "song/SongData.h"
#include "audio/Synth.h"
#include "editor/UndoManager.h"
#include <vector>

// FL-Studio-style piano roll:
//   - click an empty cell and drag right to create a note and set its length
//   - drag a note's body to move it (pitch and/or position); drag its right
//     edge to resize it
//   - right-click a note to delete it (deletes the whole selection if the
//     note you clicked is part of one)
//   - Ctrl+click a note to add/remove it from the selection without moving it
//   - Ctrl+drag from empty space to rubber-band select; hold Shift too to add
//     to the existing selection instead of replacing it
//   - with notes selected: Delete/Backspace removes them; arrow keys nudge
//     them (Left/Right = one step, Up/Down = one semitone); Ctrl+C/Ctrl+V
//     copy/paste (pasted notes land at the playhead, same pitches/lengths);
//     Ctrl+D duplicates the selection in place, shifted right by its own span
//   - click a piano key to audition a sound live
//   - Ctrl+scroll zooms horizontally, Shift+scroll zooms vertically, plain
//     scroll pans the visible pitch range (unchanged)
//   - pick a root/scale to highlight in-scale rows; with "Snap to Scale" on,
//     created and dragged notes snap to the nearest in-scale pitch
// Draws whichever track (by index) is selected. Takes the full
// instruments/tracks vectors (rather than just the one being edited) so it
// can push whole-session snapshots to the undo manager.
class PianoRollWidget
{
public:
    enum class ScaleType
    {
        Chromatic = 0, // no restriction -- every pitch counts as "in scale"
        Major,
        NaturalMinor,
        HarmonicMinor,
        MajorPentatonic,
        MinorPentatonic
    };

    // Call once per frame. Edits the track `selectedTrack` within
    // sections[editingSection]. playheadStep/showPlayhead come from
    // Sequencer -- showPlayhead should be false whenever the section that's
    // actually playing isn't the one being edited, purely for drawing the
    // red playhead line (playheadStep also doubles as the paste destination
    // regardless). Pushes whole-song (every section) snapshots to the undo
    // manager, since edits elsewhere (Add Track, grid resolution) can touch
    // every section at once. Sets dirty=true whenever an edit is made.
    void Draw(std::vector<Section>& sections, int editingSection, std::vector<int32_t>& arrangement,
              std::vector<Instrument>& instruments, int selectedTrack,
              Synth& synth, int playheadStep, bool showPlayhead, UndoManager& undo, bool& dirty);

private:
    enum class DragMode { None, Creating, Moving, Resizing };

    // A selected note's state as it was the instant a group drag/nudge
    // began, so deltas can be computed and clamped against the original
    // positions rather than compounding rounding/clamping error frame to frame.
    struct GroupNoteBackup { int index; PlacedNote note; };

    int pitchScrollOffset_ = 36; // lowest visible pitch
    int visiblePitches_ = 24;    // two octaves visible at once

    int heldPreviewPitch_ = -1;  // piano key currently held for live audition, -1 = none

    DragMode dragMode_ = DragMode::None;
    int dragNoteIndex_ = -1;     // index into track.notes, for Moving/Resizing/Creating
    PlacedNote dragOriginalNote_{};
    int dragStartStep_ = 0;
    int dragStartPitch_ = 0;
    std::vector<GroupNoteBackup> dragGroupOriginals_; // every selected note's pre-drag state, for group Moving

    int lastSelectedTrack_ = -1;      // detects a track switch, so selection doesn't carry across tracks
    int lastEditingSection_ = -1;     // detects a section switch, same reason
    std::vector<int> selectedNotes_;  // indices into track.notes

    bool rubberBandActive_ = false;
    float rubberBandStartX_ = 0.0f;
    float rubberBandStartY_ = 0.0f;

    std::vector<PlacedNote> clipboard_; // Ctrl+C/Ctrl+V, persists across track switches

    float zoomX_ = 1.0f; // multiplies the base cell width/height -- pure display, doesn't touch song data
    float zoomY_ = 1.0f;

    bool snapToScaleEnabled_ = false; // when on, created/dragged notes snap to the nearest in-scale pitch
    int scaleRoot_ = 0;               // 0=C .. 11=B
    ScaleType scaleType_ = ScaleType::Chromatic;

    bool InScale(int pitch) const;
    int  SnapPitchToScale(int pitch) const; // no-op unless snapToScaleEnabled_ and scaleType_ != Chromatic
};
