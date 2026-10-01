#pragma once
#include "RPSUICooperationApi.h"

namespace rpsui::sdk
{
    inline constexpr std::uint32_t RPSUI_REGISTRY_VERSION = 1;
    inline constexpr std::size_t RPSUI_REGISTRY_PAGE_CONSUMERS = 16;
    inline constexpr std::size_t RPSUI_REGISTRY_PAGE_PANELS = 32;

    // Independent, read-only discovery service. Existing registration and
    // rendering APIs use the expanded host pool without consumer migration.
    // RPSUI_MAX_CONSUMERS/PANELS remain frozen V1 snapshot dimensions, not
    // the current runtime budgets. Read those budgets from a registry page.
    struct RegistryQueryV1
    {
        std::uint32_t structSize{ sizeof(RegistryQueryV1) };
        std::uint32_t apiVersion{ RPSUI_REGISTRY_VERSION };
        std::uint64_t afterOwnerToken{}, afterPanelHandle{};
    };

    struct RegistryPageV1
    {
        std::uint32_t structSize{ sizeof(RegistryPageV1) };
        std::uint32_t apiVersion{ RPSUI_REGISTRY_VERSION };
        std::uint32_t consumerCapacity{}, panelCapacity{};
        std::uint32_t inputSubscriberCapacity{}, reserved32{};
        std::uint32_t totalConsumers{}, totalPanels{};
        std::uint32_t consumerCount{}, panelCount{};
        // Increases only on registration/removal, not pose/input changes.
        std::uint64_t registryRevision{};
        std::uint64_t nextOwnerToken{}, nextPanelHandle{};
        std::uint8_t moreConsumers{}, morePanels{};
        std::uint8_t reservedBytes[6]{};
        ConsumerDirectoryEntryV1 consumers[RPSUI_REGISTRY_PAGE_CONSUMERS]{};
        PanelDirectoryEntryV1 panels[RPSUI_REGISTRY_PAGE_PANELS]{};
    };

    // Entries are ordered by monotonically increasing identity, not draw order.
    // Start both cursors at zero; pass the returned cursors on the next call.
    // Stop when both more flags are zero. Removed identities never get reused.
    // Each page is an atomic advisory copy. If revisions differ, a caller needing
    // one membership census restarts on a later control tick (never spin).
    // Pose/input may change between pages even with identical revisions.
    // getPage is callable from a control thread, including before host readiness.
    struct RegistryApiV1
    {
        std::uint32_t structSize{ sizeof(RegistryApiV1) };
        std::uint32_t apiVersion{ RPSUI_REGISTRY_VERSION };
        std::uint32_t apiFlavor{ RPSUI_API_FLAVOR };
        std::uint32_t reserved32{};
        ResultV1 (RPSUI_CALL* getPage)(const RegistryQueryV1*, RegistryPageV1*) noexcept{};
    };

    [[nodiscard]] inline const RegistryApiV1* RequestRegistryApiV1() noexcept
    {
        const auto module = GetModuleHandleW(L"RPS_UI_Framework.dll");
        const auto address = module ? GetProcAddress(module, "RPSUI_RequestRegistryApi") : nullptr;
        if (!address) return nullptr;
        using Request = const RegistryApiV1* (RPSUI_CALL*)(std::uint32_t) noexcept;
        const auto* api = reinterpret_cast<Request>(address)(RPSUI_REGISTRY_VERSION);
        return api && api->structSize >= sizeof(RegistryApiV1) &&
            api->apiVersion == RPSUI_REGISTRY_VERSION && api->apiFlavor == RPSUI_API_FLAVOR && api->getPage ? api : nullptr;
    }

    static_assert(std::is_standard_layout_v<RegistryQueryV1> && sizeof(RegistryQueryV1) == 24);
    static_assert(std::is_standard_layout_v<RegistryPageV1>);
    static_assert(std::is_standard_layout_v<RegistryApiV1> && sizeof(RegistryApiV1) == 24);
    static_assert(offsetof(RegistryApiV1, getPage) == 16);
}
