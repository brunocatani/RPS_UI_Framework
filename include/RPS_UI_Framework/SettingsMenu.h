#pragma once
#include "Framework.h"

// The settings menu: every RPS mod's settings pages, the Spawner and the Console on one
// panel RPS UI owns. Players open it from the RPS UI holotape in the Pip-Boy; a mod may
// open it too (RobCo PALM's wheel does) and add its own pages, which it draws with its own
// copy of Dear ImGui exactly like a Framework panel. Owner tokens come from
// Framework::registerConsumer.
#pragma pack(push, 8)
namespace rpsui::api::settings_menu
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::SettingsMenu;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    inline constexpr std::uint32_t kMaxPages = 8;

    struct OpenRequest
    {
        std::uint32_t size{ sizeof(OpenRequest) };
        std::uint32_t version{ kRecordVersion };
        // Where the menu appears; its size is the player's last menu size. Without a
        // pose it opens in front of the headset.
        framework::PanelPose pose{};
        std::uint8_t hasPose{ 0 };
        std::uint8_t reservedBytes[7]{};
    };

    /*
     * A page's frame, between the menu's NewFrame and Render, inside a child window
     * that fills the page area. Bind imgui with RPSUI_IMGUI_BIND before any ImGui call.
     * Coordinates are the menu's layout units. fonts are the theme's faces by
     * framework::FontRole; a page adds no fonts of its own.
     */
    struct PageDrawFrame
    {
        std::uint32_t size{ sizeof(PageDrawFrame) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t pageHandle{ 0 };
        std::uint64_t contextGeneration{ 0 };
        void* d3dDevice{ nullptr };    // ID3D11Device*, for the page's own textures
        framework::ImGuiBinding imgui{};
        const framework::Theme* theme{ nullptr };
        void* fonts[framework::kFontRoleCount]{};
        float width{ 0.0f };
        float height{ 0.0f };
        float deltaSeconds{ 0.0f };
        // 1 on the frame after a Back press on the menu; a page that consumes it (closing
        // its own sub-view) returns true from its callback, otherwise the menu goes back.
        std::uint8_t backPressed{ 0 };
        std::uint8_t reservedBytes[3]{};
    };

    // Render thread. Returns true when the page used backPressed.
    using PageDrawCallback = bool(RPSUI_CALL*)(const PageDrawFrame* frame, void* userData) noexcept;

    struct PageRegistration
    {
        std::uint32_t size{ sizeof(PageRegistration) };
        std::uint32_t version{ kRecordVersion };
        char pageId[64]{};
        char title[32]{};              // The tab's label
        std::int32_t sortOrder{ 0 };   // Pages follow Settings, Spawner and Console in this order
        std::uint32_t reserved{ 0 };
        framework::ImGuiBuild imgui{};
        PageDrawCallback drawCallback{ nullptr };
        void* userData{ nullptr };
    };

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        // Game thread. FrameworkNotReady outside gameplay or before the host is ready;
        // ResourceConflict while the menu is open or opening; InvalidPose for a bad pose.
        Result(RPSUI_CALL* open)(std::uint64_t ownerToken, const OpenRequest* request) noexcept {};
        Result(RPSUI_CALL* close)(std::uint64_t ownerToken) noexcept {};
        // Any thread: the menu is open or opening.
        bool(RPSUI_CALL* isOpen)() noexcept {};
        // Any thread. CapacityReached past kMaxPages; ResourceConflict for a pageId already
        // registered; ImGuiBuildMismatch as for panels. The callback may run until
        // unregisterPage returns Ok; CallbackBusy while it runs.
        Result(RPSUI_CALL* registerPage)(std::uint64_t ownerToken, const PageRegistration* registration,
            std::uint64_t* outPageHandle) noexcept {};
        Result(RPSUI_CALL* unregisterPage)(std::uint64_t ownerToken, std::uint64_t pageHandle) noexcept {};
        // Any thread: increases each time the menu closes, so a mod can refresh what the
        // Spawner or the Console changed.
        std::uint64_t(RPSUI_CALL* closedCount)() noexcept {};
    };

    static_assert(std::is_standard_layout_v<OpenRequest> && sizeof(OpenRequest) == 72);
    static_assert(std::is_standard_layout_v<PageDrawFrame> && sizeof(PageDrawFrame) == 120);
    static_assert(std::is_standard_layout_v<PageRegistration> && sizeof(PageRegistration) == 184);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 48);
}
#pragma pack(pop)
