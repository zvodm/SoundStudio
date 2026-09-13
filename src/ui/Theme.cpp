#include "ui/Theme.h"
#include "imgui.h"

namespace
{
    ImVec4 Col(int r, int g, int b, int a = 255)
    {
        return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f);
    }
}

// Colors only -- deliberately does not touch rounding, padding, spacing, or
// border sizes, so the app's shape/layout stays exactly what ImGui's
// defaults give it. Just a palette swap: dark charcoal base, Ubuntu/GNOME
// blue accent (the same blue used for selection/highlight in Ubuntu's
// default theme) in place of rlImGuiSetup's flat default dark theme.
void ApplyCustomTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    const ImVec4 bgDarkest    = Col(32, 32, 32);
    const ImVec4 bgPanel      = Col(39, 39, 39);
    const ImVec4 bgPanelAlt   = Col(35, 35, 35);
    const ImVec4 bgHover      = Col(53, 53, 53);
    const ImVec4 bgActive     = Col(66, 66, 66);
    const ImVec4 border       = Col(60, 60, 60);
    const ImVec4 text         = Col(230, 230, 230);
    const ImVec4 textMuted    = Col(150, 150, 150);
    const ImVec4 accent       = Col(53, 132, 228);  // Ubuntu/GNOME blue (#3584E4)
    const ImVec4 accentHover  = Col(94, 160, 235);
    const ImVec4 accentActive = Col(30, 100, 190);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = text;
    colors[ImGuiCol_TextDisabled]          = textMuted;
    colors[ImGuiCol_WindowBg]              = bgPanel;
    colors[ImGuiCol_ChildBg]               = bgPanelAlt;
    colors[ImGuiCol_PopupBg]               = bgPanel;
    colors[ImGuiCol_Border]                = border;
    colors[ImGuiCol_BorderShadow]          = Col(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg]               = bgPanelAlt;
    colors[ImGuiCol_FrameBgHovered]        = bgHover;
    colors[ImGuiCol_FrameBgActive]         = bgActive;

    colors[ImGuiCol_TitleBg]               = bgDarkest;
    colors[ImGuiCol_TitleBgActive]         = Col(28, 60, 96); // dimmed accent -- marks the focused window
    colors[ImGuiCol_TitleBgCollapsed]      = bgDarkest;
    colors[ImGuiCol_MenuBarBg]             = bgPanelAlt;

    colors[ImGuiCol_ScrollbarBg]           = bgPanelAlt;
    colors[ImGuiCol_ScrollbarGrab]         = border;
    colors[ImGuiCol_ScrollbarGrabHovered]  = accentHover;
    colors[ImGuiCol_ScrollbarGrabActive]   = accent;

    colors[ImGuiCol_CheckMark]             = accent;
    colors[ImGuiCol_SliderGrab]            = accent;
    colors[ImGuiCol_SliderGrabActive]      = accentHover;

    colors[ImGuiCol_Button]                = bgHover;
    colors[ImGuiCol_ButtonHovered]         = accentHover;
    colors[ImGuiCol_ButtonActive]          = accentActive;

    colors[ImGuiCol_Header]                = bgHover;
    colors[ImGuiCol_HeaderHovered]         = accentHover;
    colors[ImGuiCol_HeaderActive]          = accentActive;

    colors[ImGuiCol_Separator]             = border;
    colors[ImGuiCol_SeparatorHovered]      = accentHover;
    colors[ImGuiCol_SeparatorActive]       = accent;

    colors[ImGuiCol_ResizeGrip]            = Col(53, 132, 228, 40);
    colors[ImGuiCol_ResizeGripHovered]     = Col(53, 132, 228, 130);
    colors[ImGuiCol_ResizeGripActive]      = accent;

    colors[ImGuiCol_Tab]                   = bgPanelAlt;
    colors[ImGuiCol_TabHovered]            = accentHover;
    colors[ImGuiCol_TabActive]             = Col(36, 72, 112);
    colors[ImGuiCol_TabUnfocused]          = bgPanelAlt;
    colors[ImGuiCol_TabUnfocusedActive]    = bgHover;

    colors[ImGuiCol_PlotLines]             = accent;
    colors[ImGuiCol_PlotLinesHovered]      = accentHover;
    colors[ImGuiCol_PlotHistogram]         = accent;
    colors[ImGuiCol_PlotHistogramHovered]  = accentHover;

    colors[ImGuiCol_TextSelectedBg]        = Col(53, 132, 228, 70);
    colors[ImGuiCol_DragDropTarget]        = accent;
    colors[ImGuiCol_NavHighlight]          = accent;
    colors[ImGuiCol_NavWindowingHighlight] = Col(255, 255, 255, 180);
    colors[ImGuiCol_NavWindowingDimBg]     = Col(32, 32, 32, 120);
    colors[ImGuiCol_ModalWindowDimBg]      = Col(32, 32, 32, 140);
}
