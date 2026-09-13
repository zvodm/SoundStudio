#pragma once
#include <string>

// The on-disk home for everything SoundStudio reads/writes outside of a
// single song file:
//
//     <user home>/SoundStudio/
//       Waves/       -- custom waveforms (.sswave) saved by the Wave Editor plugin
//       Plugins/     -- .lua plugin scripts, loaded by PluginManager
//       Songs/       -- default Save/Load location for .ssng files
//       Instruments/ -- default location for .ssip instrument presets
//
// EnsureDirectories() creates the whole tree (idempotent -- safe to call
// every launch) so a fresh machine or a fresh Windows user account just
// works with no manual setup.
namespace AppPaths
{
    const std::string& RootDir();
    const std::string& WavesDir();
    const std::string& PluginsDir();
    const std::string& SongsDir();
    const std::string& InstrumentsDir();

    // Creates RootDir and every subdirectory above if they don't already
    // exist. Call once at startup, before anything touches these paths.
    void EnsureDirectories();
}
