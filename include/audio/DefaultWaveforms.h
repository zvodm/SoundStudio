#pragma once

// ------------------------------------------------------------------
// Built-in wavetables.
//
// The oscillator's five analytic waveforms (square/triangle/saw/sine/noise)
// are great for chiptune but can't get anywhere near a piano, an organ or a
// trumpet -- those live or die on their harmonic spectrum. WaveformType::
// Custom already resamples an arbitrary single-cycle table by name (see
// audio/Synth.h's registry, populated at runtime by the Wave Editor
// plugin), so the cheapest possible way to get real instrument timbres is
// to additively synthesize a spectrum per instrument once at startup and
// register it under a well-known name.
//
// RegisterBuiltinWaveforms() does exactly that: ~45 single-cycle tables
// ("Organ", "Piano", "Trumpet", "Guitar Nylon", ...) registered before the
// first note can play. The bundled instrument presets (see
// song/DefaultInstruments.h) reference these names, so a preset sounds
// right on a fresh machine with no plugins installed and nothing for the
// user to set up.
//
// These are registered in the exact same registry the Wave Editor plugin
// writes to, so a user-defined wave that reuses one of these names simply
// replaces it for the rest of the session -- deliberately, that's the
// override path.
//
// Call once at startup, after the audio device exists and before
// PluginManager::LoadAll() (so a plugin registering the same name wins).
// ------------------------------------------------------------------
void RegisterBuiltinWaveforms();
