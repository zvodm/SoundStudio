#pragma once
#include "song/SongData.h"
#include <string>

// Binary format, versioned so it can evolve without breaking old files.
// Magic: "SSNG", followed by a uint32 version number.

bool SaveSong(const Song& song, const std::string& path);
bool LoadSong(Song& outSong, const std::string& path);