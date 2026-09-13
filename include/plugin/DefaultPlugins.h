#pragma once

// Three starter plugins, shipped as embedded Lua source: a Wave Editor
// (define a custom oscillator waveform by formula or by drawing it), a
// colored master Oscilloscope, and an Instrument Editor (every value of
// the selected instrument as an exact text field rather than a slider,
// plus a click-to-load browser over the .ssip preset library). All three
// are written out to AppPaths::PluginsDir()
// the first time it's empty of these files -- same idea as
// ui/DefaultLayout.h's shipped imgui.ini layout -- and from that point on
// they're just normal, fully user-editable .lua files; this never
// overwrites a file the user has since changed.
void WriteDefaultPluginsIfMissing();
