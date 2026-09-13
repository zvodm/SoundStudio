#pragma once
#include "song/PianoRollData.h"
#include "song/SongData.h"
#include "audio/Synth.h"
#include <vector>

// Drives a single shared playhead across whichever section is currently
// playing (track index i within that section plays using instruments[i] --
// tracks and instruments are parallel arrays, one instrument per track).
// Normally steps through the song's arrangement (a sequence of section
// indices), moving to the next section when the current one ends and
// wrapping back to the start when the arrangement finishes. With
// loopEditingSectionOnly set, ignores the arrangement entirely and just
// loops editingSection forever -- for previewing one section in isolation
// while you work on it.
//
// Each section carries its own tempo (Section::bpm), so crossing a section
// boundary during playback is how tempo automation happens here -- no
// separate per-step tempo curve needed.
//
// The piano roll widget only draws/edits; this is what actually steps time
// forward and triggers the synth.
class Sequencer
{
public:
    // countInBeats > 0 (only meaningful when this call is what starts
    // playback) delays the arrangement itself starting until that many
    // metronome clicks have played, e.g. a 1-bar-of-4 pre-roll -- lets you
    // get into position before the notes start.
    void TogglePlay(int countInBeats = 0);
    void Stop();
    bool IsPlaying() const { return playing_; }
    int  CurrentStep() const { return currentStep_; }

    // Which section is actually sounding right now, or -1 if none (e.g.
    // arrangement is empty). Use this to decide whether to draw the
    // playhead in the piano roll -- only meaningful while IsPlaying().
    int CurrentSectionIndex() const { return currentSectionIndex_; }

    // Call once per frame regardless of which section/track is visible in
    // the UI. metronomeEnabled clicks on every beat, both during count-in
    // and during normal playback.
    void Update(std::vector<Section>& sections,
                const std::vector<int32_t>& arrangement,
                int editingSection,
                bool loopEditingSectionOnly,
                std::vector<Instrument>& instruments,
                Synth& synth,
                float dt,
                int stepsPerBeat,
                bool metronomeEnabled);

private:
    Section* ResolveCurrentSection(std::vector<Section>& sections, const std::vector<int32_t>& arrangement,
                                    int editingSection, bool loopEditingSectionOnly);
    void StopAllNotes();
    void ConfigureMetronomeInstrument();
    void PlayClick(Synth& synth, bool accent);

    bool playing_ = false;
    float stepTimer_ = 0.0f;
    int currentStep_ = 0;
    int arrangementIndex_ = 0;
    int currentSectionIndex_ = -1;

    // Count-in: while > 0, playback is "on" (IsPlaying() is true, so the UI
    // shows Stop) but the arrangement itself hasn't started -- only clicks
    // play, once per beat, until this reaches 0.
    int countInBeatsRemaining_ = 0;
    float countInTimer_ = 0.0f;

    struct ActiveNote { int endStep; int channel; };
    std::vector<ActiveNote> activeNotes_;

    // A tiny fixed synth patch used only for metronome clicks (accented on
    // the first beat of a section/count-in, plain otherwise). Must stay
    // alive as long as a click might still be sounding, since Synth::Voice
    // holds a raw pointer into it.
    Instrument metronomeInstrument_;
    bool metronomeConfigured_ = false;

    Synth* synthRef_ = nullptr;
};
