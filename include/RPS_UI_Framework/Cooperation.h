#pragma once
#include "Framework.h"

// Render-resource agreements, a host directory and safe retirement of panels
// and consumers whose callbacks may still be running.
#pragma pack(push, 8)
namespace rpsui::api::cooperation
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::Cooperation;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    // Fixed snapshot dimensions, not the runtime budgets. Read those from a
    // Registry page.
    inline constexpr std::size_t kSnapshotConsumers = 16;
    inline constexpr std::size_t kSnapshotPanels = 8;

    enum class RenderStage : std::uint32_t { PanelTexture = 1, StereoOutput = 2 };
    enum class RenderAccess : std::uint64_t
    {
        PanelColorWrite = 1ull << 0,
        SceneColorWrite = 1ull << 1,
        SceneDepthWrite = 1ull << 2,
        StereoSubmissionHook = 1ull << 3,
        SceneDepthRead = 1ull << 4,
    };
    [[nodiscard]] constexpr std::uint64_t accessMask(RenderAccess value) noexcept
    {
        return static_cast<std::uint64_t>(value);
    }

    // Claim scopes, not a D3D sandbox. Scene access and direct hook ownership
    // cannot be granted to a panel callback: it issues ImGui calls, and the
    // host renders them into the panel's private color target.
    inline constexpr std::uint64_t kPanelAccess = accessMask(RenderAccess::PanelColorWrite);
    inline constexpr std::uint64_t kHostExclusiveAccess = accessMask(RenderAccess::SceneColorWrite) |
        accessMask(RenderAccess::SceneDepthWrite) | accessMask(RenderAccess::StereoSubmissionHook);

    enum class HookStatus : std::uint32_t { NotInstalled = 0, Installed = 1, Conflict = 2, Unavailable = 3 };

    struct PanelRegistration
    {
        std::uint32_t size{ sizeof(PanelRegistration) };
        std::uint32_t version{ kRecordVersion };
        framework::PanelRegistration panel{};
        RenderStage stage{ RenderStage::PanelTexture };
        std::uint32_t reserved{};
        std::uint64_t requestedAccess{ kPanelAccess };
    };

    struct PanelAgreement
    {
        std::uint32_t size{ sizeof(PanelAgreement) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t panelHandle{};
        std::uint64_t grantedAccess{};
        std::uint64_t conflictingAccess{};
        char conflictOwnerId[64]{};
    };

    struct ConsumerDirectoryEntry
    {
        std::uint64_t ownerToken{};
        char consumerId[64]{};
        char displayName[96]{};
    };

    struct PanelDirectoryEntry
    {
        std::uint64_t ownerToken{}, panelHandle{}, stateSequence{};
        char panelId[64]{};
        char displayName[96]{};
        framework::PanelPose pose{};
        std::int32_t sortOrder{};
        std::uint32_t flags{};
        RenderStage stage{ RenderStage::PanelTexture };
        std::uint32_t reserved{};
        std::uint64_t grantedAccess{ kPanelAccess };
        std::uint8_t open{}, closing{}, cooperative{};
        std::uint8_t reservedBytes[5]{};
    };

    struct Snapshot
    {
        std::uint32_t size{ sizeof(Snapshot) };
        std::uint32_t version{ kRecordVersion };
        HookStatus hookStatus{ HookStatus::NotInstalled };
        std::uint32_t consumerCount{}, panelCount{};
        std::uint8_t frameworkReady{}, suppressedHands{}; // Left bit 0, right bit 1.
        std::uint8_t reservedBytes[2]{};
        std::uint64_t focusedPanelHandle{}, resizingPanelHandle{};
        std::uint64_t hostExclusiveAccess{ kHostExclusiveAccess };
        char hostId[64]{ "rps.ui.framework" };
        ConsumerDirectoryEntry consumers[kSnapshotConsumers]{};
        // Sorted by (sortOrder, panelHandle), matching composition order.
        PanelDirectoryEntry panels[kSnapshotPanels]{};
    };

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        Result(RPSUI_CALL* registerPanel)(std::uint64_t ownerToken, const PanelRegistration* registration,
            PanelAgreement* agreement) noexcept {};
        Result(RPSUI_CALL* getSnapshot)(Snapshot* snapshot) noexcept {};
        // Nonblocking: CallbackBusy closes admission immediately. Retry on a
        // later control tick; free callback data only after Ok. Never spin.
        Result(RPSUI_CALL* unregisterPanelSafely)(std::uint64_t ownerToken, std::uint64_t panelHandle) noexcept {};
        Result(RPSUI_CALL* unregisterConsumerSafely)(std::uint64_t ownerToken) noexcept {};
    };

    static_assert(std::is_standard_layout_v<PanelRegistration> && sizeof(PanelRegistration) == 304);
    static_assert(std::is_standard_layout_v<PanelAgreement> && sizeof(PanelAgreement) == 96);
    static_assert(std::is_standard_layout_v<ConsumerDirectoryEntry> && sizeof(ConsumerDirectoryEntry) == 168);
    static_assert(std::is_standard_layout_v<PanelDirectoryEntry> && sizeof(PanelDirectoryEntry) == 272);
    static_assert(std::is_standard_layout_v<Snapshot> && sizeof(Snapshot) == 4976);
    static_assert(offsetof(Snapshot, consumers) == 112 && offsetof(Snapshot, panels) == 2800);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 32);
}
#pragma pack(pop)
