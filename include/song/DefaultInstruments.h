#pragma once

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
// touched, so anything the user has edited, replaced or deliberately
// deleted-and-recreated stays exactly as they left it. (A preset the user
// deletes outright does come back on the next launch -- that's the price
// of not keeping a separate "already shipped" manifest, and re-creating a
// file in a library folder is a lot less costly than silently overwriting
// an edited one.)
//
// Call once at startup, after AppPaths::EnsureDirectories().
// ------------------------------------------------------------------
void WriteDefaultInstrumentsIfMissing();
