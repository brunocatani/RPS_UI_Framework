#pragma once

#include <imgui.h>
#include <RPS_UI_Framework/ThemeStyle.h>
#include "MenuTheme.h"

namespace devui::visual
{
    [[nodiscard]] inline ImVec4 accent(float alpha = 1.0f) noexcept
    {
        return rpsui::settings_menu::theme::accent(alpha);
    }

    [[nodiscard]] inline ImVec4 text(float alpha = 1.0f) noexcept
    {
        return rpsui::settings_menu::theme::text(alpha);
    }

    [[nodiscard]] inline ImVec4 muted(float alpha = 1.0f) noexcept
    {
        return rpsui::settings_menu::theme::muted(alpha);
    }

    [[nodiscard]] inline ImVec4 surface(float alpha = 1.0f) noexcept
    {
        return rpsui::settings_menu::theme::ink(alpha);
    }

    // The theme's style, as RPS UI applies it to the panel's context, so the
    // desktop preview matches the game; then the menu's denser layout.
    inline void applyStyle() noexcept
    {
        rpsui::imgui_host::applyTheme(rpsui::settings_menu::theme::current());
        auto& style = ImGui::GetStyle();
        style.WindowPadding = { 0.0f, 0.0f };
        style.FramePadding = { 10.0f, 6.0f };
        style.CellPadding = { 18.0f, 8.0f };
        style.ItemSpacing = { 8.0f, 4.0f };
        style.ItemInnerSpacing = { 8.0f, 7.0f };
        style.ScrollbarSize = 18.0f;
        style.GrabMinSize = 20.0f;
        style.SeparatorTextPadding = { 18.0f, 5.0f };
    }
}
