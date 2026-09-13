#pragma once

// Recolors the ImGui palette (Ubuntu/GNOME-blue accent on a dark charcoal
// base), replacing rlImGuiSetup's flat default dark theme. Colors only --
// does not touch rounding, padding, spacing, or shape in any way. Call
// once, right after rlImGuiSetup(), before the first frame.
void ApplyCustomTheme();
