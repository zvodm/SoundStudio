#pragma once
#include <string>

// Three starter plugins, shipped as embedded Lua source: a Wave Editor
// (define a custom oscillator waveform by formula or by drawing it), a
// colored master Oscilloscope, and an Instrument Editor (every value of
// the selected instrument as an exact text field rather than a slider,
// plus a click-to-load browser over the .ssip preset library). All three
// are written out to AppPaths::PluginsDir() (via
// DefaultContent::EnsureInstalledOnFirstRun, or the installer's pick-list)
// the first time it's empty of these files -- same idea as
// ui/DefaultLayout.h's shipped imgui.ini layout -- and from that point on
// they're just normal, fully user-editable .lua files; this never
// overwrites a file the user has since changed.
void WriteDefaultPluginsIfMissing();

// The catalogue, mirroring song/DefaultInstruments.h -- so the installer's
// pick-list can enumerate the bundled plugins instead of hardcoding them.
struct DefaultPluginInfo
{
    const char* file        = ""; // filename stem (no .lua), and the selection id
    const char* name        = ""; // the panel title the script sets
    const char* description = ""; // one line, for the pick-list
};

int DefaultPluginCount();
DefaultPluginInfo DefaultPluginAt(int index);

// Writes one bundled plugin by its file stem. A plugin already on disk
// counts as success and is left alone unless overwrite is set.
bool WriteDefaultPlugin(const std::string& fileStem, bool overwrite = false);
