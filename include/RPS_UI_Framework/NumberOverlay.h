#pragma once
#include "Framework.h"

// Numbers the host draws itself. No consumer render callback or borrowed
// payload: the host owns all resources; an already copied render snapshot may
// finish after removal. Uses a Framework owner token.
#pragma pack(push, 8)
namespace rpsui::api::number_overlay
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::NumberOverlay;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;
    inline constexpr float kAspect = 1024.0f / 480.0f;

    // How the host lays out an overlay's numbers.
    enum class Style : std::uint8_t
    {
        // One value in the default face, centered.
        Plain = 0,
        // The Fallout HUD ammo counter: value over a divider over
        // secondaryValue, right aligned, in a condensed bold face.
        Hud = 1,
        // Hud inside the HUD's bracket pair: a top bar with end ticks down and
        // a bottom bar with end ticks up.
        HudBracketed = 2,
    };

    struct Registration
    {
        std::uint32_t size{ sizeof(Registration) };
        std::uint32_t version{ kRecordVersion };
        char overlayId[64]{};
        char displayName[96]{};
    };

    struct Presentation
    {
        std::uint32_t size{ sizeof(Presentation) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t sequence{};
        framework::PanelPose pose{}; // Game units. Width/height must match kAspect.
        std::uint32_t value{}; // Entire uint32 range, including zero; no leading padding.
        float opacity{ 0.55f }; // Premultiplied, 0..1; transparent background.
        std::uint8_t open{};
        std::uint8_t reservedBytes[3]{};
        float color[3]{ 1.0f, 1.0f, 1.0f }; // Digit and frame color, 0..1 each.
        std::uint32_t secondaryValue{}; // Hud styles only: the number below the divider.
        Style style{ Style::Plain };
        std::uint8_t reservedStyle[3]{};
    };

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        Result(RPSUI_CALL* registerOverlay)(std::uint64_t ownerToken, const Registration* registration,
            std::uint64_t* outHandle) noexcept {};
        Result(RPSUI_CALL* submit)(std::uint64_t ownerToken, std::uint64_t handle, const Presentation* presentation) noexcept {};
        Result(RPSUI_CALL* unregisterOverlay)(std::uint64_t ownerToken, std::uint64_t handle) noexcept {};
    };

    static_assert(std::is_standard_layout_v<Registration> && sizeof(Registration) == 168);
    static_assert(std::is_standard_layout_v<Presentation> && sizeof(Presentation) == 104);
    static_assert(offsetof(Presentation, value) == 72 && offsetof(Presentation, style) == 100);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 24);
}
#pragma pack(pop)
