#pragma once
#include "song/SongData.h"
#include <string>
#include <vector>
#include <cstdint>

// Offline (non-realtime) render of the whole arrangement -- every section
// in playback order, once through -- to a standard 16-bit PCM stereo WAV
// file. This is item 29: a convenience for getting a real shippable audio
// asset out of the live .ssng data without needing a separate DAW; the
// .ssng file itself stays the actual source of truth/shipped asset.
//
// Uses its own internal Synth instance, so exporting never disturbs
// whatever the live playback Synth (and the audio callback reading it) is
// doing at the time.
bool ExportSongToWav(const std::vector<Section>& sections,
                      const std::vector<int32_t>& arrangement,
                      const std::vector<Instrument>& instruments,
                      int stepsPerBeat,
                      const std::string& path);
