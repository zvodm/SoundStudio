#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder* -- used once to lay out the default Unity/Blender-style panel arrangement
#include "song/SongData.h"
#include "song/SongIO.h"
#include "song/InstrumentIO.h"
#include "song/PianoRollData.h"
#include "audio/Synth.h"
#include "audio/MusicUtils.h"
#include "audio/Sequencer.h"
#include "audio/AudioConfig.h"
#include "audio/WavExport.h"
#include "ui/PianoRollWidget.h"
#include "ui/Theme.h"
#include "ui/DefaultLayout.h"
#include "editor/UndoManager.h"
#include "app/AppPaths.h"
#include "plugin/PluginManager.h"
#include "plugin/PluginHost.h"
#include "audio/DefaultWaveforms.h"
#include "song/DefaultInstruments.h"
#include "song/DeployKit.h"
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>

static PianoRollTrack MakeTrack(int totalSteps, int stepsPerBeat)
{
    PianoRollTrack t;
    t.totalSteps = totalSteps;
    t.stepsPerBeat = stepsPerBeat;
    return t;
}

static Section MakeSection(const std::string& name, int trackCount, int totalSteps, int stepsPerBeat, float bpm)
{
    Section s;
    s.name = name;
    s.bpm = bpm;
    for (int i = 0; i < trackCount; ++i) s.tracks.push_back(MakeTrack(totalSteps, stepsPerBeat));
    return s;
}

// A small color-coded dot per category, so the Tracks list reads at a
// glance without having to open each instrument to see what it's for.
static ImU32 CategoryColor(InstrumentCategory category)
{
    switch (category)
    {
        case InstrumentCategory::Lead:       return IM_COL32(90, 170, 250, 255);  // blue
        case InstrumentCategory::Bass:       return IM_COL32(200, 100, 220, 255); // purple
        case InstrumentCategory::Percussion: return IM_COL32(230, 150, 60, 255);  // orange
        case InstrumentCategory::Pad:        return IM_COL32(100, 200, 140, 255); // green
        case InstrumentCategory::FX:         return IM_COL32(220, 90, 90, 255);   // red
        default:                             return IM_COL32(180, 180, 180, 255);
    }
}

int main(void)
{
    // Sets up (and, on a fresh machine/account, creates) the shared
    // SoundStudio working directory -- Waves/Plugins/Songs/Instruments
    // under the user's home dir -- before anything else touches those
    // paths (default Save/Load locations below, plugin loading).
    AppPaths::EnsureDirectories();

    // The bundled .ssip instrument library (pianos, organs, guitars,
    // brass, reeds, strings, mallets, a drum kit, ...) -- written once
    // into Instruments/, never overwriting a file that's already there.
    // See song/DefaultInstruments.h.
    WriteDefaultInstrumentsIfMissing();

    const int screenWidth = 600;
    const int screenHeight = 400;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "Sound Creator Studio");
    SetWindowMinSize(600, 400);
    MaximizeWindow(); // start filling the screen; the 600x400 above is just the un-maximized fallback size
    SetTargetFPS(60);

    InitAudioDevice();
    AudioStream stream = LoadAudioStream(44100, 32, 2); // 44.1kHz, float32, stereo (for per-track pan)
    SetAudioStreamCallback(stream, SynthAudioCallback);
    PlayAudioStream(stream);

    Synth synth;
    synth.Init(44100, TOTAL_VOICES); // NUM_VOICES for notes/preview + one extra reserved for the metronome
    SetActiveSynth(&synth);

    // Single-cycle wavetables for the bundled library's Custom-waveform
    // presets ("Organ", "Piano", "Trumpet", ...). Registered before any
    // plugin loads, so a user wave that reuses one of these names wins.
    RegisterBuiltinWaveforms();

    rlImGuiSetup(true);
    ApplyCustomTheme(); // SoundStudio's own look, overriding rlImGuiSetup's flat default dark theme

    // Modular panels, Unity/Blender-style: every ImGui::Begin()'d window
    // below (Transport, Tracks, Instrument, ...) can be dragged out, split,
    // snapped to an edge, or tabbed together with another panel. The chosen
    // layout is written into imgui.ini automatically (same file that
    // already remembers window positions) so it's restored next launch.
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    bool dockLayoutInitialized = false; // set up a sensible starting layout once, on the very first launch (no saved imgui.ini yet)

    // Ship the real, hand-arranged layout as the default: if this is a
    // brand-new instance (no imgui.ini next to the exe yet -- a fresh
    // checkout, a fresh machine, or someone deleted it), load the shipped
    // layout straight into ImGui *before* the first frame runs. That marks
    // settings as already loaded, so ImGui's usual "load imgui.ini from
    // disk on first frame" step is skipped -- which is exactly what we
    // want here, since there's no disk file to load yet. Once the app
    // actually runs, imgui.ini gets written out and takes over from then
    // on, remembering whatever the user rearranges from there. The
    // DockBuilder block further down stays only as a defensive fallback --
    // with this loaded, DockBuilderGetNode() already finds our docked
    // layout and skips rebuilding it from scratch.
    {
        FILE* existingIni = fopen("imgui.ini", "r");
        if (existingIni)
            fclose(existingIni);
        else
            ImGui::LoadIniSettingsFromMemory(kDefaultImGuiLayoutIni);
    }

    // The song is a list of reusable Sections (verse/chorus/intro/...), each
    // holding one PianoRollTrack per instrument -- tracks[i] within a
    // section is always played using instruments[i], parallel arrays, same
    // as before. `arrangement` is the sequence of section indices that
    // actually gets played, classic-tracker pattern/order style.
    //
    // `stepsPerBeat` (grid resolution) is one value shared by the whole
    // song; each section has its own length (tracks[0].totalSteps) and its
    // own tempo (Section::bpm), which is how tempo automation happens here
    // -- crossing into a new section during playback picks up that
    // section's tempo. `totalSteps` below is just the template length used
    // to seed brand new sections/tracks, not any particular section's
    // actual length.
    int totalSteps = 32;
    int stepsPerBeat = 4;

    std::vector<Instrument> instruments;
    std::vector<Section> sections;
    std::vector<int32_t> arrangement;

    Instrument firstInstrument;
    firstInstrument.name = "Lead";
    instruments.push_back(firstInstrument);

    sections.push_back(MakeSection("Main", 1, totalSteps, stepsPerBeat, 120.0f));
    arrangement.push_back(0);

    int selectedTrack = 0;
    int editingSection = 0;
    bool loopEditingSectionOnly = false; // Transport toggle: preview just the edited section instead of the whole arrangement

    PianoRollWidget pianoRoll;
    Sequencer sequencer;
    UndoManager undo;

    std::vector<float> scopeSamples; // reused every frame by the Instrument panel's live oscilloscope

    float masterVolume = 1.0f; // 0-1, applied on top of every track's own volume/pan
    bool dirty = false; // true whenever there are unsaved changes

    // Hand the plugin API a way to reach the editor's own state, so a
    // plugin can read and write the live instruments (the bundled
    // Instrument Editor does exactly that) with the same undo/dirty/voice-
    // safety behaviour the built-in panels have. Everything a plugin
    // touches goes through these -- see plugin/PluginHost.h.
    {
        PluginHost::Context hostContext;
        hostContext.instruments        = &instruments;
        hostContext.selectedInstrument = &selectedTrack;
        hostContext.pushUndo   = [&]() { undo.PushUndo(instruments, sections, arrangement); };
        hostContext.markDirty  = [&]() { dirty = true; };
        hostContext.resetSynth = [&]() { synth.Reset(); };
        PluginHost::Set(hostContext);
    }

    // Lua plugins (Wave Editor, Oscilloscope, Instrument Editor, and
    // anything the user drops into Plugins/ themselves) -- see
    // plugin/PluginManager.h. Loaded after the host context above, since a
    // plugin's Init() may already want to read the song.
    PluginManager pluginManager;
    pluginManager.LoadAll();

    char songNameBuf[64];
    strncpy(songNameBuf, "My Song", sizeof(songNameBuf) - 1);
    songNameBuf[sizeof(songNameBuf) - 1] = 0;

    static char pathBuf[256];
    snprintf(pathBuf, sizeof(pathBuf), "%s/my_song.ssng", AppPaths::SongsDir().c_str());
    std::string fileStatus;
    std::string presetStatus;
    std::string wavStatus;
    DeployResult deployResult; // last Deploy's outcome, shown under the button
    bool pendingLoadConfirm = false;

    bool metronomeEnabled = false;
    int countInBeatsIndex = 0; // index into kCountInOptions below
    static const char* kCountInLabels[] = { "Off", "1 beat", "2 beats", "4 beats" };
    static const int   kCountInValues[]  = { 0, 1, 2, 4 };

    // ---- Recent files: a short, persisted (recent_files.txt, next to the
    // exe) most-recently-used list, so you don't have to retype a path
    // every time you want to reopen something. ----
    std::vector<std::string> recentFiles;
    auto loadRecentFiles = [&]()
    {
        recentFiles.clear();
        FILE* f = fopen("recent_files.txt", "r");
        if (!f) return;
        char buf[256];
        while (fgets(buf, sizeof(buf), f))
        {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
            if (len > 0) recentFiles.push_back(buf);
        }
        fclose(f);
    };
    auto saveRecentFiles = [&]()
    {
        FILE* f = fopen("recent_files.txt", "w");
        if (!f) return;
        for (const auto& p : recentFiles) fprintf(f, "%s\n", p.c_str());
        fclose(f);
    };
    auto addRecentFile = [&](const std::string& path)
    {
        recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
        recentFiles.insert(recentFiles.begin(), path);
        constexpr size_t kMaxRecent = 8;
        if (recentFiles.size() > kMaxRecent) recentFiles.resize(kMaxRecent);
        saveRecentFiles();
    };
    loadRecentFiles();

    // ---- Auto-save: every AUTOSAVE_INTERVAL seconds, if there are unsaved
    // changes, silently save to a fixed side file (never the user's own
    // save path -- this is a safety net, not a substitute for Save). ----
    constexpr float AUTOSAVE_INTERVAL = 120.0f;
    constexpr const char* AUTOSAVE_PATH = "autosave.ssng";
    float autosaveTimer = 0.0f;
    std::string autosaveStatus;

    // Clamp the current selection after anything that can shrink/replace
    // sections or instruments (undo/redo, load, section delete).
    auto clampSelection = [&]()
    {
        if (sections.empty()) return; // shouldn't happen -- SaveSong/LoadSong/undo always keep at least one
        if (editingSection >= (int)sections.size()) editingSection = (int)sections.size() - 1;
        if (editingSection < 0) editingSection = 0;
        if (selectedTrack >= (int)sections[editingSection].tracks.size())
            selectedTrack = (int)sections[editingSection].tracks.size() - 1;
        if (selectedTrack < 0) selectedTrack = 0;
    };

    // Reused by both the Load button and the "discard changes?" popup's
    // confirm button, so both paths behave identically.
    auto doLoad = [&]()
    {
        Song loaded;
        bool valid = LoadSong(loaded, pathBuf) && !loaded.instruments.empty();

        // migrate a pre-arrangement (v1-v3) file: wrap its one flat track
        // list into a single "Main" section so nothing downstream has to
        // know the old format ever existed
        if (valid && loaded.sections.empty() && !loaded.tracks.empty())
        {
            Section mainSection;
            mainSection.name = "Main";
            mainSection.bpm = (loaded.bpm > 0) ? (float)loaded.bpm : 120.0f;
            mainSection.tracks = loaded.tracks;
            loaded.sections.push_back(mainSection);
            loaded.arrangement = { 0 };
        }

        valid = valid && !loaded.sections.empty() && loaded.instruments.size() == loaded.sections[0].tracks.size();

        if (valid)
        {
            sequencer.Stop();
            synth.Reset(); // avoid any Voice left pointing into the instruments vector we're about to replace

            instruments = loaded.instruments;
            sections = loaded.sections;
            arrangement = loaded.arrangement.empty() ? std::vector<int32_t>{ 0 } : loaded.arrangement;
            masterVolume = loaded.masterVolume;
            stepsPerBeat = (loaded.rowsPerBeat > 0) ? loaded.rowsPerBeat : stepsPerBeat;
            totalSteps = sections[0].tracks.empty() ? totalSteps : sections[0].tracks[0].totalSteps;
            editingSection = 0;
            selectedTrack = 0;

            strncpy(songNameBuf, loaded.name.c_str(), sizeof(songNameBuf) - 1);
            songNameBuf[sizeof(songNameBuf) - 1] = 0;

            undo.Clear(); // undoing past a freshly loaded song would be confusing
            dirty = false;
            fileStatus = "Loaded.";
            addRecentFile(pathBuf);
        }
        else
        {
            fileStatus = "Load failed (missing file, empty song, or a mismatched instrument/track count).";
        }
    };

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        sequencer.Update(sections, arrangement, editingSection, loopEditingSectionOnly, instruments, synth, dt, stepsPerBeat, metronomeEnabled);
        pluginManager.Update(dt);

        synth.SetMasterVolume(masterVolume);
        bool anySolo = std::any_of(instruments.begin(), instruments.end(), [](const Instrument& i) { return i.solo; });
        synth.SetAnySolo(anySolo);

        // ---- Auto-save: a periodic safety net, separate from the user's own Save/path ----
        if (dirty)
        {
            autosaveTimer += dt;
            if (autosaveTimer >= AUTOSAVE_INTERVAL)
            {
                autosaveTimer = 0.0f;
                Song autosaveSong;
                autosaveSong.name = songNameBuf;
                autosaveSong.bpm = (uint16_t)sections[editingSection].bpm;
                autosaveSong.rowsPerBeat = (uint8_t)stepsPerBeat;
                autosaveSong.masterVolume = masterVolume;
                autosaveSong.instruments = instruments;
                autosaveSong.sections = sections;
                autosaveSong.arrangement = arrangement;
                autosaveStatus = SaveSong(autosaveSong, AUTOSAVE_PATH)
                    ? "Auto-saved to " + std::string(AUTOSAVE_PATH)
                    : "Auto-save failed.";
            }
        }
        else
        {
            autosaveTimer = 0.0f; // no unsaved changes -- nothing to auto-save, reset so a fresh edit gets the full interval
        }

        // ---- Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) undo/redo ----
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !io.KeyShift)
        {
            sequencer.Stop();
            synth.Reset();
            undo.Undo(instruments, sections, arrangement);
            clampSelection();
            dirty = true;
        }
        else if (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))))
        {
            sequencer.Stop();
            synth.Reset();
            undo.Redo(instruments, sections, arrangement);
            clampSelection();
            dirty = true;
        }

        // ---- Spacebar play/stop, everywhere except while typing into a text field ----
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space))
        {
            sequencer.TogglePlay(kCountInValues[countInBeatsIndex]);
        }

        BeginDrawing();
        ClearBackground(Color{ 32, 32, 32, 255 }); // matches the ImGui theme's darkest background (Theme.cpp)

        rlImGuiBegin();

        // Unity/Blender-style docking: every panel below can be dragged out,
        // split, snapped to an edge, or tabbed with another panel. The
        // dockspace covers the whole window as an invisible "floor" that
        // panels snap onto; PassthruCentralNode lets our own ClearBackground
        // show through any part of it that isn't covered by a panel.
        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpaceOverViewport(dockspaceId, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);

        // First-ever launch (no imgui.ini yet, so the dockspace node doesn't
        // exist): lay out a sensible default arrangement. Once the user has
        // run the app and imgui.ini has saved a layout, this block is never
        // reached again -- their own arrangement (including anything they
        // dragged/snapped) is restored instead and never overwritten here.
        if (!dockLayoutInitialized)
        {
            dockLayoutInitialized = true;
            if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
            {
                ImGui::DockBuilderAddNode(dockspaceId, (ImGuiDockNodeFlags)((int)ImGuiDockNodeFlags_DockSpace | (int)ImGuiDockNodeFlags_PassthruCentralNode));
                ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

                ImGuiID center = dockspaceId;
                ImGuiID top    = ImGui::DockBuilderSplitNode(center, ImGuiDir_Up,   0.10f, nullptr, &center);
                ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
                ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
                ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
                ImGuiID leftBottom  = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
                ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);

                ImGui::DockBuilderDockWindow("Transport", top);
                ImGui::DockBuilderDockWindow("Tracks", left);
                ImGui::DockBuilderDockWindow("Instrument", leftBottom);
                ImGui::DockBuilderDockWindow("Arrangement", right);
                ImGui::DockBuilderDockWindow("File", rightBottom);
                ImGui::DockBuilderDockWindow("Piano Roll", center);
                ImGui::DockBuilderDockWindow("Timeline", bottom);

                ImGui::DockBuilderFinish(dockspaceId);
            }
        }

        // only draw the red playhead in the piano roll when the section
        // actually sounding right now is the one being edited -- otherwise
        // it'd be showing progress through a different section entirely
        bool showPlayhead = sequencer.IsPlaying() && sequencer.CurrentSectionIndex() == editingSection;
        pianoRoll.Draw(sections, editingSection, arrangement, instruments, selectedTrack, synth,
                       sequencer.CurrentStep(), showPlayhead, undo, dirty);

        // ---- Transport ----
        ImGui::Begin("Transport");
        if (ImGui::Button(sequencer.IsPlaying() ? "Stop" : "Play"))
        {
            sequencer.TogglePlay(kCountInValues[countInBeatsIndex]);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Loop Section", &loopEditingSectionOnly))
        {
            sequencer.Stop(); // switching modes mid-playback would otherwise leave a stale arrangement position
        }

        ImGui::SameLine();
        ImGui::Checkbox("Metronome", &metronomeEnabled);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("Count-in", &countInBeatsIndex, kCountInLabels, 4);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderFloat("BPM", &sections[editingSection].bpm, 60.0f, 240.0f, "%.0f")) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        {
            int totalStepsUi = sections[editingSection].tracks.empty() ? totalSteps : sections[editingSection].tracks[0].totalSteps;
            if (ImGui::InputInt("Length (steps)", &totalStepsUi, stepsPerBeat, stepsPerBeat * 4))
            {
                // clamp to at least one beat, and to a sane upper bound so the
                // piano roll canvas doesn't grow unusably wide
                totalStepsUi = std::clamp(totalStepsUi, stepsPerBeat, 1024);

                sequencer.Stop();
                for (auto& t : sections[editingSection].tracks)
                {
                    t.totalSteps = totalStepsUi;
                    // shrinking: drop notes that no longer start in range, and
                    // clip any note that now runs past the new end
                    for (int i = (int)t.notes.size() - 1; i >= 0; --i)
                    {
                        PlacedNote& n = t.notes[i];
                        if (n.startStep >= totalStepsUi) t.notes.erase(t.notes.begin() + i);
                        else if (n.startStep + n.length > totalStepsUi) n.length = totalStepsUi - n.startStep;
                    }
                }
                dirty = true;
            }
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        {
            // Grid resolution -- how many editing/playback steps per beat,
            // shared by every section. Changing it rescales every existing
            // note's position and length in every section proportionally
            // (and each section's own length), so the song still sounds the
            // same, just quantized to the new grid.
            static const char* kResolutionNames[] = { "1/8 notes", "1/16 notes", "1/32 notes", "1/8 triplets", "1/16 triplets" };
            static const int   kResolutionSteps[]  = { 2, 4, 8, 3, 6 };
            constexpr int kResolutionCount = 5;

            int resolutionIndex = 1; // default to 1/16 if stepsPerBeat doesn't match any preset (e.g. an old file)
            for (int i = 0; i < kResolutionCount; ++i)
                if (kResolutionSteps[i] == stepsPerBeat) { resolutionIndex = i; break; }

            if (ImGui::Combo("Grid", &resolutionIndex, kResolutionNames, kResolutionCount))
            {
                int newStepsPerBeat = kResolutionSteps[resolutionIndex];
                if (newStepsPerBeat != stepsPerBeat)
                {
                    undo.PushUndo(instruments, sections, arrangement);
                    sequencer.Stop();

                    double scale = (double)newStepsPerBeat / (double)stepsPerBeat;

                    for (auto& sec : sections)
                    {
                        if (sec.tracks.empty()) continue;
                        int secTotalSteps = sec.tracks[0].totalSteps;
                        int newSecTotalSteps = std::clamp((int)std::lround(secTotalSteps * scale), newStepsPerBeat, 1024);

                        for (auto& t : sec.tracks)
                        {
                            for (auto& n : t.notes)
                            {
                                int newStart = (int)std::lround(n.startStep * scale);
                                int newLen = std::max(1, (int)std::lround(n.length * scale));
                                n.startStep = std::clamp(newStart, 0, std::max(0, newSecTotalSteps - 1));
                                n.length = std::clamp(newLen, 1, newSecTotalSteps - n.startStep);
                            }
                            t.stepsPerBeat = newStepsPerBeat;
                            t.totalSteps = newSecTotalSteps;
                        }
                    }

                    stepsPerBeat = newStepsPerBeat;
                    dirty = true;
                }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!undo.CanUndo());
        if (ImGui::Button("Undo (Ctrl+Z)"))
        {
            sequencer.Stop();
            synth.Reset();
            undo.Undo(instruments, sections, arrangement);
            clampSelection();
            dirty = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!undo.CanRedo());
        if (ImGui::Button("Redo (Ctrl+Y)"))
        {
            sequencer.Stop();
            synth.Reset();
            undo.Redo(instruments, sections, arrangement);
            clampSelection();
            dirty = true;
        }
        ImGui::EndDisabled();
        if (dirty)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(unsaved changes)");
        }

        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Master Volume", &masterVolume, 0.0f, 1.0f);
        if (anySolo)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(soloing -- non-soloed tracks are silent)");
        }
        ImGui::End();

        // ---- Track list ----
        ImGui::Begin("Tracks");
        for (int i = 0; i < (int)instruments.size(); ++i)
        {
            ImGui::PushID(i);

            Instrument& rowInst = instruments[i];
            bool muted = rowInst.muted;
            bool solo = rowInst.solo;

            // small per-row mute/solo toggles, so you don't have to select a
            // track just to silence or isolate it
            if (muted) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 70, 70, 255));
            if (ImGui::SmallButton("M"))
            {
                undo.PushUndo(instruments, sections, arrangement);
                rowInst.muted = !rowInst.muted;
                dirty = true;
            }
            if (muted) ImGui::PopStyleColor();

            ImGui::SameLine();
            if (solo) ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(210, 180, 60, 255));
            if (ImGui::SmallButton("S"))
            {
                undo.PushUndo(instruments, sections, arrangement);
                rowInst.solo = !rowInst.solo;
                dirty = true;
            }
            if (solo) ImGui::PopStyleColor();

            ImGui::SameLine();
            {
                // small category-colored dot, drawn inline before the name
                ImVec2 dotCenter = ImGui::GetCursorScreenPos();
                dotCenter.x += 6.0f;
                dotCenter.y += ImGui::GetTextLineHeight() * 0.5f;
                ImGui::GetWindowDrawList()->AddCircleFilled(dotCenter, 5.0f, CategoryColor(rowInst.category));
                ImGui::Dummy(ImVec2(14.0f, 1.0f));
            }
            ImGui::SameLine();
            bool isSelected = (i == selectedTrack);
            if (ImGui::Selectable(rowInst.name.c_str(), isSelected))
            {
                selectedTrack = i;
            }

            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Add Track"))
        {
            sequencer.Stop();
            synth.Reset();
            undo.PushUndo(instruments, sections, arrangement);

            Instrument newInst;
            newInst.name = "Track " + std::to_string(instruments.size() + 1);
            instruments.push_back(newInst);

            // every section needs a matching new track, each at that
            // section's own length/resolution -- sections can differ in length
            for (auto& sec : sections)
            {
                int secTotalSteps = sec.tracks.empty() ? totalSteps : sec.tracks[0].totalSteps;
                int secStepsPerBeat = sec.tracks.empty() ? stepsPerBeat : sec.tracks[0].stepsPerBeat;
                sec.tracks.push_back(MakeTrack(secTotalSteps, secStepsPerBeat));
            }

            selectedTrack = (int)instruments.size() - 1;
            dirty = true;
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(instruments.size() <= 1);
        if (ImGui::Button("Remove Selected"))
        {
            sequencer.Stop();
            synth.Reset();
            undo.PushUndo(instruments, sections, arrangement);

            instruments.erase(instruments.begin() + selectedTrack);
            for (auto& sec : sections)
                if (selectedTrack < (int)sec.tracks.size())
                    sec.tracks.erase(sec.tracks.begin() + selectedTrack);

            if (selectedTrack >= (int)instruments.size())
                selectedTrack = (int)instruments.size() - 1;
            dirty = true;
        }
        ImGui::EndDisabled();
        ImGui::End();

        // ---- Selected instrument's settings ----
        ImGui::Begin("Instrument");
        Instrument& instrument = instruments[selectedTrack];

        char nameBuf[64];
        strncpy(nameBuf, instrument.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = 0;
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        {
            instrument.name = nameBuf;
            dirty = true;
        }
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        int waveformIndex = (int)instrument.waveform;
        const char* waveformNames[] = { "Square", "Triangle", "Sawtooth", "Sine", "Noise", "Custom" };
        if (ImGui::Combo("Waveform", &waveformIndex, waveformNames, 6))
        {
            instrument.waveform = (WaveformType)waveformIndex;
            dirty = true;
        }
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (instrument.waveform == WaveformType::Square)
        {
            if (ImGui::SliderFloat("Duty Cycle", &instrument.dutyCycle, 0.05f, 0.95f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);
        }

        if (instrument.waveform == WaveformType::Custom)
        {
            char customBuf[64];
            strncpy(customBuf, instrument.customWaveform.c_str(), sizeof(customBuf) - 1);
            customBuf[sizeof(customBuf) - 1] = 0;
            if (ImGui::InputText("Custom Name", customBuf, sizeof(customBuf)))
            {
                instrument.customWaveform = customBuf;
                dirty = true;
            }
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            if (!FindCustomWaveform(instrument.customWaveform))
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "No wave registered under this name yet -- open the Wave Editor plugin.");
        }

        // Live oscilloscope (item 30): shows what this exact instrument is
        // actually outputting right now -- hold a piano roll key or hit
        // Play to see it move. Flat at zero when it isn't sounding.
        synth.SetScopeInstrument(&instrument);
        synth.GetScopeSamples(scopeSamples);
        if (!scopeSamples.empty())
        {
            ImGui::PlotLines("##scope", scopeSamples.data(), (int)scopeSamples.size(), 0,
                              "Oscilloscope", -1.0f, 1.0f, ImVec2(260, 80));
        }

        if (ImGui::SliderFloat("Volume", &instrument.volume, 0.0f, 1.0f)) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (ImGui::SliderFloat("Pan", &instrument.pan, -1.0f, 1.0f, "%.2f")) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        // bind Checkbox to a local copy so we can push the pre-toggle undo
        // snapshot before applying the change, not after -- otherwise Undo
        // would restore the already-toggled state and appear to do nothing
        bool mutedLocal = instrument.muted;
        if (ImGui::Checkbox("Mute", &mutedLocal))
        {
            undo.PushUndo(instruments, sections, arrangement);
            instrument.muted = mutedLocal;
            dirty = true;
        }
        ImGui::SameLine();
        bool soloLocal = instrument.solo;
        if (ImGui::Checkbox("Solo", &soloLocal))
        {
            undo.PushUndo(instruments, sections, arrangement);
            instrument.solo = soloLocal;
            dirty = true;
        }

        if (ImGui::SliderFloat("Attack", &instrument.envelope.attack, 0.0f, 1.0f)) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (ImGui::SliderFloat("Decay", &instrument.envelope.decay, 0.0f, 1.0f)) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (ImGui::SliderFloat("Sustain", &instrument.envelope.sustain, 0.0f, 1.0f)) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (ImGui::SliderFloat("Release", &instrument.envelope.release, 0.0f, 1.0f)) dirty = true;
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        ImGui::Separator();

        int categoryIndex = (int)instrument.category;
        const char* categoryNames[] = { "Lead", "Bass", "Percussion", "Pad", "FX" };
        if (ImGui::Combo("Category", &categoryIndex, categoryNames, 5))
        {
            instrument.category = (InstrumentCategory)categoryIndex;
            dirty = true;
        }
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        ImGui::SeparatorText("Arpeggiator");
        int arpIndex = (int)instrument.arpPattern;
        const char* arpNames[] = { "Off", "Octave", "Major Chord", "Minor Chord" };
        if (ImGui::Combo("Pattern", &arpIndex, arpNames, 4))
        {
            instrument.arpPattern = (ArpPattern)arpIndex;
            dirty = true;
        }
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (instrument.arpPattern != ArpPattern::Off)
        {
            if (ImGui::SliderFloat("Arp Rate", &instrument.arpRate, 1.0f, 30.0f, "%.0f notes/s")) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);
        }

        ImGui::SeparatorText("Vibrato");
        bool vibratoLocal = instrument.vibratoEnabled;
        if (ImGui::Checkbox("Vibrato Enabled", &vibratoLocal))
        {
            undo.PushUndo(instruments, sections, arrangement);
            instrument.vibratoEnabled = vibratoLocal;
            dirty = true;
        }
        if (instrument.vibratoEnabled)
        {
            if (ImGui::SliderFloat("Vibrato Rate", &instrument.vibratoRate, 0.5f, 12.0f, "%.1f Hz")) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            if (ImGui::SliderFloat("Vibrato Depth", &instrument.vibratoDepth, 0.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);
        }

        ImGui::SeparatorText("Filter");
        int filterIndex = (int)instrument.filterType;
        const char* filterNames[] = { "None", "Low Pass", "High Pass" };
        if (ImGui::Combo("Filter Type", &filterIndex, filterNames, 3))
        {
            instrument.filterType = (FilterType)filterIndex;
            dirty = true;
        }
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (instrument.filterType != FilterType::None)
        {
            if (ImGui::SliderFloat("Cutoff", &instrument.filterCutoff, 0.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);
        }

        ImGui::SeparatorText("Second Envelope");
        int env2Index = (int)instrument.env2Target;
        const char* env2Names[] = { "None", "Pitch", "Filter Cutoff" };
        if (ImGui::Combo("Env2 Target", &env2Index, env2Names, 3))
        {
            instrument.env2Target = (Env2Target)env2Index;
            dirty = true;
        }
        if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

        if (instrument.env2Target != Env2Target::None)
        {
            if (ImGui::SliderFloat("Env2 Attack", &instrument.env2.attack, 0.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            if (ImGui::SliderFloat("Env2 Decay", &instrument.env2.decay, 0.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            if (ImGui::SliderFloat("Env2 Sustain", &instrument.env2.sustain, 0.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            if (ImGui::SliderFloat("Env2 Release", &instrument.env2.release, 0.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            // signed amount -- negative sweeps the target down instead of up
            if (ImGui::SliderFloat("Env2 Amount", &instrument.env2Amount, -1.0f, 1.0f)) dirty = true;
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);
        }

        ImGui::SeparatorText("Presets");
        static char presetPathBuf[256];
        if (presetPathBuf[0] == 0)
            snprintf(presetPathBuf, sizeof(presetPathBuf), "%s/instrument.ssip", AppPaths::InstrumentsDir().c_str());
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Preset Path", presetPathBuf, sizeof(presetPathBuf));
        if (ImGui::Button("Save Preset"))
        {
            presetStatus = SaveInstrumentPreset(instrument, presetPathBuf) ? "Preset saved." : "Preset save failed.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Preset"))
        {
            Instrument loadedPreset;
            if (LoadInstrumentPreset(loadedPreset, presetPathBuf))
            {
                undo.PushUndo(instruments, sections, arrangement);
                synth.Reset(); // avoid a Voice left pointing at the instrument we're about to overwrite
                std::string keepName = instrument.name; // keep the track's own name rather than the preset's
                instrument = loadedPreset;
                instrument.name = keepName;
                dirty = true;
                presetStatus = "Preset loaded.";
            }
            else
            {
                presetStatus = "Preset load failed.";
            }
        }
        if (!presetStatus.empty()) ImGui::TextDisabled("%s", presetStatus.c_str());

        ImGui::End();

        // ---- Arrangement: sections (reusable blocks) + the order they play in ----
        ImGui::Begin("Arrangement");
        ImGui::TextWrapped("Sections are reusable blocks of notes (verse/chorus/intro). The list below chains them into the actual song.");

        ImGui::SeparatorText("Sections");
        for (int i = 0; i < (int)sections.size(); ++i)
        {
            ImGui::PushID(i);
            bool isEditing = (i == editingSection);

            char sectionNameBuf[64];
            strncpy(sectionNameBuf, sections[i].name.c_str(), sizeof(sectionNameBuf) - 1);
            sectionNameBuf[sizeof(sectionNameBuf) - 1] = 0;
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputText("##name", sectionNameBuf, sizeof(sectionNameBuf)))
            {
                sections[i].name = sectionNameBuf;
                dirty = true;
            }
            if (ImGui::IsItemActivated()) undo.PushUndo(instruments, sections, arrangement);

            ImGui::SameLine();
            int stepsForLabel = sections[i].tracks.empty() ? 0 : sections[i].tracks[0].totalSteps;
            ImGui::TextDisabled("(%d steps, %.0f BPM)", stepsForLabel, sections[i].bpm);

            ImGui::SameLine();
            ImGui::BeginDisabled(isEditing);
            bool editClicked = ImGui::SmallButton("Edit");
            ImGui::EndDisabled();

            ImGui::SameLine();
            bool duplicateClicked = ImGui::SmallButton("Duplicate");

            ImGui::SameLine();
            ImGui::BeginDisabled(sections.size() <= 1);
            bool deleteClicked = ImGui::SmallButton("Delete");
            ImGui::EndDisabled();

            ImGui::PopID();

            // act on clicks after the ID/disabled scopes are closed, and
            // after we're done reading sections[i] by reference -- Duplicate
            // and Delete both mutate the `sections` vector, which can
            // reallocate and invalidate that reference
            if (editClicked)
            {
                editingSection = i;
            }
            else if (duplicateClicked)
            {
                undo.PushUndo(instruments, sections, arrangement);
                Section copy = sections[i];
                copy.name += " (copy)";
                sections.push_back(copy);
                dirty = true;
            }
            else if (deleteClicked)
            {
                undo.PushUndo(instruments, sections, arrangement);
                sections.erase(sections.begin() + i);

                // fix up the arrangement: drop any slot pointing at the
                // deleted section, shift the rest down to match
                for (int k = (int)arrangement.size() - 1; k >= 0; --k)
                {
                    if (arrangement[k] == i) arrangement.erase(arrangement.begin() + k);
                    else if (arrangement[k] > i) arrangement[k]--;
                }
                if (arrangement.empty()) arrangement.push_back(0); // never leave the arrangement empty while sections exist

                if (editingSection > i) editingSection--;
                if (editingSection >= (int)sections.size()) editingSection = (int)sections.size() - 1;

                dirty = true;
                ImGui::PopID();
                break; // sections[] was mutated -- stop iterating this frame, the panel redraws next frame
            }
        }
        clampSelection();

        if (ImGui::Button("New Section"))
        {
            sequencer.Stop();
            undo.PushUndo(instruments, sections, arrangement);
            int newTotalSteps = sections[editingSection].tracks.empty() ? totalSteps : sections[editingSection].tracks[0].totalSteps;
            float newBpm = sections[editingSection].bpm;
            sections.push_back(MakeSection("Section " + std::to_string(sections.size() + 1), (int)instruments.size(), newTotalSteps, stepsPerBeat, newBpm));
            dirty = true;
        }

        ImGui::SeparatorText("Arrangement (playback order)");
        for (int i = 0; i < (int)arrangement.size(); ++i)
        {
            ImGui::PushID(1000 + i);
            int secIdx = arrangement[i];
            const char* label = (secIdx >= 0 && secIdx < (int)sections.size()) ? sections[secIdx].name.c_str() : "(missing)";

            ImGui::Text("%d.", i + 1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo("##pick", label))
            {
                for (int s = 0; s < (int)sections.size(); ++s)
                {
                    bool isSelectedEntry = (s == secIdx);
                    if (ImGui::Selectable(sections[s].name.c_str(), isSelectedEntry))
                    {
                        undo.PushUndo(instruments, sections, arrangement);
                        arrangement[i] = s;
                        dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(i == 0);
            bool upClicked = ImGui::SmallButton("Up");
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(i == (int)arrangement.size() - 1);
            bool downClicked = ImGui::SmallButton("Down");
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled((int)arrangement.size() <= 1);
            bool removeClicked = ImGui::SmallButton("Remove");
            ImGui::EndDisabled();

            ImGui::PopID();

            if (upClicked)
            {
                undo.PushUndo(instruments, sections, arrangement);
                std::swap(arrangement[i], arrangement[i - 1]);
                dirty = true;
            }
            else if (downClicked)
            {
                undo.PushUndo(instruments, sections, arrangement);
                std::swap(arrangement[i], arrangement[i + 1]);
                dirty = true;
            }
            else if (removeClicked)
            {
                undo.PushUndo(instruments, sections, arrangement);
                arrangement.erase(arrangement.begin() + i);
                dirty = true;
                break; // arrangement[] was mutated -- stop iterating this frame
            }
        }

        if (ImGui::Button("Append Section to Arrangement"))
        {
            undo.PushUndo(instruments, sections, arrangement);
            arrangement.push_back(editingSection);
            dirty = true;
        }

        if (sequencer.IsPlaying() && !loopEditingSectionOnly)
        {
            int playingSection = sequencer.CurrentSectionIndex();
            if (playingSection >= 0 && playingSection < (int)sections.size())
                ImGui::TextDisabled("Now playing: %s (step %d)", sections[playingSection].name.c_str(), sequencer.CurrentStep());
        }

        ImGui::End();

        // ---- Timeline: zoomed-out, DAW-style view of the whole arrangement
        // across every track at once (item 31) -- one horizontal block per
        // arrangement entry, sized by that section's actual duration
        // (steps / its own tempo, so tempo automation is reflected), with a
        // row per instrument showing that section's notes as small ticks.
        // Read-mostly: click a block to jump the piano roll to that section. ----
        ImGui::Begin("Timeline");
        static float timelinePixelsPerSecond = 40.0f;
        ImGui::SetNextItemWidth(160);
        ImGui::SliderFloat("Zoom", &timelinePixelsPerSecond, 10.0f, 150.0f, "%.0f px/s");

        if (arrangement.empty() || instruments.empty())
        {
            ImGui::TextDisabled("Nothing to show yet -- add an instrument and a section to the arrangement.");
        }
        else
        {
            constexpr float ROW_HEIGHT = 22.0f;
            constexpr float HEADER_HEIGHT = 20.0f;
            constexpr float TRACK_LABEL_W = 90.0f;
            constexpr int TIMELINE_MAX_PITCH = 96; // matches the piano roll's own pitch range
            float contentHeight = HEADER_HEIGHT + ROW_HEIGHT * instruments.size();

            struct TimelineBlock { int sectionIdx; float x; float width; };
            std::vector<TimelineBlock> blocks;
            float cursorX = 0.0f;
            for (int32_t secIdx : arrangement)
            {
                float durationSeconds = 0.0f;
                if (secIdx >= 0 && secIdx < (int)sections.size() && !sections[secIdx].tracks.empty() && sections[secIdx].bpm > 0.0f)
                {
                    int blockTotalSteps = sections[secIdx].tracks[0].totalSteps;
                    durationSeconds = blockTotalSteps * 60.0f / sections[secIdx].bpm / (float)stepsPerBeat;
                }
                float width = std::max(4.0f, durationSeconds * timelinePixelsPerSecond);
                blocks.push_back({ secIdx, cursorX, width });
                cursorX += width;
            }
            float totalWidth = cursorX;

            // fixed (non-scrolling) track-name column, doubling as a
            // shortcut to select that track elsewhere in the UI
            ImGui::BeginChild("timeline_labels", ImVec2(TRACK_LABEL_W, contentHeight + 16), false);
            ImGui::Dummy(ImVec2(1, HEADER_HEIGHT));
            for (int t = 0; t < (int)instruments.size(); ++t)
            {
                ImGui::PushID(3000 + t);
                if (ImGui::Selectable(instruments[t].name.c_str(), t == selectedTrack, 0, ImVec2(TRACK_LABEL_W - 4, ROW_HEIGHT - 2)))
                    selectedTrack = t;
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("timeline_scroll", ImVec2(0, contentHeight + 20), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 origin = ImGui::GetCursorScreenPos();

            // single canvas-wide invisible button + manual hit-testing,
            // same pattern the piano roll uses, rather than one InvisibleButton
            // per block (simpler ID/layout management for a dynamic block count)
            ImGui::InvisibleButton("timeline_canvas", ImVec2(std::max(1.0f, totalWidth), contentHeight));
            bool timelineClicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            float mouseX = ImGui::GetIO().MousePos.x;

            static const ImU32 kBlockColors[] = {
                IM_COL32(90, 120, 180, 200), IM_COL32(150, 100, 180, 200), IM_COL32(90, 160, 140, 200),
                IM_COL32(180, 140, 90, 200), IM_COL32(160, 90, 120, 200), IM_COL32(110, 150, 90, 200)
            };

            for (const auto& blk : blocks)
            {
                bool valid = blk.sectionIdx >= 0 && blk.sectionIdx < (int)sections.size();
                float bx = origin.x + blk.x;

                ImVec2 headerMin(bx, origin.y);
                ImVec2 headerMax(bx + blk.width, origin.y + HEADER_HEIGHT);
                bool isPlayingBlock = sequencer.IsPlaying() && valid && sequencer.CurrentSectionIndex() == blk.sectionIdx;
                ImU32 headerColor = kBlockColors[valid ? blk.sectionIdx % 6 : 0];

                drawList->AddRectFilled(headerMin, headerMax, headerColor);
                drawList->AddRect(headerMin, headerMax, isPlayingBlock ? IM_COL32(255, 220, 100, 255) : IM_COL32(20, 20, 20, 255),
                                   0.0f, 0, isPlayingBlock ? 2.5f : 1.0f);
                if (valid && blk.width > 24.0f)
                    drawList->AddText(ImVec2(bx + 3, origin.y + 3), IM_COL32(255, 255, 255, 255), sections[blk.sectionIdx].name.c_str());

                if (timelineClicked && valid && mouseX >= bx && mouseX < bx + blk.width)
                    editingSection = blk.sectionIdx;

                if (!valid) continue;

                const Section& sec = sections[blk.sectionIdx];
                int blockTotalSteps = sec.tracks.empty() ? 0 : sec.tracks[0].totalSteps;

                for (int t = 0; t < (int)instruments.size() && t < (int)sec.tracks.size(); ++t)
                {
                    float rowY = origin.y + HEADER_HEIGHT + t * ROW_HEIGHT;
                    drawList->AddRect(ImVec2(bx, rowY), ImVec2(bx + blk.width, rowY + ROW_HEIGHT), IM_COL32(60, 60, 60, 255));

                    if (blockTotalSteps <= 0) continue;
                    for (const auto& n : sec.tracks[t].notes)
                    {
                        float nx0 = bx + (float)n.startStep / blockTotalSteps * blk.width;
                        float nx1 = bx + (float)(n.startStep + n.length) / blockTotalSteps * blk.width;
                        float pitchFrac = std::clamp((float)n.pitch / (float)TIMELINE_MAX_PITCH, 0.0f, 1.0f);
                        float ny = rowY + (1.0f - pitchFrac) * (ROW_HEIGHT - 3.0f);
                        drawList->AddRectFilled(ImVec2(nx0, ny), ImVec2(std::max(nx0 + 1.0f, nx1), ny + 3.0f), IM_COL32(140, 200, 255, 220));
                    }
                }
            }

            ImGui::EndChild();
        }
        ImGui::End();

        // ---- File: save/load the whole session (all sections + instruments) ----
        ImGui::Begin("File");

        ImGui::InputText("Song Name", songNameBuf, sizeof(songNameBuf));
        ImGui::InputText("Path", pathBuf, sizeof(pathBuf));

        // Shared by the Load button and Recent Files entries -- both need
        // the same "warn if there are unsaved changes" gate before
        // overwriting the current session.
        auto requestLoad = [&]()
        {
            if (dirty)
            {
                pendingLoadConfirm = true;
                ImGui::OpenPopup("Unsaved Changes");
            }
            else
            {
                doLoad();
            }
        };

        if (ImGui::Button("Save"))
        {
            Song song;
            song.name = songNameBuf;
            song.bpm = (uint16_t)sections[editingSection].bpm; // fallback tempo for older readers; each section carries its real tempo
            song.rowsPerBeat = (uint8_t)stepsPerBeat;
            song.masterVolume = masterVolume;
            song.instruments = instruments;
            song.sections = sections;
            song.arrangement = arrangement;
            // song.patterns / song.order / song.tracks left empty -- legacy
            // tracker-grid and pre-arrangement fields, superseded by
            // sections/arrangement above (still supported for loading old files).

            if (SaveSong(song, pathBuf))
            {
                fileStatus = "Saved.";
                dirty = false;
                addRecentFile(pathBuf);
            }
            else
            {
                fileStatus = "Save failed.";
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            requestLoad();
        }

        if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("You have unsaved changes.");
            ImGui::Text("Load anyway and discard them?");
            ImGui::Separator();
            if (ImGui::Button("Load Anyway"))
            {
                doLoad();
                pendingLoadConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pendingLoadConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        (void)pendingLoadConfirm; // only used to gate opening the popup once

        if (!fileStatus.empty()) ImGui::Text("%s", fileStatus.c_str());
        if (!autosaveStatus.empty()) ImGui::TextDisabled("%s", autosaveStatus.c_str());

        if (!recentFiles.empty())
        {
            ImGui::SeparatorText("Recent Files");
            for (int i = 0; i < (int)recentFiles.size(); ++i)
            {
                ImGui::PushID(2000 + i);
                if (ImGui::Selectable(recentFiles[i].c_str()))
                {
                    strncpy(pathBuf, recentFiles[i].c_str(), sizeof(pathBuf) - 1);
                    pathBuf[sizeof(pathBuf) - 1] = 0;
                    requestLoad();
                }
                ImGui::PopID();
            }
        }

        ImGui::SeparatorText("Export");
        static char wavPathBuf[128] = "export.wav";
        ImGui::InputText("WAV Path", wavPathBuf, sizeof(wavPathBuf));
        if (ImGui::Button("Export WAV"))
        {
            wavStatus = ExportSongToWav(sections, arrangement, instruments, stepsPerBeat, wavPathBuf)
                ? "Exported."
                : "Export failed (empty arrangement or a section with no tracks?).";
        }
        if (!wavStatus.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", wavStatus.c_str());
        }

        // ---- Deploy: build a drop-in kit a game can actually ship ----
        //
        // Saving a .ssng isn't enough for a game, because a Custom
        // instrument's wavetable is stored only by NAME -- resolved against
        // a registry that soundstudio_player.cpp doesn't have (no builtin
        // waveforms, no Lua, no plugins). Deploy bakes the sample data
        // itself into a .ssbundle, so all the wavetable synthesis happens
        // here, once, and the game just reads floats at startup.
        ImGui::SeparatorText("Deploy");
        ImGui::TextWrapped("Bakes the song plus every custom wavetable it uses into a .ssbundle, "
                            "with the player runtime and a ready-to-include header, for dropping into a game.");

        static char deployDirBuf[256] = "deploy";
        ImGui::InputText("Output Folder", deployDirBuf, sizeof(deployDirBuf));

        if (ImGui::Button("Deploy Game Kit"))
        {
            Song deploySong;
            deploySong.name = songNameBuf;
            deploySong.bpm = (uint16_t)sections[editingSection].bpm;
            deploySong.rowsPerBeat = (uint8_t)stepsPerBeat;
            deploySong.masterVolume = masterVolume;
            deploySong.instruments = instruments;
            deploySong.sections = sections;
            deploySong.arrangement = arrangement;

            // Where to look for soundstudio_player.h/.cpp to copy into the
            // kit: the working directory and the executable's, plus their
            // parents -- which covers running from the repo root (run.bat)
            // and running build\SoundStudio.exe directly. Not finding them
            // only costs the kit those two files; README.txt says so.
            std::vector<std::string> playerSearchDirs = { ".", ".." };
            if (const char* appDir = GetApplicationDirectory())
            {
                playerSearchDirs.push_back(appDir);
                playerSearchDirs.push_back(std::string(appDir) + "..");
            }

            deployResult = DeployGameKit(deploySong, deployDirBuf, playerSearchDirs);
        }

        if (!deployResult.message.empty())
        {
            if (deployResult.ok && deployResult.missingWaves.empty())
                ImGui::TextWrapped("%s", deployResult.message.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", deployResult.message.c_str());

            // Name the instruments that will play as sine waves in the game,
            // rather than making this a surprise at runtime.
            for (const std::string& missing : deployResult.missingWaves)
                ImGui::BulletText("unresolved wavetable: %s", missing.c_str());
        }

        ImGui::End();

        // ---- Plugins: Wave Editor, Oscilloscope, and anything else the
        // user drops into Plugins/ -- each gets its own docked/floating
        // panel, drawn last so they layer on top of nothing in particular. ----
        pluginManager.DrawAll();

        rlImGuiEnd();
        EndDrawing();
    }

    PluginHost::Set(PluginHost::Context{}); // drop the lambdas capturing main()'s locals before they go out of scope
    pluginManager.Shutdown();
    rlImGuiShutdown();

    UnloadAudioStream(stream);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
