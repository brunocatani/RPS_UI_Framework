#pragma once

#include "ContextualScrollPolicy.h"
#include "PanelResizePolicy.h"
#include "PointerClickGate.h"
#include "PointerHandSelection.h"
#include "PointerPanelIntersection.h"

#include "RPSUIFrameworkApi.h"
#include "ROCKProviderApi.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace rpsui
{
    struct RenderPanelSnapshot
    {
        std::uint64_t panelHandle{ 0 };
        rpsui::sdk::PanelRenderCallbackV1 renderCallback{ nullptr };
        void* userData{ nullptr };
        std::uint32_t pixelWidth{ 0 };
        std::uint32_t pixelHeight{ 0 };
        std::int32_t sortOrder{ 0 };
        std::uint32_t flags{ 0 };
        rpsui::sdk::PanelPoseV1 pose{};
        rpsui::sdk::PhysicalHandV1 pointerHand{
            rpsui::sdk::PhysicalHandV1::None
        };
        rpsui::sdk::ResizeHandleV1 hoveredResizeHandle{
            rpsui::sdk::ResizeHandleV1::None
        };
        rpsui::sdk::ResizeHandleV1 activeResizeHandle{
            rpsui::sdk::ResizeHandleV1::None
        };
        float pointerPixelX{ 0.0f };
        float pointerPixelY{ 0.0f };
        float scrollAxisX{ 0.0f };
        float scrollAxisY{ 0.0f };
        bool pointerValid{ false };
        bool primaryDown{ false };
        bool backDown{ false };
        std::uint64_t stateSequence{ 0 };
    };

    struct RenderPanelBatch
    {
        std::array<RenderPanelSnapshot, rpsui::sdk::RPSUI_MAX_PANELS> panels{};
        std::size_t count{ 0 };

        [[nodiscard]] bool empty() const noexcept
        {
            return count == 0;
        }

        [[nodiscard]] auto begin() const noexcept
        {
            return panels.begin();
        }

        [[nodiscard]] auto end() const noexcept
        {
            return panels.begin() + static_cast<std::ptrdiff_t>(count);
        }
    };

    class FrameworkRuntime final
    {
    public:
        static FrameworkRuntime& get() noexcept;

        void start() noexcept;
        void setRendererReady(bool ready) noexcept;
        [[nodiscard]] bool isReady() const noexcept;
        [[nodiscard]] bool hasOpenPanels() const noexcept;
        [[nodiscard]] RenderPanelBatch snapshotRenderPanels() const noexcept;

        [[nodiscard]] rpsui::sdk::ResultV1 registerConsumer(
            const rpsui::sdk::ConsumerRegistrationV1& registration,
            rpsui::sdk::ConsumerHandleV1& outHandle) noexcept;
        [[nodiscard]] rpsui::sdk::ResultV1 unregisterConsumer(
            std::uint64_t ownerToken) noexcept;
        [[nodiscard]] rpsui::sdk::ResultV1 registerPanel(
            std::uint64_t ownerToken,
            const rpsui::sdk::PanelRegistrationV1& registration,
            std::uint64_t& outPanelHandle) noexcept;
        [[nodiscard]] rpsui::sdk::ResultV1 unregisterPanel(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) noexcept;
        [[nodiscard]] rpsui::sdk::ResultV1 submitPanelPresentation(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle,
            const rpsui::sdk::PanelPresentationV1& presentation) noexcept;
        [[nodiscard]] rpsui::sdk::ResultV1 resetPanelSize(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) noexcept;
        [[nodiscard]] rpsui::sdk::ResultV1 getPanelState(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle,
            rpsui::sdk::PanelStateV1& outState) const noexcept;

        FrameworkRuntime(const FrameworkRuntime&) = delete;
        FrameworkRuntime& operator=(const FrameworkRuntime&) = delete;

    private:
        struct ConsumerRecord
        {
            std::uint64_t ownerToken{ 0 };
            std::string consumerId;
            std::string displayName;
            std::uint64_t grantedFeatures{ 0 };
        };

        struct PanelInput
        {
            rpsui::sdk::PhysicalHandV1 hand{
                rpsui::sdk::PhysicalHandV1::None
            };
            rpsui::sdk::ResizeHandleV1 hovered{
                rpsui::sdk::ResizeHandleV1::None
            };
            rpsui::sdk::ResizeHandleV1 active{
                rpsui::sdk::ResizeHandleV1::None
            };
            float pixelX{ 0.0f };
            float pixelY{ 0.0f };
            float scrollX{ 0.0f };
            float scrollY{ 0.0f };
            bool valid{ false };
            bool primaryDown{ false };
            bool backDown{ false };
        };

        struct PanelRecord
        {
            std::uint64_t ownerToken{ 0 };
            std::uint64_t panelHandle{ 0 };
            std::string panelId;
            std::string displayName;
            std::uint32_t pixelWidth{ 0 };
            std::uint32_t pixelHeight{ 0 };
            float defaultPhysicalWidth{ 0.0f };
            float minimumPhysicalWidth{ 0.0f };
            float maximumPhysicalWidth{ 0.0f };
            std::int32_t sortOrder{ 0 };
            std::uint32_t flags{ 0 };
            rpsui::sdk::PanelRenderCallbackV1 renderCallback{ nullptr };
            void* userData{ nullptr };
            rpsui::sdk::PanelPoseV1 pose{};
            PanelInput input{};
            std::uint64_t submittedSequence{ 0 };
            std::uint64_t stateSequence{ 1 };
            bool open{ false };
        };

        struct HandSample
        {
            pointer_panel_intersection::Ray ray{};
            pointer_panel_intersection::Hit hit{};
            std::uint64_t hitPanel{ 0 };
            float hitDistance{ 0.0f };
            bool rayValid{ false };
            bool rawAvailable{ false };
            bool rawPrimaryDown{ false };
            bool leaseAccepted{ false };
            bool submittedPrimaryDown{ false };
            contextual_scroll::Stick stick{};
            bool configNavigation{ false };
            bool rawBackAvailable{ false };
            bool rawBackDown{ false };
            bool submittedBackDown{ false };
        };

        struct HandState
        {
            pointer_click_gate::State clickGate{};
            pointer_click_gate::State backGate{};
            bool rawPrimaryPrevious{ false };
            bool gameplayPressLatched{ false };
        };

        struct ActiveResize
        {
            bool active{ false };
            std::uint64_t panelHandle{ 0 };
            pointer_hand_selection::Hand hand{
                pointer_hand_selection::Hand::None
            };
            panel_resize::Drag drag{};
            rpsui::sdk::PanelPoseV1 initialPose{};
        };

        FrameworkRuntime() = default;
        ~FrameworkRuntime() = default;

        static void ROCK_PROVIDER_CALL onRockFrame(
            const rock::provider::RockProviderFrameSnapshot* snapshot,
            void* userData) noexcept;

        void handleRockFrame(
            const rock::provider::RockProviderFrameSnapshot& snapshot) noexcept;
        [[nodiscard]] bool connectRockProvider(bool logFailure) noexcept;
        void discoveryLoop(std::stop_token stopToken) noexcept;

        [[nodiscard]] PanelRecord* findPanelLocked(
            std::uint64_t panelHandle) noexcept;
        [[nodiscard]] const PanelRecord* findPanelLocked(
            std::uint64_t panelHandle) const noexcept;
        [[nodiscard]] bool hasConsumerLocked(
            std::uint64_t ownerToken) const noexcept;
        [[nodiscard]] bool ownerMatchesLocked(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) const noexcept;
        [[nodiscard]] bool validatePose(
            const rpsui::sdk::PanelPoseV1& pose,
            const PanelRecord& panel) const noexcept;
        void separateNewPanelLocked(PanelRecord& panel) noexcept;
        void updateDepthRequestLocked() const noexcept;
        void updatePointerLocked(
            const rock::provider::RockProviderFrameSnapshot& snapshot) noexcept;
        [[nodiscard]] bool requestInputSuppressionLocked(
            const rock::provider::RockProviderFrameSnapshot& snapshot,
            rock::provider::RockProviderHand hand) noexcept;
        void clearInputSuppressionLocked(
            rock::provider::RockProviderHand hand) noexcept;
        void clearAllInputSuppressionLocked() noexcept;
        void clearPointerStateLocked() noexcept;

        mutable std::mutex mutex_;
        std::vector<ConsumerRecord> consumers_;
        std::vector<PanelRecord> panels_;
        std::uint64_t nextOwnerToken_{ 1 };
        std::uint64_t nextPanelHandle_{ 1 };
        std::atomic_bool started_{ false };
        std::atomic_bool rendererReady_{ false };

        std::uint64_t providerOwnerToken_{ 0 };
        std::uint64_t providerCallbackToken_{ 0 };
        std::jthread discoveryThread_;
        std::array<HandState, 2> handState_{};
        std::array<contextual_scroll::State, 2> scrollState_{};
        pointer_hand_selection::State pointerSelection_{};
        std::uint64_t pointerPanelHandle_{ 0 };
        ActiveResize activeResize_{};
        std::uint8_t suppressedHands_{ 0 };
    };
}
