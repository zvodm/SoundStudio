#pragma once

// Voice 0 is reserved for live keyboard preview (clicking piano keys in the
// editor). Playback (the Sequencer) uses the remaining voices, round-robin,
// shared across all tracks -- so total simultaneous playing notes across
// every track is capped at NUM_VOICES - 1 for now. Raise this if you need
// more polyphony; each voice is cheap, this just isn't tuned yet.
static constexpr int NUM_VOICES = 8;
static constexpr int PREVIEW_CHANNEL = 0;

// One extra voice, dedicated to metronome clicks -- kept outside the
// NUM_VOICES range so it never competes with the round-robin note channels
// (1..NUM_VOICES-1) for polyphony. The live Synth must be Init'd with
// TOTAL_VOICES, not NUM_VOICES, so this channel actually exists.
static constexpr int METRONOME_CHANNEL = NUM_VOICES;
static constexpr int TOTAL_VOICES = NUM_VOICES + 1;