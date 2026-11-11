#pragma once

// A theme as Dear ImGui's style, the mapping RPS UI applies to every panel
// context. Includes only Dear ImGui, so a consumer's desktop preview or test
// can style its own context the way the game does.

#include <RPS_UI_Framework/Framework.h>

#include <imgui.h>

#include <algorithm>

namespace rpsui::imgui_host
{
    [[nodiscard]] inline ImVec4 color(const api::framework::ThemeColor& value, float alpha = 1.0f) noexcept
    {
        return { value.r, value.g, value.b, value.a * alpha };
    }

    /*
     * A chosen choice, drawn the same way on every panel: an accent tint, an accent
     * outline and accent text. The solid accent fill stays for a page's one main action,
     * so the next step always stands out. For buttons; pair with popChosen().
     */
    inline void pushChosen(const api::framework::Theme& theme) noexcept
    {
        ImGui::PushStyleColor(ImGuiCol_Button, color(theme.accent, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color(theme.accent, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color(theme.accent, 0.36f));
        ImGui::PushStyleColor(ImGuiCol_Text, color(theme.accent));
        ImGui::PushStyleColor(ImGuiCol_Border, color(theme.accent));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, theme.borderWidth * 2.0f);
    }

    inline void popChosen() noexcept
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);
    }

    // The theme as ImGui's style, so plain widgets follow it. The context must
    // be current. A consumer's setup callback may adjust it afterwards.
    inline void applyTheme(const api::framework::Theme& theme) noexcept
    {
        auto& style = ImGui::GetStyle();
        const float textPad = std::max(0.0f, (theme.targetHeight - theme.fontSize) * 0.5f);
        style.WindowPadding = { theme.padding, theme.padding };
        style.FramePadding = { theme.padding, std::min(textPad, theme.padding * 2.0f) };
        style.CellPadding = { theme.padding, theme.spacing * 0.5f };
        style.ItemSpacing = { theme.spacing, theme.spacing * 0.5f };
        style.ItemInnerSpacing = { theme.spacing, theme.spacing };
        // A ray jitters: hit boxes reach into the gaps between items.
        style.TouchExtraPadding = { theme.spacing * 0.25f, theme.spacing * 0.25f };
        style.ScrollbarSize = std::max(14.0f, theme.fontSize * 0.9f);
        style.GrabMinSize = std::max(16.0f, theme.fontSize);
        style.WindowRounding = style.ChildRounding = style.PopupRounding = theme.rounding;
        style.FrameRounding = style.ScrollbarRounding = style.TabRounding = theme.rounding;
        style.GrabRounding = theme.rounding * 0.5f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.FrameBorderSize = theme.borderWidth;
        style.PopupBorderSize = theme.borderWidth;
        style.TabBorderSize = 0.0f;
        style.SeparatorTextBorderSize = theme.borderWidth;
        style.SeparatorTextAlign = { 0.0f, 0.5f };
        style.SeparatorTextPadding = { theme.padding, theme.spacing * 0.5f };
        style.SelectableTextAlign = { 0.0f, 0.5f };
        style.DisabledAlpha = 0.45f;
        style.AntiAliasedLines = true;
        style.AntiAliasedLinesUseTex = true;
        style.AntiAliasedFill = true;

        const auto none = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        auto* c = style.Colors;
        c[ImGuiCol_Text] = color(theme.text);
        c[ImGuiCol_TextDisabled] = color(theme.textDisabled);
        c[ImGuiCol_WindowBg] = color(theme.background);
        c[ImGuiCol_ChildBg] = none;
        c[ImGuiCol_PopupBg] = color(theme.background);
        c[ImGuiCol_Border] = color(theme.border);
        c[ImGuiCol_BorderShadow] = none;
        c[ImGuiCol_FrameBg] = color(theme.surface);
        c[ImGuiCol_FrameBgHovered] = color(theme.hover);
        c[ImGuiCol_FrameBgActive] = color(theme.pressed);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = color(theme.background);
        c[ImGuiCol_MenuBarBg] = color(theme.surface);
        c[ImGuiCol_ScrollbarBg] = color(theme.surface);
        c[ImGuiCol_ScrollbarGrab] = color(theme.border);
        c[ImGuiCol_ScrollbarGrabHovered] = color(theme.focus);
        c[ImGuiCol_ScrollbarGrabActive] = color(theme.accent);
        c[ImGuiCol_CheckMark] = color(theme.onAccent);
        c[ImGuiCol_CheckboxSelectedBg] = color(theme.accent);
        c[ImGuiCol_SliderGrab] = color(theme.accent);
        c[ImGuiCol_SliderGrabActive] = color(theme.focus);
        c[ImGuiCol_Button] = color(theme.surface);
        c[ImGuiCol_ButtonHovered] = color(theme.hover);
        c[ImGuiCol_ButtonActive] = color(theme.pressed);
        c[ImGuiCol_Header] = color(theme.selected);
        c[ImGuiCol_HeaderHovered] = color(theme.hover);
        c[ImGuiCol_HeaderActive] = color(theme.pressed);
        c[ImGuiCol_Separator] = color(theme.divider);
        c[ImGuiCol_SeparatorHovered] = color(theme.focus);
        c[ImGuiCol_SeparatorActive] = color(theme.accent);
        c[ImGuiCol_ResizeGrip] = color(theme.surface);
        c[ImGuiCol_ResizeGripHovered] = color(theme.hover);
        c[ImGuiCol_ResizeGripActive] = color(theme.accent);
        c[ImGuiCol_InputTextCursor] = color(theme.accent);
        c[ImGuiCol_Tab] = c[ImGuiCol_TabDimmed] = color(theme.surface);
        c[ImGuiCol_TabHovered] = color(theme.hover);
        c[ImGuiCol_TabSelected] = c[ImGuiCol_TabDimmedSelected] = color(theme.selected);
        c[ImGuiCol_TabSelectedOverline] = color(theme.accent);
        c[ImGuiCol_TabDimmedSelectedOverline] = color(theme.textMuted);
        c[ImGuiCol_PlotLines] = c[ImGuiCol_PlotLinesHovered] = color(theme.accent);
        c[ImGuiCol_PlotHistogram] = c[ImGuiCol_PlotHistogramHovered] = color(theme.accent);
        c[ImGuiCol_TableHeaderBg] = color(theme.surface);
        c[ImGuiCol_TableBorderStrong] = color(theme.border);
        c[ImGuiCol_TableBorderLight] = color(theme.divider);
        c[ImGuiCol_TableRowBg] = none;
        c[ImGuiCol_TableRowBgAlt] = color(theme.surface, 0.5f);
        c[ImGuiCol_TextLink] = color(theme.accent);
        c[ImGuiCol_TextSelectedBg] = color(theme.selected);
        c[ImGuiCol_TreeLines] = color(theme.divider);
        c[ImGuiCol_DragDropTarget] = color(theme.accent);
        c[ImGuiCol_DragDropTargetBg] = color(theme.hover);
        c[ImGuiCol_UnsavedMarker] = color(theme.pending);
        c[ImGuiCol_NavCursor] = color(theme.focus);
        c[ImGuiCol_NavWindowingHighlight] = color(theme.focus);
        c[ImGuiCol_NavWindowingDimBg] = color(theme.scrim);
        c[ImGuiCol_ModalWindowDimBg] = color(theme.scrim);
    }
}
