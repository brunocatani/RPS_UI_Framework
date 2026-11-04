#pragma once
#include "Cooperation.h"

// Read-only discovery of every registered consumer and panel, with the current
// runtime budgets.
//
// Entries are ordered by monotonically increasing identity, not draw order.
// Start both cursors at zero; pass the returned cursors on the next call.
// Stop when both more flags are zero. Removed identities never get reused.
// Each page is an atomic advisory copy. If revisions differ, a caller needing
// one membership census restarts on a later control tick (never spin).
// Pose and input may change between pages even with identical revisions.
#pragma pack(push, 8)
namespace rpsui::api::registry
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::Registry;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;
    inline constexpr std::size_t kPageConsumers = 16;
    inline constexpr std::size_t kPagePanels = 32;

    struct Query
    {
        std::uint32_t size{ sizeof(Query) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t afterOwnerToken{}, afterPanelHandle{};
    };

    struct Page
    {
        std::uint32_t size{ sizeof(Page) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t consumerCapacity{}, panelCapacity{};
        std::uint32_t inputSubscriberCapacity{}, reserved{};
        std::uint32_t totalConsumers{}, totalPanels{};
        std::uint32_t consumerCount{}, panelCount{};
        // Increases only on registration/removal, not pose/input changes.
        std::uint64_t registryRevision{};
        std::uint64_t nextOwnerToken{}, nextPanelHandle{};
        std::uint8_t moreConsumers{}, morePanels{};
        std::uint8_t reservedBytes[6]{};
        cooperation::ConsumerDirectoryEntry consumers[kPageConsumers]{};
        cooperation::PanelDirectoryEntry panels[kPagePanels]{};
    };

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        // Callable from a control thread, including before host readiness.
        Result(RPSUI_CALL* getPage)(const Query* query, Page* page) noexcept {};
    };

    static_assert(std::is_standard_layout_v<Query> && sizeof(Query) == 24);
    static_assert(std::is_standard_layout_v<Page>);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 8);
}
#pragma pack(pop)
