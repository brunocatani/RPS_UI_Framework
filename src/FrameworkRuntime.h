#pragma once

#include "ContextualScrollPolicy.h"
#include "GripGesture.h"
#include "PanelMovePolicy.h"
#include "PanelResizePolicy.h"
#include "PointerClickGate.h"
#include "PointerHandSelection.h"
#include "PointerPanelIntersection.h"
#include "PanelCooperation.h"
#include "RegistryCapacity.h"
#include <RPS_UI_Framework/Registry.h>

#include <RPS_UI_Framework/Framework.h>
#include <RPS_UI_Framework/Input.h>
#include <RPS_UI_Framework/NumberOverlay.h>

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
    // A number overlay's style fields, copied from its last presentation.
    struct NumberOverlayLook
    {
        std::array<float, 3> color{ 1.0f, 1.0f, 1.0f };
        std::uint32_t secondaryValue{ 0 };
        rpsui::api::number_overlay::Style style{ rpsui::api::number_overlay::Style::Plain };
        rpsui::api::number_overlay::ChamberMarker chamber{ rpsui::api::number_overlay::ChamberMarker::None };
    };

    struct RenderPanelSnapshot
    {
        std::uint64_t panelHandle{ 0 };
        rpsui::api::framework::PanelSetupCallback setupCallback{ nullptr };
        rpsui::api::framework::PanelDrawCallback drawCallback{ nullptr };
        void* userData{ nullptr };
        std::shared_ptr<PanelCallbackGate> callbackGate;
        float layoutWidth{ 0.0f };
        float layoutHeight{ 0.0f };
        float defaultPhysicalWidth{ 0.0f };
        std::int32_t sortOrder{ 0 };
        std::uint32_t flags{ 0 };
        bool numberOverlay{};
        std::uint32_t number{};
        float numberOpacity{0.55f};
        NumberOverlayLook numberLook{};
        rpsui::api::framework::PanelPose pose{};
        rpsui::api::framework::PhysicalHand pointerHand{
            rpsui::api::framework::PhysicalHand::None
        };
        rpsui::api::framework::ResizeHandle hoveredResizeHandle{
            rpsui::api::framework::ResizeHandle::None
        };
        rpsui::api::framework::ResizeHandle activeResizeHandle{
            rpsui::api::framework::ResizeHandle::None
        };
        // Layout units.
        float pointerX{ 0.0f };
        float pointerY{ 0.0f };
        float scrollAxisX{ 0.0f };
        float scrollAxisY{ 0.0f };
        bool pointerValid{ false };
        bool primaryDown{ false };
        // Rises once for each Back press.
        std::uint64_t backSequence{ 0 };
        std::uint64_t stateSequence{ 0 };
    };

    struct RenderPanelBatch
    {
        // Owned by the renderer and reused: never zero 1024 slots on every frame.
        std::array<RenderPanelSnapshot, kPanelCapacity> panels{};
        std::size_t count{ 0 };

        void clear() noexcept
        {
            for (std::size_t i = 0; i < count; ++i) panels[i].callbackGate.reset();
            count = 0;
        }

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

        bool start() noexcept;
        void handleInputFrame(const api::input::Frame& snapshot) noexcept;
        void setRendererReady(bool ready) noexcept;
        void setHookStatus(api::cooperation::HookStatus status) noexcept;
        [[nodiscard]] bool isReady() const noexcept;
        [[nodiscard]] bool hasOpenPanels() const noexcept;
        void snapshotRenderPanels(RenderPanelBatch& out) const noexcept;

        [[nodiscard]] rpsui::api::Result registerConsumer(
            const rpsui::api::framework::ConsumerRegistration& registration,
            rpsui::api::framework::ConsumerHandle& outHandle) noexcept;
        [[nodiscard]] rpsui::api::Result unregisterConsumer(
            std::uint64_t ownerToken) noexcept;
        [[nodiscard]] rpsui::api::Result registerPanel(
            std::uint64_t ownerToken,
            const rpsui::api::framework::PanelRegistration& registration,
            std::uint64_t& outPanelHandle) noexcept;
        [[nodiscard]] rpsui::api::Result unregisterPanel(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) noexcept;
        [[nodiscard]] rpsui::api::Result submitPanelPresentation(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle,
            const rpsui::api::framework::PanelPresentation& presentation) noexcept;
        [[nodiscard]] rpsui::api::Result resetPanelLayout(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) noexcept;
        [[nodiscard]] rpsui::api::Result pulseHaptic(
            std::uint64_t ownerToken,
            rpsui::api::framework::PhysicalHand hand,
            float strength) const noexcept;
        [[nodiscard]] rpsui::api::Result getPanelState(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle,
            rpsui::api::framework::PanelState& outState) const noexcept;

        api::Result registerCooperativePanel(std::uint64_t ownerToken,
            const api::cooperation::PanelRegistration&, api::cooperation::PanelAgreement&) noexcept;
        api::Result getCooperationSnapshot(api::cooperation::Snapshot&) const noexcept;
        api::Result getRegistryPage(const api::registry::Query&, api::registry::Page&) const noexcept;
        api::Result unregisterPanelSafely(std::uint64_t ownerToken, std::uint64_t panelHandle) noexcept;
        api::Result unregisterConsumerSafely(std::uint64_t ownerToken) noexcept;
        api::Result registerNumberOverlay(std::uint64_t ownerToken,
            const api::number_overlay::Registration&,std::uint64_t& outHandle) noexcept;
        api::Result registerPassivePanel(std::uint64_t ownerToken,
            const api::framework::PanelRegistration&, std::uint64_t& outHandle) noexcept;
        api::Result registerFocusedPanel(std::uint64_t ownerToken,
            const api::framework::PanelRegistration&, std::uint64_t& outHandle) noexcept;
        api::Result submitNumberOverlay(std::uint64_t ownerToken,std::uint64_t handle,
            const api::number_overlay::Presentation&) noexcept;

        FrameworkRuntime(const FrameworkRuntime&) = delete;
        FrameworkRuntime& operator=(const FrameworkRuntime&) = delete;

    private:
        struct ConsumerRecord
        {
            std::uint64_t ownerToken{ 0 };
            std::string consumerId;
            std::string displayName;
            bool closing{ false };
        };

        struct PanelInput
        {
            rpsui::api::framework::PhysicalHand hand{
                rpsui::api::framework::PhysicalHand::None
            };
            rpsui::api::framework::ResizeHandle hovered{
                rpsui::api::framework::ResizeHandle::None
            };
            rpsui::api::framework::ResizeHandle active{
                rpsui::api::framework::ResizeHandle::None
            };
            // Layout units.
            float x{ 0.0f };
            float y{ 0.0f };
            float scrollX{ 0.0f };
            float scrollY{ 0.0f };
            bool valid{ false };
            bool primaryDown{ false };
        };

        struct PanelRecord
        {
            std::uint64_t ownerToken{ 0 };
            std::uint64_t panelHandle{ 0 };
            std::string panelId;
            std::string displayName;
            float defaultPhysicalWidth{ 0.0f };
            float minimumPhysicalWidth{ 0.0f };
            float maximumPhysicalWidth{ 0.0f };
            std::int32_t sortOrder{ 0 };
            std::uint32_t flags{ 0 };
            bool numberOverlay{}, passive{}, focusedInput{};
            std::uint32_t number{};
            float numberOpacity{0.55f};
            NumberOverlayLook numberLook{};
            float layoutWidth{ 0.0f };
            float layoutHeight{ 0.0f };
            rpsui::api::framework::PanelSetupCallback setupCallback{ nullptr };
            rpsui::api::framework::PanelDrawCallback drawCallback{ nullptr };
            void* userData{ nullptr };
            std::shared_ptr<PanelCallbackGate> callbackGate;
            rpsui::api::framework::PanelPose pose{};
            // Where the owner last presented it; resetPanelLayout returns here.
            rpsui::api::framework::PanelPose presentedPose{};
            PanelInput input{};
            std::uint64_t backSequence{ 0 };
            std::uint64_t submittedSequence{ 0 };
            std::uint64_t stateSequence{ 1 };
            bool open{ false };
            bool closing{ false };
            bool cooperative{ false };
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
            // The hit panel takes the grip: Back, a move, or both.
            bool gripPanel{ false };
            bool rawBackAvailable{ false };
            bool rawBackDown{ false };
            bool rawGripDown{ false };
            bool submittedBackDown{ false };
        };

        struct HandState
        {
            pointer_click_gate::State clickGate{};
            pointer_click_gate::State backGate{};
            bool rawPrimaryPrevious{ false };
            bool rawGripPrevious{ false };
            bool gameplayPressLatched{ false };
            std::uint64_t focusedPanel{};
            // Presses a panel consumed, still physically held: the hand stays captured until
            // release so the rest of the press never reaches the game. ROCK reads any capture
            // on a hand as the whole hand being the UI's, so this includes its trigger.
            bool focusedPressDrain{};
            bool backPressDrain{};
            grip_gesture::State grip{};
        };

        struct ActiveResize
        {
            bool active{ false };
            std::uint64_t panelHandle{ 0 };
            pointer_hand_selection::Hand hand{
                pointer_hand_selection::Hand::None
            };
            panel_resize::Drag drag{};
            rpsui::api::framework::PanelPose initialPose{};
        };

        struct ActiveMove
        {
            bool active{ false };
            std::uint64_t panelHandle{ 0 };
            pointer_hand_selection::Hand hand{ pointer_hand_selection::Hand::None };
            panel_move::Grab grab{};
            // Layout units: where the pointer holds the panel.
            float pointerX{ 0.0f };
            float pointerY{ 0.0f };
            double lastSeconds{ 0.0 };
        };

        FrameworkRuntime();
        ~FrameworkRuntime() = default;

        api::Result registerPanelImpl(std::uint64_t ownerToken,
            const api::framework::PanelRegistration&, std::uint64_t& outPanelHandle, bool cooperative,
            api::cooperation::PanelAgreement* agreement = nullptr, bool numberOverlay = false, bool passive = false,
            bool focusedInput = false) noexcept;
        api::Result unregisterPanelImpl(std::uint64_t ownerToken, std::uint64_t panelHandle, bool drain) noexcept;
        api::Result unregisterConsumerImpl(std::uint64_t ownerToken, bool drain) noexcept;

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
            const rpsui::api::framework::PanelPose& pose,
            const PanelRecord& panel) const noexcept;
        void separateNewPanelLocked(PanelRecord& panel) noexcept;
        void updateDepthRequestLocked() const noexcept;
        void setPanelOpenLocked(PanelRecord& panel, bool open) noexcept;
        void rebuildOpenPanelsLocked() noexcept;
        void updatePointerLocked(
            const api::input::Frame& snapshot) noexcept;
        [[nodiscard]] bool requestInputSuppressionLocked(
            const api::input::Frame& snapshot,
            pointer_hand_selection::Hand hand) noexcept;
        void clearInputSuppressionLocked(
            pointer_hand_selection::Hand hand) noexcept;
        void clearAllInputSuppressionLocked() noexcept;
        void clearPointerStateLocked() noexcept;

        mutable std::mutex mutex_;
        std::vector<ConsumerRecord> consumers_;
        std::vector<PanelRecord> panels_;
        // Sorted registry indices. Only open panels participate in per-frame work.
        // Capacity is reserved once; removals rebuild indices before releasing mutex_.
        std::vector<std::size_t> openPanels_;
        std::uint64_t registryRevision_{ 1 };
        std::uint64_t nextOwnerToken_{ 1 };
        std::uint64_t nextPanelHandle_{ 1 };
        std::atomic_bool started_{ false };
        std::atomic_bool rendererReady_{ false };
        std::atomic<api::cooperation::HookStatus> hookStatus_{ api::cooperation::HookStatus::NotInstalled };

        std::array<HandState, 2> handState_{};
        std::array<contextual_scroll::State, 2> scrollState_{};
        pointer_hand_selection::State pointerSelection_{};
        std::uint64_t pointerPanelHandle_{ 0 };
        std::uint64_t pointerTracePanel_{ 0 };
        unsigned pointerTraceSamples_{ 0 };
        double pointerTraceNextTime_{ 0 };
        ActiveResize activeResize_{};
        ActiveMove activeMove_{};
        std::uint8_t suppressedHands_{ 0 };
        bool numberInputReady_{};
    };
}
