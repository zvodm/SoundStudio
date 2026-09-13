#include "ui/PianoRollWidget.h"
#include "audio/MusicUtils.h"
#include "audio/AudioConfig.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <limits>

static constexpr float CELL_W_BASE = 22.0f; // at zoomX_ == 1.0
static constexpr float CELL_H_BASE = 16.0f; // at zoomY_ == 1.0
static constexpr float KEYS_W = 50.0f;
static constexpr int   MAX_PITCH = 96;
static constexpr float RESIZE_HANDLE_PX = 6.0f; // width of the draggable edge on a note's right side

static const bool kIsBlackKey[12] = { false,true,false,true,false,false,true,false,true,false,true,false };

// Intervals (semitones above the root) that belong to each scale. Chromatic
// isn't listed here -- InScale() special-cases it to "everything is in scale".
static const int kMajorIntervals[]        = { 0, 2, 4, 5, 7, 9, 11 };
static const int kNaturalMinorIntervals[] = { 0, 2, 3, 5, 7, 8, 10 };
static const int kHarmonicMinorIntervals[]= { 0, 2, 3, 5, 7, 8, 11 };
static const int kMajorPentIntervals[]    = { 0, 2, 4, 7, 9 };
static const int kMinorPentIntervals[]    = { 0, 3, 5, 7, 10 };

static bool IntervalInSet(int interval, const int* set, int count)
{
    for (int i = 0; i < count; ++i) if (set[i] == interval) return true;
    return false;
}

bool PianoRollWidget::InScale(int pitch) const
{
    if (scaleType_ == ScaleType::Chromatic) return true;
    int interval = ((pitch - scaleRoot_) % 12 + 12) % 12;
    switch (scaleType_)
    {
        case ScaleType::Major:           return IntervalInSet(interval, kMajorIntervals, 7);
        case ScaleType::NaturalMinor:    return IntervalInSet(interval, kNaturalMinorIntervals, 7);
        case ScaleType::HarmonicMinor:   return IntervalInSet(interval, kHarmonicMinorIntervals, 7);
        case ScaleType::MajorPentatonic: return IntervalInSet(interval, kMajorPentIntervals, 5);
        case ScaleType::MinorPentatonic: return IntervalInSet(interval, kMinorPentIntervals, 5);
        default: return true;
    }
}

int PianoRollWidget::SnapPitchToScale(int pitch) const
{
    if (!snapToScaleEnabled_ || InScale(pitch)) return pitch;
    for (int d = 1; d <= 11; ++d)
    {
        if (pitch - d >= 0 && InScale(pitch - d)) return pitch - d;
        if (pitch + d < MAX_PITCH && InScale(pitch + d)) return pitch + d;
    }
    return pitch; // unreachable for any real scale within a 12-semitone search, but keep it safe
}

static int FindNoteIndexAt(const PianoRollTrack& track, int pitch, int step)
{
    for (int i = 0; i < (int)track.notes.size(); ++i)
    {
        const auto& n = track.notes[i];
        if (n.pitch == pitch && step >= n.startStep && step < n.startStep + n.length) return i;
    }
    return -1;
}

void PianoRollWidget::Draw(std::vector<Section>& sections, int editingSection, std::vector<int32_t>& arrangement,
                            std::vector<Instrument>& instruments, int selectedTrack,
                            Synth& synth, int playheadStep, bool showPlayhead, UndoManager& undo, bool& dirty)
{
    std::vector<PianoRollTrack>& tracks = sections[editingSection].tracks;
    PianoRollTrack& track = tracks[selectedTrack];
    Instrument& instrument = instruments[selectedTrack];
    bool isPlaying = showPlayhead;

    // switching tracks or sections drops the selection -- a selection full
    // of stale indices into different notes would be actively misleading
    if (selectedTrack != lastSelectedTrack_ || editingSection != lastEditingSection_)
    {
        selectedNotes_.clear();
        rubberBandActive_ = false;
        lastSelectedTrack_ = selectedTrack;
        lastEditingSection_ = editingSection;
    }
    // undo/redo, or any edit elsewhere, can shrink track.notes out from under
    // stale indices -- drop anything that's no longer valid
    selectedNotes_.erase(
        std::remove_if(selectedNotes_.begin(), selectedNotes_.end(),
                        [&](int i) { return i < 0 || i >= (int)track.notes.size(); }),
        selectedNotes_.end());

    auto isSelected = [&](int idx)
    {
        return std::find(selectedNotes_.begin(), selectedNotes_.end(), idx) != selectedNotes_.end();
    };

    ImGui::Begin("Piano Roll");
    ImGui::TextDisabled("Drag empty space to create a note; Ctrl+drag to rubber-band select. Drag a note to move it, its right edge to resize. Right-click to delete.");
    ImGui::TextDisabled("Ctrl+scroll zooms horizontally, Shift+scroll zooms vertically, plain scroll pans pitch.");
    ImGui::Text("Editing: %s", instrument.name.c_str());
    if (!selectedNotes_.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d note%s selected)", (int)selectedNotes_.size(), selectedNotes_.size() == 1 ? "" : "s");

        // Velocity slider for the selection -- shows the first selected
        // note's velocity, and on change (drag release) applies the new
        // value to every selected note at once.
        int velocity = track.notes[selectedNotes_.front()].velocity;
        ImGui::SetNextItemWidth(160);
        bool velocityChanged = ImGui::SliderInt("Velocity", &velocity, 1, 127);
        if (ImGui::IsItemActivated())
        {
            undo.PushUndo(instruments, sections, arrangement);
            dirty = true;
        }
        if (velocityChanged)
        {
            velocity = std::clamp(velocity, 1, 127);
            for (int idx : selectedNotes_)
                if (idx >= 0 && idx < (int)track.notes.size())
                    track.notes[idx].velocity = (uint8_t)velocity;
        }
    }

    ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("Zoom X", &zoomX_, 0.5f, 3.0f, "%.1fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::SliderFloat("Zoom Y", &zoomY_, 0.6f, 2.0f, "%.1fx");

    ImGui::Checkbox("Snap to Scale", &snapToScaleEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    static const char* kRootNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    ImGui::Combo("Root", &scaleRoot_, kRootNames, 12);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    static const char* kScaleNames[] = { "Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Major Pentatonic", "Minor Pentatonic" };
    int scaleTypeIndex = (int)scaleType_;
    if (ImGui::Combo("Scale", &scaleTypeIndex, kScaleNames, 6)) scaleType_ = (ScaleType)scaleTypeIndex;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    int totalSteps = track.totalSteps;
    int stepsPerBeat = std::max(1, track.stepsPerBeat); // for beat-group shading below, stays correct across grid-resolution changes
    int visible = visiblePitches_;
    float cellW = CELL_W_BASE * zoomX_;
    float cellH = CELL_H_BASE * zoomY_;
    bool showScaleHighlight = (scaleType_ != ScaleType::Chromatic);

    ImVec2 size(KEYS_W + cellW * totalSteps, cellH * visible);
    ImGui::InvisibleButton("piano_roll_canvas", size);
    bool hovered = ImGui::IsItemHovered();
    bool pianoRollFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImVec2 mouse = ImGui::GetIO().MousePos;
    ImGuiIO& io = ImGui::GetIO();

    if (hovered)
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            if (io.KeyCtrl)
            {
                zoomX_ = std::clamp(zoomX_ + wheel * 0.1f, 0.5f, 3.0f);
            }
            else if (io.KeyShift)
            {
                zoomY_ = std::clamp(zoomY_ + wheel * 0.1f, 0.6f, 2.0f);
            }
            else
            {
                pitchScrollOffset_ += (int)wheel;
                pitchScrollOffset_ = std::clamp(pitchScrollOffset_, 0, MAX_PITCH - visible);
            }
        }
    }

    // ---- background grid + piano keys ----
    for (int row = 0; row < visible; ++row)
    {
        int pitch = pitchScrollOffset_ + (visible - 1 - row); // top row = highest pitch
        float y = origin.y + row * cellH;
        bool inScale = InScale(pitch);

        ImVec2 keyMin(origin.x, y);
        ImVec2 keyMax(origin.x + KEYS_W, y + cellH);
        int pc = ((pitch % 12) + 12) % 12;
        // key colors tinted slightly blue-gray to match the app's recolored
        // theme, same rects/sizes as before -- just a palette swap
        ImU32 keyColor = kIsBlackKey[pc] ? IM_COL32(32, 36, 42, 255) : IM_COL32(224, 228, 232, 255);
        drawList->AddRectFilled(keyMin, keyMax, keyColor);
        if (showScaleHighlight && inScale)
            drawList->AddRectFilled(keyMin, keyMax, IM_COL32(80, 200, 120, 60)); // tint keys that belong to the chosen scale
        drawList->AddRect(keyMin, keyMax, IM_COL32(14, 16, 20, 255));

        if (pc == 0)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "C%d", pitch / 12);
            drawList->AddText(ImVec2(keyMin.x + 4, keyMin.y + 1), IM_COL32(150, 158, 168, 255), buf);
        }

        bool overKey = hovered && mouse.x >= keyMin.x && mouse.x < keyMax.x && mouse.y >= keyMin.y && mouse.y < keyMax.y;
        if (overKey && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            heldPreviewPitch_ = pitch;
            synth.NoteOn(PREVIEW_CHANNEL, NoteToFrequency((int8_t)pitch), &instrument);
        }

        for (int step = 0; step < totalSteps; ++step)
        {
            float x = origin.x + KEYS_W + step * cellW;
            ImVec2 cMin(x, y);
            ImVec2 cMax(x + cellW, y + cellH);

            ImU32 bg = ((step / stepsPerBeat) % 2 == 0) ? IM_COL32(28, 32, 38, 255) : IM_COL32(36, 41, 48, 255);
            drawList->AddRectFilled(cMin, cMax, bg);
            if (showScaleHighlight && inScale)
                drawList->AddRectFilled(cMin, cMax, IM_COL32(80, 200, 120, 28)); // subtle overlay so in-scale rows read as "eligible"
            drawList->AddRect(cMin, cMax, IM_COL32(16, 18, 22, 255));
        }
    }

    // ---- ghost notes: other tracks' notes, drawn faint, purely for reference ----
    for (int t = 0; t < (int)tracks.size(); ++t)
    {
        if (t == selectedTrack) continue;
        for (const auto& n : tracks[t].notes)
        {
            if (n.pitch < pitchScrollOffset_ || n.pitch >= pitchScrollOffset_ + visible) continue;

            int row = (visible - 1) - (n.pitch - pitchScrollOffset_);
            float x = origin.x + KEYS_W + n.startStep * cellW;
            float y = origin.y + row * cellH;

            ImVec2 nMin(x + 1, y + 1);
            ImVec2 nMax(x + n.length * cellW - 1, y + cellH - 1);
            drawList->AddRectFilled(nMin, nMax, IM_COL32(200, 200, 200, 35));
            drawList->AddRect(nMin, nMax, IM_COL32(200, 200, 200, 75));
        }
    }

    // ---- note create/select/move/resize/delete ----
    int hoverStep = (int)((mouse.x - origin.x - KEYS_W) / cellW);
    int hoverRow = (int)((mouse.y - origin.y) / cellH);
    int hoverPitch = pitchScrollOffset_ + (visible - 1 - hoverRow);
    bool withinGrid = hovered && hoverStep >= 0 && hoverStep < totalSteps && hoverPitch >= 0 && hoverPitch < MAX_PITCH;

    if (withinGrid && dragMode_ == DragMode::None && !rubberBandActive_ && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        int idx = FindNoteIndexAt(track, hoverPitch, hoverStep);
        if (idx != -1)
        {
            undo.PushUndo(instruments, sections, arrangement);
            dirty = true;

            if (isSelected(idx) && selectedNotes_.size() > 1)
            {
                std::vector<int> toDelete = selectedNotes_;
                std::sort(toDelete.rbegin(), toDelete.rend()); // erase back-to-front so indices stay valid
                for (int di : toDelete) track.notes.erase(track.notes.begin() + di);
            }
            else
            {
                track.notes.erase(track.notes.begin() + idx);
            }
            selectedNotes_.clear();
        }
    }

    if (dragMode_ == DragMode::None && !rubberBandActive_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        int idx = withinGrid ? FindNoteIndexAt(track, hoverPitch, hoverStep) : -1;

        if (withinGrid && idx == -1 && io.KeyCtrl)
        {
            // Ctrl+drag on empty space starts a rubber-band selection
            rubberBandActive_ = true;
            rubberBandStartX_ = mouse.x;
            rubberBandStartY_ = mouse.y;
            if (!io.KeyShift) selectedNotes_.clear();
        }
        else if (idx != -1 && io.KeyCtrl)
        {
            // toggle membership only -- doesn't start a drag
            auto it = std::find(selectedNotes_.begin(), selectedNotes_.end(), idx);
            if (it != selectedNotes_.end()) selectedNotes_.erase(it);
            else selectedNotes_.push_back(idx);
        }
        else if (idx != -1)
        {
            // clicking a note that's already part of a multi-selection keeps
            // the whole selection (so the drag moves the group); otherwise
            // this note becomes the sole selection
            if (!isSelected(idx)) selectedNotes_ = { idx };

            undo.PushUndo(instruments, sections, arrangement);
            dirty = true;

            float noteXMax = origin.x + KEYS_W + (track.notes[idx].startStep + track.notes[idx].length) * cellW;
            dragMode_ = (mouse.x >= noteXMax - RESIZE_HANDLE_PX) ? DragMode::Resizing : DragMode::Moving;
            dragNoteIndex_ = idx;
            dragOriginalNote_ = track.notes[idx];
            dragStartStep_ = hoverStep;
            dragStartPitch_ = hoverPitch;

            dragGroupOriginals_.clear();
            for (int s : selectedNotes_) dragGroupOriginals_.push_back({ s, track.notes[s] });
        }
        else if (withinGrid)
        {
            undo.PushUndo(instruments, sections, arrangement);
            dirty = true;

            PlacedNote n;
            n.pitch = (int8_t)SnapPitchToScale(hoverPitch);
            n.startStep = hoverStep;
            n.length = 1;
            n.instrument = 0; // track index maps 1:1 to instrument index, see Sequencer
            track.notes.push_back(n);

            dragMode_ = DragMode::Creating;
            dragNoteIndex_ = (int)track.notes.size() - 1;
            dragOriginalNote_ = n;
            dragStartStep_ = hoverStep;
            dragStartPitch_ = hoverPitch;

            selectedNotes_ = { dragNoteIndex_ };
            dragGroupOriginals_.clear();
        }
    }

    if (dragMode_ != DragMode::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)
        && dragNoteIndex_ >= 0 && dragNoteIndex_ < (int)track.notes.size())
    {
        int step = std::clamp((int)((mouse.x - origin.x - KEYS_W) / cellW), 0, totalSteps - 1);
        int pitch = std::clamp(pitchScrollOffset_ + (visible - 1 - (int)((mouse.y - origin.y) / cellH)), 0, MAX_PITCH - 1);
        if (dragMode_ == DragMode::Moving) pitch = SnapPitchToScale(pitch);
        PlacedNote& n = track.notes[dragNoteIndex_];

        if (dragMode_ == DragMode::Creating)
        {
            int newLength = step - dragStartStep_ + 1;
            n.length = std::clamp(newLength, 1, totalSteps - n.startStep);
        }
        else if (dragMode_ == DragMode::Resizing)
        {
            int deltaStep = step - dragStartStep_;
            int newLength = dragOriginalNote_.length + deltaStep;
            n.length = std::clamp(newLength, 1, totalSteps - n.startStep);
        }
        else if (dragMode_ == DragMode::Moving)
        {
            int rawDeltaStep = step - dragStartStep_;
            int rawDeltaPitch = pitch - dragStartPitch_;

            // clamp the delta so every note in the group stays in range,
            // rather than clamping each note independently (which would
            // shear the group apart as soon as any one note hit an edge)
            int minDeltaStep = std::numeric_limits<int>::min(), maxDeltaStep = std::numeric_limits<int>::max();
            int minDeltaPitch = std::numeric_limits<int>::min(), maxDeltaPitch = std::numeric_limits<int>::max();
            for (const auto& g : dragGroupOriginals_)
            {
                minDeltaStep = std::max(minDeltaStep, -g.note.startStep);
                maxDeltaStep = std::min(maxDeltaStep, totalSteps - g.note.length - g.note.startStep);
                minDeltaPitch = std::max(minDeltaPitch, -(int)g.note.pitch);
                maxDeltaPitch = std::min(maxDeltaPitch, (MAX_PITCH - 1) - (int)g.note.pitch);
            }
            int deltaStep = std::clamp(rawDeltaStep, minDeltaStep, maxDeltaStep);
            int deltaPitch = std::clamp(rawDeltaPitch, minDeltaPitch, maxDeltaPitch);

            for (const auto& g : dragGroupOriginals_)
            {
                if (g.index < 0 || g.index >= (int)track.notes.size()) continue;
                PlacedNote& gn = track.notes[g.index];
                gn.startStep = g.note.startStep + deltaStep;
                gn.pitch = (int8_t)(g.note.pitch + deltaPitch);
            }
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        dragMode_ = DragMode::None;
        dragNoteIndex_ = -1;
        dragGroupOriginals_.clear();
    }

    // ---- rubber-band select ----
    if (rubberBandActive_)
    {
        ImVec2 rMin(std::min(rubberBandStartX_, mouse.x), std::min(rubberBandStartY_, mouse.y));
        ImVec2 rMax(std::max(rubberBandStartX_, mouse.x), std::max(rubberBandStartY_, mouse.y));
        drawList->AddRectFilled(rMin, rMax, IM_COL32(120, 170, 255, 60));
        drawList->AddRect(rMin, rMax, IM_COL32(120, 170, 255, 200));

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            for (int i = 0; i < (int)track.notes.size(); ++i)
            {
                const auto& n = track.notes[i];
                if (n.pitch < pitchScrollOffset_ || n.pitch >= pitchScrollOffset_ + visible) continue;

                int row = (visible - 1) - (n.pitch - pitchScrollOffset_);
                float nx0 = origin.x + KEYS_W + n.startStep * cellW;
                float nx1 = origin.x + KEYS_W + (n.startStep + n.length) * cellW;
                float ny0 = origin.y + row * cellH;
                float ny1 = ny0 + cellH;

                bool intersects = nx0 < rMax.x && nx1 > rMin.x && ny0 < rMax.y && ny1 > rMin.y;
                if (intersects && !isSelected(i)) selectedNotes_.push_back(i);
            }
            rubberBandActive_ = false;
        }
    }

    // ---- keyboard: delete / nudge / copy / paste / duplicate (only while this window has focus) ----
    if (pianoRollFocused)
    {
        if (!selectedNotes_.empty() && dragMode_ == DragMode::None)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
            {
                undo.PushUndo(instruments, sections, arrangement);
                dirty = true;
                std::vector<int> toDelete = selectedNotes_;
                std::sort(toDelete.rbegin(), toDelete.rend());
                for (int di : toDelete) track.notes.erase(track.notes.begin() + di);
                selectedNotes_.clear();
            }
            else
            {
                int dStep = 0, dPitch = 0;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  dStep = -1;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dStep = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    dPitch = 1;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  dPitch = -1;

                if (dStep != 0 || dPitch != 0)
                {
                    int minDeltaStep = std::numeric_limits<int>::min(), maxDeltaStep = std::numeric_limits<int>::max();
                    int minDeltaPitch = std::numeric_limits<int>::min(), maxDeltaPitch = std::numeric_limits<int>::max();
                    for (int idx : selectedNotes_)
                    {
                        const PlacedNote& n = track.notes[idx];
                        minDeltaStep = std::max(minDeltaStep, -n.startStep);
                        maxDeltaStep = std::min(maxDeltaStep, totalSteps - n.length - n.startStep);
                        minDeltaPitch = std::max(minDeltaPitch, -(int)n.pitch);
                        maxDeltaPitch = std::min(maxDeltaPitch, (MAX_PITCH - 1) - (int)n.pitch);
                    }
                    dStep = std::clamp(dStep, minDeltaStep, maxDeltaStep);
                    dPitch = std::clamp(dPitch, minDeltaPitch, maxDeltaPitch);

                    if (dStep != 0 || dPitch != 0)
                    {
                        undo.PushUndo(instruments, sections, arrangement);
                        dirty = true;
                        for (int idx : selectedNotes_)
                        {
                            PlacedNote& n = track.notes[idx];
                            n.startStep += dStep;
                            n.pitch = (int8_t)(n.pitch + dPitch);
                        }
                    }
                }
            }
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedNotes_.empty())
        {
            clipboard_.clear();
            for (int idx : selectedNotes_) clipboard_.push_back(track.notes[idx]);
        }
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboard_.empty())
        {
            undo.PushUndo(instruments, sections, arrangement);
            dirty = true;

            // paste keeping pitches/lengths/relative offsets, anchored so the
            // earliest-starting copied note lands on the playhead
            int minStart = std::numeric_limits<int>::max();
            for (const auto& n : clipboard_) minStart = std::min(minStart, (int)n.startStep);
            int shift = playheadStep - minStart;

            selectedNotes_.clear();
            for (const auto& n : clipboard_)
            {
                PlacedNote copy = n;
                copy.startStep = std::clamp(copy.startStep + shift, 0, std::max(0, totalSteps - copy.length));
                track.notes.push_back(copy);
                selectedNotes_.push_back((int)track.notes.size() - 1);
            }
        }
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && !selectedNotes_.empty())
        {
            undo.PushUndo(instruments, sections, arrangement);
            dirty = true;

            // classic tracker clone-and-nudge: duplicate lands immediately
            // after the selection, offset by the group's own span
            int minStart = std::numeric_limits<int>::max(), maxEnd = std::numeric_limits<int>::min();
            for (int idx : selectedNotes_)
            {
                const PlacedNote& n = track.notes[idx];
                minStart = std::min(minStart, n.startStep);
                maxEnd = std::max(maxEnd, n.startStep + n.length);
            }
            int span = maxEnd - minStart;

            std::vector<PlacedNote> toClone;
            for (int idx : selectedNotes_) toClone.push_back(track.notes[idx]);

            selectedNotes_.clear();
            for (const auto& n : toClone)
            {
                PlacedNote copy = n;
                copy.startStep = std::clamp(copy.startStep + span, 0, std::max(0, totalSteps - copy.length));
                track.notes.push_back(copy);
                selectedNotes_.push_back((int)track.notes.size() - 1);
            }
        }
    }

    // ---- draw notes on top ----
    for (int i = 0; i < (int)track.notes.size(); ++i)
    {
        const auto& n = track.notes[i];
        if (n.pitch < pitchScrollOffset_ || n.pitch >= pitchScrollOffset_ + visible) continue;

        int row = (visible - 1) - (n.pitch - pitchScrollOffset_);
        float x = origin.x + KEYS_W + n.startStep * cellW;
        float y = origin.y + row * cellH;

        ImVec2 nMin(x + 1, y + 1);
        ImVec2 nMax(x + n.length * cellW - 1, y + cellH - 1);
        bool selected = isSelected(i) || (dragMode_ != DragMode::None && i == dragNoteIndex_);

        // dim softer notes so velocity reads visually at a glance -- floor
        // at 0.35 so even a quiet note stays clearly visible/clickable
        float velScale = 0.35f + 0.65f * (n.velocity / 127.0f);
        ImU32 fillColor = selected
            ? IM_COL32(140, 200, 255, 255)
            : IM_COL32((int)(90 * velScale), (int)(170 * velScale), (int)(250 * velScale), 255);
        ImU32 borderColor = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 60, 120, 255);
        drawList->AddRectFilled(nMin, nMax, fillColor);
        drawList->AddRect(nMin, nMax, borderColor, 0.0f, 0, selected ? 2.0f : 1.0f);

        // little resize-handle hint on the right edge
        drawList->AddRectFilled(ImVec2(nMax.x - RESIZE_HANDLE_PX, nMin.y), nMax, IM_COL32(20, 60, 120, 120));
    }

    if (isPlaying)
    {
        float x = origin.x + KEYS_W + playheadStep * cellW;
        drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + visible * cellH), IM_COL32(255, 80, 80, 255), 2.0f);
    }

    if (heldPreviewPitch_ != -1 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        synth.NoteOff(PREVIEW_CHANNEL);
        heldPreviewPitch_ = -1;
    }

    ImGui::End();
}
