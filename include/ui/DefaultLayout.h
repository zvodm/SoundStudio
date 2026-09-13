#pragma once

// The panel arrangement that ships with SoundStudio: sized and docked the
// way it looked after actually using the app (Transport across the top,
// Tracks on the left, Arrangement/Instrument tabbed together on the right,
// File/Timeline tabbed together along the bottom, Piano Roll filling the
// center). Every brand-new checkout/instance starts from this layout
// instead of the coarser DockBuilder split-in-code fallback, so the GUI
// never needs to be rebuilt by hand on first launch.
//
// This is raw Dear ImGui .ini text (same format imgui.ini is saved in) and
// is only ever applied when no imgui.ini already exists on disk -- once the
// app has run once, the real imgui.ini takes over and remembers whatever
// the user has since rearranged.
extern const char* kDefaultImGuiLayoutIni;
