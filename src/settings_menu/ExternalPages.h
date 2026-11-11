#pragma once
#include <RPS_UI_Framework/SettingsMenu.h>

#include <cstdint>
#include <string>
#include <vector>

/*
 * Pages other mods add to the settings menu (SettingsMenu::registerPage). The menu shows
 * one tab per page after Settings, Spawner and Console, and calls the page's draw callback
 * inside the page area. Registration runs on any thread; drawing on the render thread
 * under the same mutex, so unregister refuses (CallbackBusy) while a page is drawing and
 * a page's callback never runs after unregister returned Ok.
 */
namespace rpsui::settings_menu::external_pages
{
    struct Tab
    {
        std::uint64_t handle{ 0 };
        std::string title;
    };

    [[nodiscard]] api::Result add(std::uint64_t owner, const api::settings_menu::PageRegistration& registration,
        std::uint64_t& outHandle) noexcept;
    [[nodiscard]] api::Result remove(std::uint64_t owner, std::uint64_t handle) noexcept;
    // Every page of an owner, when its consumer goes away.
    void removeOwner(std::uint64_t owner) noexcept;
    // In sortOrder, then registration order.
    [[nodiscard]] std::vector<Tab> tabs();

    // What the menu panel's frame supplies to a page (render thread, during the frame).
    struct FrameContext
    {
        std::uint64_t contextGeneration{ 0 };
        void* d3dDevice{ nullptr };
        api::framework::ImGuiBinding imgui{};
        const api::framework::Theme* theme{ nullptr };
        void* fonts[api::framework::kFontRoleCount]{};
        float deltaSeconds{ 0.0f };
    };
    void setFrameContext(const FrameContext* context) noexcept;
    // Draws the page in the current window's remaining area. False when the page is gone
    // (the menu then shows Settings); usedBack tells whether the page consumed a Back press.
    [[nodiscard]] bool draw(std::uint64_t handle, float width, float height, bool backPressed, bool& usedBack) noexcept;
}
