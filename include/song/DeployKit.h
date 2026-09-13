#pragma once
#include "song/SongData.h"
#include <string>
#include <vector>

// ------------------------------------------------------------------
// Deploy: turn the song you're editing into something a game can ship.
//
// The problem this solves: a .ssng stores a Custom instrument's wavetable
// only BY NAME. The name is resolved at runtime against the editor's
// wavetable registry, which is filled in by RegisterBuiltinWaveforms() and
// by Lua plugins like the Wave Editor. A game linking
// soundstudio_player.cpp has neither, so every Custom instrument in a bare
// .ssng falls back to a sine wave.
//
// Deploying bakes the actual sample data of every wavetable the song uses
// into a .ssbundle, so all the wavetable synthesis -- the additive
// spectra, the Lua formulas, the hand-drawn curves -- happens HERE, once,
// at deploy time, and the game does nothing at startup but read floats.
//
// The output folder is a self-contained drop-in kit:
//
//     <name>.ssbundle     song + baked wavetables; the file to ship
//     <name>.ssng         the plain song, for reopening in the editor
//     <name>_embed.h      the bundle as a C array, to compile the music
//                         into your executable and ship no file at all
//     soundstudio_player.h/.cpp   the runtime (copied if they can be found)
//     README.txt          how to wire it into a game, in about six lines
//
// ------------------------------------------------------------------
// .ssbundle binary format, version 1 (little-endian, same conventions as
// .ssng -- see song/BinaryIO.h):
//
//     char[4]   "SSBN"
//     uint32    bundle format version (currently 1)
//     uint32    waveCount
//       repeated waveCount times:
//         uint32  nameLength
//         char[]  name (not NUL-terminated)
//         uint32  sampleCount
//         float[] samples          -- one full cycle, as registered
//     uint32    songByteCount
//     byte[]    a complete .ssng stream, "SSNG" magic and all
//
// Embedding the whole .ssng verbatim rather than re-serializing it means
// the bundle inherits every future .ssng version for free: only the
// wavetable block above is the bundle's own business.
// ------------------------------------------------------------------

struct DeployResult
{
    bool        ok = false;
    std::string message;      // one-line summary, ready to show in the UI

    std::string bundlePath;
    size_t      bundleBytes = 0;

    int  wavesEmbedded = 0;
    int  customInstruments = 0;

    // Instruments whose wavetable name isn't registered right now -- they
    // will play as sine waves in the game, exactly as they do in the editor.
    // Usually means the plugin that registers that wave isn't loaded.
    std::vector<std::string> missingWaves;

    bool playerSourcesCopied = false;
};

// `song` is the same Song the Save button builds. `outDir` is created if
// it doesn't exist. `playerSourceSearchDirs` are directories to look in
// for soundstudio_player.h/.cpp -- pass the working directory and the
// executable's directory (and their parents); the first hit wins, and if
// none do, everything else is still written and README.txt says so.
DeployResult DeployGameKit(const Song& song,
                           const std::string& outDir,
                           const std::vector<std::string>& playerSourceSearchDirs);
