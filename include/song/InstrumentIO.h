#pragma once
#include "song/SongData.h"
#include <cstdio>
#include <cstdint>
#include <string>

// Shared Instrument (de)serialization -- used both for whole Song files
// (SongIO.cpp) and standalone instrument presets below, which let you save
// and reuse a synth patch across different songs, independent of any one
// song file.

// `version` here means "which song-format version is this data associated
// with" (see SongIO.cpp) -- it gates which fields are present, so older
// song files missing newer instrument fields still load with sane
// defaults instead of misreading the rest of the file.
void WriteInstrument(FILE* f, const Instrument& inst);
bool ReadInstrument(FILE* f, Instrument& inst, uint32_t version);

// The instrument-field version presets are always written/read at -- kept
// equal to SongIO's current SONG_VERSION by convention, since presets share
// the exact same field set as instruments embedded in a song.
constexpr uint32_t CURRENT_INSTRUMENT_FIELD_VERSION = 6;

// Presets: one instrument, saved/loaded on its own (.ssip = Sound Studio
// Instrument Preset), independent of any song. Save it once, drag it into
// any track in any song via Load Preset.
bool SaveInstrumentPreset(const Instrument& inst, const std::string& path);
bool LoadInstrumentPreset(Instrument& outInst, const std::string& path);
