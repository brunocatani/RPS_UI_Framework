#pragma once
#include "RPSUIFrameworkApi.h"

namespace rpsui::sdk
{
    inline constexpr std::uint32_t RPSUI_COOPERATION_VERSION = 1;

    enum class RenderStageV1 : std::uint32_t { PanelTexture = 1, StereoOutput = 2 };
    enum class RenderAccessV1 : std::uint64_t
    {
        PanelColorWrite = 1ull << 0,
        SceneColorWrite = 1ull << 1,
        SceneDepthWrite = 1ull << 2,
        StereoSubmissionHook = 1ull << 3,
        SceneDepthRead = 1ull << 4,
    };
    [[nodiscard]] constexpr std::uint64_t accessMask(RenderAccessV1 value) noexcept
    { return static_cast<std::uint64_t>(value); }

    // Claim scopes, not a D3D sandbox. Scene access and direct hook ownership
    // cannot be granted to a panel callback; use only its private color target.
    inline constexpr auto RPSUI_PANEL_ACCESS = accessMask(RenderAccessV1::PanelColorWrite);
    inline constexpr auto RPSUI_HOST_EXCLUSIVE_ACCESS = accessMask(RenderAccessV1::SceneColorWrite) |
        accessMask(RenderAccessV1::SceneDepthWrite) | accessMask(RenderAccessV1::StereoSubmissionHook);

    enum class HookStatusV1 : std::uint32_t
    { NotInstalled = 0, Installed = 1, Conflict = 2, Unavailable = 3 };

    struct CooperativePanelRegistrationV1
    {
        std::uint32_t structSize{ sizeof(CooperativePanelRegistrationV1) };
        std::uint32_t apiVersion{ RPSUI_COOPERATION_VERSION };
        PanelRegistrationV1 panel{};
        RenderStageV1 stage{ RenderStageV1::PanelTexture };
        std::uint32_t reserved32{};
        std::uint64_t requestedAccess{ RPSUI_PANEL_ACCESS };
    };
    struct PanelAgreementV1
    {
        std::uint32_t structSize{ sizeof(PanelAgreementV1) };
        std::uint32_t apiVersion{ RPSUI_COOPERATION_VERSION };
        std::uint64_t panelHandle{};
        std::uint64_t grantedAccess{};
        std::uint64_t conflictingAccess{};
        char conflictOwnerId[64]{};
    };
    struct ConsumerDirectoryEntryV1
    {
        std::uint64_t ownerToken{};
        std::uint64_t grantedFeatures{};
        char consumerId[64]{};
        char displayName[96]{};
    };
    struct PanelDirectoryEntryV1
    {
        std::uint64_t ownerToken{}, panelHandle{}, stateSequence{};
        char panelId[64]{};
        char displayName[96]{};
        PanelPoseV1 pose{};
        std::int32_t sortOrder{};
        std::uint32_t flags{};
        RenderStageV1 stage{ RenderStageV1::PanelTexture };
        std::uint32_t reserved32{};
        std::uint64_t grantedAccess{ RPSUI_PANEL_ACCESS };
        std::uint8_t open{}, closing{}, cooperative{};
        std::uint8_t reservedBytes[5]{};
    };
    struct CooperationSnapshotV1
    {
        std::uint32_t structSize{ sizeof(CooperationSnapshotV1) };
        std::uint32_t apiVersion{ RPSUI_COOPERATION_VERSION };
        HookStatusV1 hookStatus{ HookStatusV1::NotInstalled };
        std::uint32_t consumerCount{}, panelCount{};
        std::uint8_t frameworkReady{}, suppressedHands{}; // Left bit 0, right bit 1.
        std::uint8_t reservedBytes[2]{};
        std::uint64_t focusedPanelHandle{}, resizingPanelHandle{};
        std::uint64_t hostExclusiveAccess{ RPSUI_HOST_EXCLUSIVE_ACCESS };
        char hostId[64]{ "rps.ui.framework" };
        ConsumerDirectoryEntryV1 consumers[RPSUI_MAX_CONSUMERS]{};
        // Sorted by (sortOrder, panelHandle), matching composition order.
        PanelDirectoryEntryV1 panels[RPSUI_MAX_PANELS]{};
    };
    struct CooperationApiV1
    {
        std::uint32_t structSize{ sizeof(CooperationApiV1) };
        std::uint32_t apiVersion{ RPSUI_COOPERATION_VERSION };
        std::uint32_t apiFlavor{ RPSUI_API_FLAVOR };
        std::uint32_t reserved32{};
        ResultV1 (RPSUI_CALL* registerPanel)(std::uint64_t ownerToken,
            const CooperativePanelRegistrationV1*, PanelAgreementV1*) noexcept{};
        ResultV1 (RPSUI_CALL* getSnapshot)(CooperationSnapshotV1*) noexcept{};
        // Nonblocking: CallbackBusy closes admission immediately. Retry on a
        // later control tick; free callback data only after Ok. Never spin.
        ResultV1 (RPSUI_CALL* unregisterPanelSafely)(std::uint64_t ownerToken, std::uint64_t panelHandle) noexcept{};
        ResultV1 (RPSUI_CALL* unregisterConsumerSafely)(std::uint64_t ownerToken) noexcept{};
    };
    [[nodiscard]] inline const CooperationApiV1* RequestCooperationApiV1() noexcept
    {
        const auto module = GetModuleHandleW(L"RPS_UI_Framework.dll");
        if (!module) return nullptr;
        const auto address = GetProcAddress(module, "RPSUI_RequestCooperationApi");
        if (!address) return nullptr;
        using Request = const CooperationApiV1* (RPSUI_CALL*)(std::uint32_t) noexcept;
        const auto* api = reinterpret_cast<Request>(address)(RPSUI_COOPERATION_VERSION);
        return api && api->structSize >= sizeof(CooperationApiV1) &&
            api->apiVersion == RPSUI_COOPERATION_VERSION && api->apiFlavor == RPSUI_API_FLAVOR ? api : nullptr;
    }
    static_assert(std::is_standard_layout_v<CooperativePanelRegistrationV1>);
    static_assert(std::is_standard_layout_v<PanelAgreementV1>);
    static_assert(std::is_standard_layout_v<CooperationSnapshotV1>);
    static_assert(std::is_standard_layout_v<CooperationApiV1>);
}
