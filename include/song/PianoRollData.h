#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A note placed freely on the piano roll grid, FL-Studio style, as opposed
// to the fixed per-row tracker grid in SongData.h's Pattern. Both are valid
// ways to represent the same underlying idea (a note starting at some time,
// lasting some duration, played by some instrument) -- we can reconcile /
// convert between the two later once the editor UI direction settles.
struct PlacedNote
{
    int8_t  pitch;             // semitone offset from C0, matches Note::pitch elsewhere
    int     startStep;         // which grid step this note begins on
    int     length;            // duration in steps
    uint8_t instrument;        // index into Song::instruments
    uint8_t velocity = 127;    // 0-127, MIDI-style -- scales this note's amplitude
};

struct PianoRollTrack
{
    std::vector<PlacedNote> notes;
    int stepsPerBeat = 4;  // 4 = 16th-note grid resolution
    int totalSteps   = 32; // 32 steps = 2 bars at 4/4 with 16th-note steps
};

// A reusable block of notes across every track -- e.g. "Intro", "Verse",
// "Chorus". The song's arrangement (Song::arrangement) chains these together
// to build the actual playback timeline, classic-tracker style: edit a
// section once, reuse it anywhere in the arrangement.
//
// tracks[] is parallel to Song::instruments (instruments are shared/global
// across every section; only the notes are per-section). Every section is
// expected to carry exactly one PianoRollTrack per instrument, and every
// track within one section should share the same totalSteps/stepsPerBeat --
// that's what makes "how long is this section" and "what's the grid
// resolution" well defined for playback.
struct Section
{
    std::string name = "Section";
    std::vector<PianoRollTrack> tracks;
    float bpm = 120.0f; // this section's own tempo -- lets the arrangement speed up/slow down between sections
};