#pragma once
#include "RPSUIFrameworkApi.h"

namespace rpsui::sdk
{
    inline constexpr std::uint32_t RPSUI_NUMBER_OVERLAY_VERSION = 1;
    inline constexpr float RPSUI_NUMBER_OVERLAY_ASPECT = 1024.0f/480.0f;

    // Independently negotiated addition. Existing panel/input/cooperation V1
    // tables and records keep their contracts. Uses an existing consumer token.
    struct NumberOverlayRegistrationV1
    {
        std::uint32_t structSize{sizeof(NumberOverlayRegistrationV1)};
        std::uint32_t apiVersion{RPSUI_NUMBER_OVERLAY_VERSION};
        char overlayId[64]{};
        char displayName[96]{};
    };
    struct NumberOverlayPresentationV1
    {
        std::uint32_t structSize{sizeof(NumberOverlayPresentationV1)};
        std::uint32_t apiVersion{RPSUI_NUMBER_OVERLAY_VERSION};
        std::uint64_t sequence{};
        PanelPoseV1 pose{}; // Game units. Width/height must match the aspect above.
        std::uint32_t value{}; // Entire uint32 range, including zero; no leading padding.
        float opacity{0.55f}; // Premultiplied white, 0..1; transparent background.
        std::uint8_t open{};
        std::uint8_t reserved[7]{};
    };
    struct NumberOverlayApiV1
    {
        std::uint32_t structSize{sizeof(NumberOverlayApiV1)};
        std::uint32_t apiVersion{RPSUI_NUMBER_OVERLAY_VERSION};
        std::uint32_t apiFlavor{RPSUI_API_FLAVOR};
        std::uint32_t reserved32{};
        ResultV1 (RPSUI_CALL* registerOverlay)(std::uint64_t ownerToken,
            const NumberOverlayRegistrationV1*,std::uint64_t* outHandle) noexcept{};
        ResultV1 (RPSUI_CALL* submit)(std::uint64_t ownerToken,std::uint64_t handle,
            const NumberOverlayPresentationV1*) noexcept{};
        // No consumer render callback or borrowed payload. The host owns all
        // resources; an already copied render snapshot may finish after removal.
        ResultV1 (RPSUI_CALL* unregisterOverlay)(std::uint64_t ownerToken,std::uint64_t handle) noexcept{};
    };
    [[nodiscard]] inline const NumberOverlayApiV1* RequestNumberOverlayApiV1() noexcept
    {
        const auto module=GetModuleHandleW(L"RPS_UI_Framework.dll");
        if (!module) return nullptr;
        const auto address=GetProcAddress(module,"RPSUI_RequestNumberOverlayApi");
        if (!address) return nullptr;
        using Request=const NumberOverlayApiV1* (RPSUI_CALL*)(std::uint32_t) noexcept;
        const auto* api=reinterpret_cast<Request>(address)(RPSUI_NUMBER_OVERLAY_VERSION);
        return api && api->structSize>=sizeof(NumberOverlayApiV1) && api->apiVersion==RPSUI_NUMBER_OVERLAY_VERSION &&
            api->apiFlavor==RPSUI_API_FLAVOR && api->registerOverlay && api->submit && api->unregisterOverlay ? api : nullptr;
    }
    static_assert(std::is_standard_layout_v<NumberOverlayRegistrationV1>);
    static_assert(std::is_standard_layout_v<NumberOverlayPresentationV1>);
    static_assert(std::is_standard_layout_v<NumberOverlayApiV1>);
}
