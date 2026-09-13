#pragma once
#include <string>

// ------------------------------------------------------------------
// The bundled instrument library.
//
// Phase E shipped the .ssip preset format (save/reuse a patch across
// songs) but nothing to actually load -- a fresh install starts with an
// empty Instruments/ folder and one default square-wave "Lead". This
// writes a full starter library into AppPaths::InstrumentsDir(): pianos,
// organs, guitars, basses, brass, reeds, winds, bowed strings, mallets,
// voices, synth leads/pads and a drum kit, all as ordinary .ssip files.
//
// Most of them are WaveformType::Custom patches pointing at the
// single-cycle tables registered by RegisterBuiltinWaveforms() (see
// audio/DefaultWaveforms.h) -- so call that first, or the Custom ones
// will load fine but play silence.
//
// Same "ship it, then get out of the way" contract as the default plugins
// and the default imgui.ini layout: a file that already exists is never
// touched, so anything the user has edited or replaced stays exactly as
// they left it.
//
// Not called directly at startup any more -- go through
// DefaultContent::EnsureInstalledOnFirstRun() (app/DefaultContent.h), which
// only installs the library when nobody has chosen yet. That's what stops
// a preset the user deleted from reappearing on the next launch.
// ------------------------------------------------------------------
void WriteDefaultInstrumentsIfMissing();

// ------------------------------------------------------------------
// The catalogue, so something other than this file can decide what gets
// installed -- the installer's pick-list enumerates it rather than keeping
// its own copy of the list, which would drift the moment a preset is added
// here. See app/DefaultContent.h.
// ------------------------------------------------------------------

struct DefaultInstrumentInfo
{
    const char* file  = "";  // filename stem, and the id the selection list uses
    const char* name  = "";  // display name
    const char* group = "";  // family: "Organs", "Guitars", "Drum Kit", ...
};

int DefaultInstrumentCount();
DefaultInstrumentInfo DefaultInstrumentAt(int index);

// Writes one bundled preset by its file stem. Returns false only if no
// bundled preset has that name, or the write failed -- a preset that is
// already on disk counts as success (and is left alone unless overwrite).
bool WriteDefaultInstrument(const std::string& fileStem, bool overwrite = false);
