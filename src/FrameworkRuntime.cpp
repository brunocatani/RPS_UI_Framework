#include "PCH.h"

#include "FrameworkRuntime.h"
#include "PanelSeparationPolicy.h"

#include "Logger.h"
#include "render/SceneDepthCapture.h"

#include "vrcf/VRControllersManager.h"

namespace rpsui
{
    namespace
    {
        using rock::provider::RockProviderApi;
        using rock::provider::RockProviderConsumerCapabilityV1;
        using rock::provider::RockProviderConsumerHandleV1;
        using rock::provider::RockProviderConsumerRegistrationV1;
        using rock::provider::RockProviderFrameSnapshot;
        using rock::provider::RockProviderHand;
        using rock::provider::RockProviderHandInputSuppressionFlagV1;
        using rock::provider::RockProviderHandInputSuppressionRequestV1;
        using rock::provider::RockProviderLifecycleFlag;
        using rock::provider::RockProviderLimitsV1;
        using rock::provider::RockProviderRawWandButtonStateV1;
        using rock::provider::RockProviderResultV1;

        constexpr std::uint32_t kTriggerButton =
            static_cast<std::uint32_t>(f4cf::vrcf::k_EButton_SteamVR_Trigger);
        constexpr std::uint32_t kFaceButton =
            static_cast<std::uint32_t>(f4cf::vrcf::k_EButton_A);
        constexpr std::uint32_t kSuppressionFlags =
            static_cast<std::uint32_t>(
                RockProviderHandInputSuppressionFlagV1::SuppressConfigModeChord) |
            static_cast<std::uint32_t>(
                RockProviderHandInputSuppressionFlagV1::SuppressOpenVrGameInput) |
            static_cast<std::uint32_t>(
                RockProviderHandInputSuppressionFlagV1::SuppressNativeVats) |
            static_cast<std::uint32_t>(
                RockProviderHandInputSuppressionFlagV1::SuppressNativeVans);
        constexpr std::uint32_t kSuppressionLeaseFrames = 3;
        constexpr float kPointerMaximumDistance = 500.0f;
        constexpr std::uint64_t kAllFeatures =
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::MultipleWorldPanels) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::SceneDepthOcclusion) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::CentralPointerRouting) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::ContextualTwoAxisScroll) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::PhysicalPanelResize) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::ConsumerRenderCallbacks) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::SharedStereoComposition) |
            rpsui::sdk::featureMask(rpsui::sdk::FeatureV1::ShapedPanels);

        [[nodiscard]] bool inputReady(const RockProviderFrameSnapshot& snapshot) noexcept
        {
            return snapshot.providerReady != 0 &&
                   snapshot.menuBlocking == 0 &&
                   snapshot.configBlocking == 0 &&
                   rock::provider::hasLifecycleFlag(
                       snapshot.lifecycleFlags,
                       RockProviderLifecycleFlag::WorldAvailable) &&
                   rock::provider::hasLifecycleFlag(
                       snapshot.lifecycleFlags,
                       RockProviderLifecycleFlag::SkeletonReady) &&
                   rock::provider::hasLifecycleFlag(
                       snapshot.lifecycleFlags,
                       RockProviderLifecycleFlag::ProviderReady);
        }

        [[nodiscard]] constexpr std::size_t handIndex(RockProviderHand hand) noexcept
        {
            return hand == RockProviderHand::Left ? 0u : 1u;
        }

        [[nodiscard]] constexpr RockProviderHand rockHand(std::size_t index) noexcept
        {
            return index == 0 ? RockProviderHand::Left : RockProviderHand::Right;
        }

        [[nodiscard]] constexpr pointer_hand_selection::Hand policyHand(
            RockProviderHand hand) noexcept
        {
            return hand == RockProviderHand::Left ?
                pointer_hand_selection::Hand::Left :
                pointer_hand_selection::Hand::Right;
        }

        [[nodiscard]] constexpr RockProviderHand rockHand(
            pointer_hand_selection::Hand hand) noexcept
        {
            return hand == pointer_hand_selection::Hand::Left ?
                RockProviderHand::Left :
                RockProviderHand::Right;
        }

        [[nodiscard]] constexpr rpsui::sdk::PhysicalHandV1 sdkHand(
            pointer_hand_selection::Hand hand) noexcept
        {
            if (hand == pointer_hand_selection::Hand::Left) {
                return rpsui::sdk::PhysicalHandV1::Left;
            }
            if (hand == pointer_hand_selection::Hand::Right) {
                return rpsui::sdk::PhysicalHandV1::Right;
            }
            return rpsui::sdk::PhysicalHandV1::None;
        }

        [[nodiscard]] constexpr f4cf::vrcf::Hand controllerHand(
            pointer_hand_selection::Hand hand) noexcept
        {
            return hand == pointer_hand_selection::Hand::Left ?
                f4cf::vrcf::Hand::Left :
                f4cf::vrcf::Hand::Right;
        }

        [[nodiscard]] constexpr std::uint8_t suppressionBit(
            RockProviderHand hand) noexcept
        {
            return hand == RockProviderHand::Left ?
                std::uint8_t{ 1u << 0 } :
                std::uint8_t{ 1u << 1 };
        }

        [[nodiscard]] std::string boundedString(
            const char* value,
            std::size_t capacity)
        {
            if (!value || capacity == 0) {
                return {};
            }
            const auto* end = static_cast<const char*>(
                std::memchr(value, '\0', capacity));
            return end ? std::string(value, end) : std::string{};
        }

        [[nodiscard]] float dot3(const float* left, const float* right) noexcept
        {
            return left[0] * right[0] +
                   left[1] * right[1] +
                   left[2] * right[2];
        }

        [[nodiscard]] float lengthSquared3(const float* value) noexcept
        {
            return dot3(value, value);
        }

        [[nodiscard]] float handedness3(
            const float* right,
            const float* up,
            const float* front) noexcept
        {
            const float crossUpFront[3]{
                up[1] * front[2] - up[2] * front[1],
                up[2] * front[0] - up[0] * front[2],
                up[0] * front[1] - up[1] * front[0],
            };
            return dot3(crossUpFront, right);
        }

        [[nodiscard]] rpsui::sdk::ResizeHandleV1 sdkResizeHandle(
            panel_resize::Handle handle) noexcept
        {
            return static_cast<rpsui::sdk::ResizeHandleV1>(
                static_cast<std::uint32_t>(handle));
        }

        [[nodiscard]] pointer_panel_intersection::Panel intersectionPanel(
            const rpsui::sdk::PanelPoseV1& pose) noexcept
        {
            return {
                .center = { pose.center[0], pose.center[1], pose.center[2] },
                .right = { pose.right[0], pose.right[1], pose.right[2] },
                .up = { pose.up[0], pose.up[1], pose.up[2] },
                .front = { pose.front[0], pose.front[1], pose.front[2] },
                .width = pose.physicalWidth,
                .height = pose.physicalHeight,
            };
        }
    }

    FrameworkRuntime& FrameworkRuntime::get() noexcept
    {
        static auto* runtime = new FrameworkRuntime();
        return *runtime;
    }

    void FrameworkRuntime::start() noexcept
    {
        if (started_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        discoveryThread_ = std::jthread(
            [this](std::stop_token stopToken) {
                discoveryLoop(stopToken);
            });
        log::info("RPS UI Framework runtime started");
    }

    void FrameworkRuntime::setRendererReady(bool ready) noexcept
    {
        rendererReady_.store(ready, std::memory_order_release);
    }

    bool FrameworkRuntime::isReady() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return rendererReady_.load(std::memory_order_acquire) &&
               providerCallbackToken_ != 0;
    }

    bool FrameworkRuntime::hasOpenPanels() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return std::any_of(
            panels_.begin(),
            panels_.end(),
            [](const PanelRecord& panel) {
                return panel.open;
            });
    }

    RenderPanelBatch FrameworkRuntime::snapshotRenderPanels() const noexcept
    {
        std::scoped_lock lock(mutex_);
        RenderPanelBatch snapshots;
        for (const auto& panel : panels_) {
            if (!panel.open || !panel.renderCallback) {
                continue;
            }
            if (snapshots.count >= snapshots.panels.size()) {
                break;
            }
            snapshots.panels[snapshots.count++] = {
                .panelHandle = panel.panelHandle,
                .renderCallback = panel.renderCallback,
                .userData = panel.userData,
                .pixelWidth = panel.pixelWidth,
                .pixelHeight = panel.pixelHeight,
                .sortOrder = panel.sortOrder,
                .flags = panel.flags,
                .pose = panel.pose,
                .pointerHand = panel.input.hand,
                .hoveredResizeHandle = panel.input.hovered,
                .activeResizeHandle = panel.input.active,
                .pointerPixelX = panel.input.pixelX,
                .pointerPixelY = panel.input.pixelY,
                .scrollAxisX = panel.input.scrollX,
                .scrollAxisY = panel.input.scrollY,
                .pointerValid = panel.input.valid,
                .primaryDown = panel.input.primaryDown,
                .stateSequence = panel.stateSequence,
            };
        }
        std::sort(
            snapshots.panels.begin(),
            snapshots.panels.begin() +
                static_cast<std::ptrdiff_t>(snapshots.count),
            [](const RenderPanelSnapshot& left, const RenderPanelSnapshot& right) {
                if (left.sortOrder != right.sortOrder) {
                    return left.sortOrder < right.sortOrder;
                }
                return left.panelHandle < right.panelHandle;
            });
        return snapshots;
    }

    bool FrameworkRuntime::hasConsumerLocked(std::uint64_t ownerToken) const noexcept
    {
        return ownerToken != 0 &&
               std::any_of(
                   consumers_.begin(),
                   consumers_.end(),
                   [ownerToken](const ConsumerRecord& consumer) {
                       return consumer.ownerToken == ownerToken;
                   });
    }

    FrameworkRuntime::PanelRecord* FrameworkRuntime::findPanelLocked(
        std::uint64_t panelHandle) noexcept
    {
        const auto found = std::find_if(
            panels_.begin(),
            panels_.end(),
            [panelHandle](const PanelRecord& panel) {
                return panel.panelHandle == panelHandle;
            });
        return found != panels_.end() ? std::addressof(*found) : nullptr;
    }

    const FrameworkRuntime::PanelRecord* FrameworkRuntime::findPanelLocked(
        std::uint64_t panelHandle) const noexcept
    {
        const auto found = std::find_if(
            panels_.begin(),
            panels_.end(),
            [panelHandle](const PanelRecord& panel) {
                return panel.panelHandle == panelHandle;
            });
        return found != panels_.end() ? std::addressof(*found) : nullptr;
    }

    bool FrameworkRuntime::ownerMatchesLocked(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) const noexcept
    {
        const auto* panel = findPanelLocked(panelHandle);
        return panel && panel->ownerToken == ownerToken;
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::registerConsumer(
        const rpsui::sdk::ConsumerRegistrationV1& registration,
        rpsui::sdk::ConsumerHandleV1& outHandle) noexcept
    {
        try {
            outHandle = {};
            if (registration.structSize < sizeof(registration) ||
                registration.apiVersion != rpsui::sdk::RPSUI_API_VERSION) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }
            const auto consumerId = boundedString(
                registration.consumerId,
                sizeof(registration.consumerId));
            const auto displayName = boundedString(
                registration.displayName,
                sizeof(registration.displayName));
            if (consumerId.empty() ||
                displayName.empty()) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }

            std::scoped_lock lock(mutex_);
            if (consumers_.size() >= rpsui::sdk::RPSUI_MAX_CONSUMERS) {
                return rpsui::sdk::ResultV1::CapacityReached;
            }
            if (std::any_of(
                    consumers_.begin(),
                    consumers_.end(),
                    [&](const ConsumerRecord& consumer) {
                        return consumer.consumerId == consumerId;
                    })) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }

            const std::uint64_t token = nextOwnerToken_++;
            const std::uint64_t granted =
                registration.requestedFeatures & kAllFeatures;
            consumers_.push_back({
                .ownerToken = token,
                .consumerId = consumerId,
                .displayName = displayName,
                .grantedFeatures = granted,
            });
            outHandle.ownerToken = token;
            outHandle.grantedFeatures = granted;
            log::info(
                "Registered UI consumer '{}' as owner {}",
                consumerId,
                token);
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::unregisterConsumer(
        std::uint64_t ownerToken) noexcept
    {
        try {
            std::scoped_lock lock(mutex_);
            const auto consumer = std::find_if(
                consumers_.begin(),
                consumers_.end(),
                [ownerToken](const ConsumerRecord& entry) {
                    return entry.ownerToken == ownerToken;
                });
            if (consumer == consumers_.end()) {
                return rpsui::sdk::ResultV1::OwnerNotRegistered;
            }

            panels_.erase(
                std::remove_if(
                    panels_.begin(),
                    panels_.end(),
                    [ownerToken](const PanelRecord& panel) {
                        return panel.ownerToken == ownerToken;
                    }),
                panels_.end());
            consumers_.erase(consumer);
            clearPointerStateLocked();
            updateDepthRequestLocked();
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::registerPanel(
        std::uint64_t ownerToken,
        const rpsui::sdk::PanelRegistrationV1& registration,
        std::uint64_t& outPanelHandle) noexcept
    {
        try {
            outPanelHandle = 0;
            if (registration.structSize < sizeof(registration) ||
                registration.apiVersion != rpsui::sdk::RPSUI_API_VERSION) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }
            const auto panelId = boundedString(
                registration.panelId,
                sizeof(registration.panelId));
            const auto displayName = boundedString(
                registration.displayName,
                sizeof(registration.displayName));
            const bool dimensionsValid =
                registration.pixelWidth >= 640 &&
                registration.pixelWidth <= 4096 &&
                registration.pixelHeight >= 480 &&
                registration.pixelHeight <= 4096;
            const bool widthsValid =
                std::isfinite(registration.defaultPhysicalWidth) &&
                std::isfinite(registration.minimumPhysicalWidth) &&
                std::isfinite(registration.maximumPhysicalWidth) &&
                registration.minimumPhysicalWidth > 0.0f &&
                registration.maximumPhysicalWidth >=
                    registration.minimumPhysicalWidth &&
                registration.defaultPhysicalWidth >=
                    registration.minimumPhysicalWidth &&
                registration.defaultPhysicalWidth <=
                    registration.maximumPhysicalWidth;
            if (panelId.empty() ||
                displayName.empty() ||
                !dimensionsValid ||
                !widthsValid ||
                (registration.flags & ~7u) != 0 ||
                !registration.renderCallback) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }

            std::scoped_lock lock(mutex_);
            if (!hasConsumerLocked(ownerToken)) {
                return rpsui::sdk::ResultV1::OwnerNotRegistered;
            }
            if (panels_.size() >= rpsui::sdk::RPSUI_MAX_PANELS) {
                return rpsui::sdk::ResultV1::CapacityReached;
            }
            if (std::any_of(
                    panels_.begin(),
                    panels_.end(),
                    [&](const PanelRecord& panel) {
                        return panel.panelId == panelId;
                    })) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }

            const std::uint64_t handle = nextPanelHandle_++;
            const float aspect =
                static_cast<float>(registration.pixelWidth) /
                static_cast<float>(registration.pixelHeight);
            PanelRecord panel{
                .ownerToken = ownerToken,
                .panelHandle = handle,
                .panelId = panelId,
                .displayName = displayName,
                .pixelWidth = registration.pixelWidth,
                .pixelHeight = registration.pixelHeight,
                .defaultPhysicalWidth = registration.defaultPhysicalWidth,
                .minimumPhysicalWidth = registration.minimumPhysicalWidth,
                .maximumPhysicalWidth = registration.maximumPhysicalWidth,
                .sortOrder = registration.sortOrder,
                .flags = registration.flags,
                .renderCallback = registration.renderCallback,
                .userData = registration.userData,
            };
            panel.pose.physicalWidth = registration.defaultPhysicalWidth;
            panel.pose.physicalHeight =
                registration.defaultPhysicalWidth / aspect;
            panels_.push_back(std::move(panel));
            outPanelHandle = handle;
            log::info(
                "Registered UI panel '{}' as handle {} for owner {}",
                panelId,
                handle,
                ownerToken);
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::unregisterPanel(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) noexcept
    {
        try {
            std::scoped_lock lock(mutex_);
            const auto panel = std::find_if(
                panels_.begin(),
                panels_.end(),
                [panelHandle](const PanelRecord& entry) {
                    return entry.panelHandle == panelHandle;
                });
            if (panel == panels_.end()) {
                return rpsui::sdk::ResultV1::PanelNotRegistered;
            }
            if (panel->ownerToken != ownerToken) {
                return rpsui::sdk::ResultV1::OwnershipMismatch;
            }
            const bool ownedInteraction =
                pointerPanelHandle_ == panelHandle ||
                activeResize_.panelHandle == panelHandle;
            panels_.erase(panel);
            if (ownedInteraction) {
                clearPointerStateLocked();
            }
            updateDepthRequestLocked();
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    bool FrameworkRuntime::validatePose(
        const rpsui::sdk::PanelPoseV1& pose,
        const PanelRecord& panel) const noexcept
    {
        const auto finite3 = [](const float* value) {
            return std::isfinite(value[0]) &&
                   std::isfinite(value[1]) &&
                   std::isfinite(value[2]);
        };
        constexpr float kMaximumWorldMagnitude = 1.0e8f;
        if (!finite3(pose.center) ||
            !finite3(pose.right) ||
            !finite3(pose.up) ||
            !finite3(pose.front) ||
            !std::isfinite(pose.physicalWidth) ||
            !std::isfinite(pose.physicalHeight) ||
            pose.physicalWidth < panel.minimumPhysicalWidth ||
            pose.physicalWidth > panel.maximumPhysicalWidth ||
            pose.physicalHeight <= 0.0f ||
            std::ranges::any_of(
                pose.center,
                [](float component) {
                    return std::fabs(component) >
                           kMaximumWorldMagnitude;
                })) {
            return false;
        }
        const float aspect =
            static_cast<float>(panel.pixelWidth) /
            static_cast<float>(panel.pixelHeight);
        return lengthSquared3(pose.right) > 0.99f &&
               lengthSquared3(pose.right) < 1.01f &&
               lengthSquared3(pose.up) > 0.99f &&
               lengthSquared3(pose.up) < 1.01f &&
               lengthSquared3(pose.front) > 0.99f &&
               lengthSquared3(pose.front) < 1.01f &&
               std::fabs(dot3(pose.right, pose.up)) < 0.01f &&
               std::fabs(dot3(pose.right, pose.front)) < 0.01f &&
               std::fabs(dot3(pose.up, pose.front)) < 0.01f &&
               handedness3(pose.right, pose.up, pose.front) > 0.99f &&
               std::fabs(
                   pose.physicalWidth / pose.physicalHeight - aspect) <
                   0.001f;
    }

    void FrameworkRuntime::separateNewPanelLocked(PanelRecord& panel) noexcept
    {
        for (const auto& other : panels_) {
            if (!other.open || other.panelHandle == panel.panelHandle) {
                continue;
            }
            if (panel_separation::haveInPlaneGap(panel.pose, other.pose)) {
                continue;
            }
            const float delta[3]{
                panel.pose.center[0] - other.pose.center[0],
                panel.pose.center[1] - other.pose.center[1],
                panel.pose.center[2] - other.pose.center[2],
            };
            const float separationSquared = lengthSquared3(delta);
            const float required =
                (panel.pose.physicalWidth + other.pose.physicalWidth) * 0.55f;
            if (separationSquared >= required * required) {
                continue;
            }
            const float direction =
                panel.sortOrder >= other.sortOrder ? 1.0f : -1.0f;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                panel.pose.center[axis] +=
                    panel.pose.right[axis] * required * direction;
            }
        }
    }

    void FrameworkRuntime::updateDepthRequestLocked() const noexcept
    {
        const bool requested = std::any_of(
            panels_.begin(),
            panels_.end(),
            [](const PanelRecord& panel) {
                return panel.open;
            });
        render::SceneDepthCapture::SetCaptureRequested(requested);
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::submitPanelPresentation(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle,
        const rpsui::sdk::PanelPresentationV1& presentation) noexcept
    {
        try {
            if (presentation.structSize < sizeof(presentation) ||
                presentation.apiVersion != rpsui::sdk::RPSUI_API_VERSION) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }
            std::scoped_lock lock(mutex_);
            auto* panel = findPanelLocked(panelHandle);
            if (!panel) {
                return rpsui::sdk::ResultV1::PanelNotRegistered;
            }
            if (panel->ownerToken != ownerToken) {
                return rpsui::sdk::ResultV1::OwnershipMismatch;
            }
            if (presentation.sequence != 0 &&
                presentation.sequence <= panel->submittedSequence) {
                return rpsui::sdk::ResultV1::InvalidArgument;
            }
            if (presentation.open != 0 &&
                !validatePose(presentation.pose, *panel)) {
                return rpsui::sdk::ResultV1::InvalidPose;
            }

            panel->open = presentation.open != 0;
            if (panel->open) {
                panel->pose = presentation.pose;
                separateNewPanelLocked(*panel);
                if (activeResize_.panelHandle == panelHandle) {
                    clearPointerStateLocked();
                }
            } else {
                panel->input = {};
                if (pointerPanelHandle_ == panelHandle ||
                    activeResize_.panelHandle == panelHandle) {
                    clearPointerStateLocked();
                }
            }
            panel->submittedSequence =
                presentation.sequence != 0 ?
                presentation.sequence :
                panel->submittedSequence + 1;
            ++panel->stateSequence;
            updateDepthRequestLocked();
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::resetPanelSize(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) noexcept
    {
        try {
            std::scoped_lock lock(mutex_);
            auto* panel = findPanelLocked(panelHandle);
            if (!panel) {
                return rpsui::sdk::ResultV1::PanelNotRegistered;
            }
            if (panel->ownerToken != ownerToken) {
                return rpsui::sdk::ResultV1::OwnershipMismatch;
            }
            const float aspect =
                static_cast<float>(panel->pixelWidth) /
                static_cast<float>(panel->pixelHeight);
            panel->pose.physicalWidth = panel->defaultPhysicalWidth;
            panel->pose.physicalHeight =
                panel->defaultPhysicalWidth / aspect;
            ++panel->stateSequence;
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    rpsui::sdk::ResultV1 FrameworkRuntime::getPanelState(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle,
        rpsui::sdk::PanelStateV1& outState) const noexcept
    {
        try {
            outState = {};
            std::scoped_lock lock(mutex_);
            const auto* panel = findPanelLocked(panelHandle);
            if (!panel) {
                return rpsui::sdk::ResultV1::PanelNotRegistered;
            }
            if (panel->ownerToken != ownerToken) {
                return rpsui::sdk::ResultV1::OwnershipMismatch;
            }
            outState.sequence = panel->stateSequence;
            outState.pose = panel->pose;
            outState.pointerHand = panel->input.hand;
            outState.hoveredResizeHandle = panel->input.hovered;
            outState.activeResizeHandle = panel->input.active;
            outState.open = panel->open ? 1 : 0;
            outState.pointerValid = panel->input.valid ? 1 : 0;
            outState.primaryDown = panel->input.primaryDown ? 1 : 0;
            return rpsui::sdk::ResultV1::Ok;
        } catch (...) {
            return rpsui::sdk::ResultV1::InternalError;
        }
    }

    bool FrameworkRuntime::connectRockProvider(bool logFailure) noexcept
    {
        try {
            {
                std::scoped_lock lock(mutex_);
                if (providerCallbackToken_ != 0) {
                    return true;
                }
            }
            const auto initialized = RockProviderApi::initialize(
                rock::provider::ROCK_PROVIDER_API_VERSION,
                rock::provider::ROCK_PROVIDER_API_V1_OWNER_FRAME_CALLBACKS_TABLE_BYTES);
            if (initialized != 0 || !RockProviderApi::inst) {
                if (logFailure) {
                    log::warn("ROCK provider unavailable ({})", initialized);
                }
                return false;
            }

            RockProviderLimitsV1 limits{};
            if (!RockProviderApi::inst->getProviderLimitsV1 ||
                !RockProviderApi::inst->getProviderLimitsV1(&limits) ||
                !rock::provider::supportsHandInputSuppressionV1(limits) ||
                !rock::provider::supportsRawWandButtonStateV1(limits) ||
                !rock::provider::supportsOwnerFrameCallbacksV1() ||
                !RockProviderApi::inst->registerConsumerV1 ||
                !RockProviderApi::inst->unregisterConsumerV1 ||
                !RockProviderApi::inst->registerFrameCallbackForOwnerV1 ||
                !RockProviderApi::inst->setHandInputSuppressionV1 ||
                !RockProviderApi::inst->clearHandInputSuppressionV1 ||
                !RockProviderApi::inst->getRawWandButtonStateV1) {
                if (logFailure) {
                    log::warn("ROCK provider lacks required UI input capabilities");
                }
                return false;
            }

            RockProviderConsumerRegistrationV1 registration{};
            std::snprintf(
                registration.modName,
                sizeof(registration.modName),
                "RPS UI Framework");
            registration.requestedCapabilities =
                static_cast<std::uint32_t>(
                    RockProviderConsumerCapabilityV1::FrameSnapshots) |
                static_cast<std::uint32_t>(
                    RockProviderConsumerCapabilityV1::HandInputSuppression);

            RockProviderConsumerHandleV1 handle{};
            const auto registered =
                RockProviderApi::inst->registerConsumerV1(
                    &registration,
                    &handle);
            if (registered != RockProviderResultV1::Ok ||
                handle.ownerToken == 0 ||
                !rock::provider::hasConsumerCapabilityV1(
                    handle.grantedCapabilities,
                    RockProviderConsumerCapabilityV1::FrameSnapshots) ||
                !rock::provider::hasConsumerCapabilityV1(
                    handle.grantedCapabilities,
                    RockProviderConsumerCapabilityV1::HandInputSuppression)) {
                if (handle.ownerToken != 0) {
                    (void)RockProviderApi::inst->unregisterConsumerV1(
                        handle.ownerToken);
                }
                if (logFailure) {
                    log::warn(
                        "ROCK provider rejected UI host registration ({})",
                        static_cast<std::uint32_t>(registered));
                }
                return false;
            }

            std::uint64_t callbackToken = 0;
            const auto callbackResult =
                RockProviderApi::inst->registerFrameCallbackForOwnerV1(
                    handle.ownerToken,
                    &FrameworkRuntime::onRockFrame,
                    this,
                    &callbackToken);
            if (callbackResult != RockProviderResultV1::Ok ||
                callbackToken == 0) {
                (void)RockProviderApi::inst->unregisterConsumerV1(
                    handle.ownerToken);
                if (logFailure) {
                    log::warn(
                        "ROCK provider rejected UI host frame callback ({})",
                        static_cast<std::uint32_t>(callbackResult));
                }
                return false;
            }
            {
                std::scoped_lock lock(mutex_);
                providerOwnerToken_ = handle.ownerToken;
                providerCallbackToken_ = callbackToken;
            }
            log::info(
                "RPS UI Framework connected to ROCK provider as owner {}",
                handle.ownerToken);
            return true;
        } catch (...) {
            return false;
        }
    }

    void FrameworkRuntime::discoveryLoop(std::stop_token stopToken) noexcept
    {
        for (std::uint32_t attempt = 0;
             !stopToken.stop_requested();
             ++attempt) {
            if (connectRockProvider(attempt == 0 || attempt % 10 == 9)) {
                return;
            }
            for (int slice = 0;
                 slice < 10 && !stopToken.stop_requested();
                 ++slice) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void ROCK_PROVIDER_CALL FrameworkRuntime::onRockFrame(
        const RockProviderFrameSnapshot* snapshot,
        void* userData) noexcept
    {
        if (snapshot && userData) {
            static_cast<FrameworkRuntime*>(userData)->handleRockFrame(*snapshot);
        }
    }

    bool FrameworkRuntime::requestInputSuppressionLocked(
        const RockProviderFrameSnapshot& snapshot,
        RockProviderHand hand) noexcept
    {
        if (providerOwnerToken_ == 0 ||
            !RockProviderApi::inst ||
            !RockProviderApi::inst->setHandInputSuppressionV1) {
            return false;
        }
        RockProviderHandInputSuppressionRequestV1 request{};
        request.hand = hand;
        request.flags = kSuppressionFlags;
        request.leaseFrames = kSuppressionLeaseFrames;
        request.worldGeneration = snapshot.worldGeneration;
        request.skeletonGeneration = snapshot.skeletonGeneration;
        request.providerGeneration = snapshot.providerGeneration;
        const bool accepted =
            RockProviderApi::inst->setHandInputSuppressionV1(
                providerOwnerToken_,
                &request) == RockProviderResultV1::Ok;
        if (accepted) {
            suppressedHands_ |= suppressionBit(hand);
        }
        return accepted;
    }

    void FrameworkRuntime::clearInputSuppressionLocked(
        RockProviderHand hand) noexcept
    {
        const auto bit = suppressionBit(hand);
        if ((suppressedHands_ & bit) == 0) {
            return;
        }
        if (providerOwnerToken_ != 0 &&
            RockProviderApi::inst &&
            RockProviderApi::inst->clearHandInputSuppressionV1) {
            (void)RockProviderApi::inst->clearHandInputSuppressionV1(
                providerOwnerToken_,
                hand);
        }
        suppressedHands_ &= static_cast<std::uint8_t>(~bit);
    }

    void FrameworkRuntime::clearAllInputSuppressionLocked() noexcept
    {
        clearInputSuppressionLocked(RockProviderHand::Left);
        clearInputSuppressionLocked(RockProviderHand::Right);
    }

    void FrameworkRuntime::clearPointerStateLocked() noexcept
    {
        for (auto& panel : panels_) {
            panel.input = {};
        }
        for (auto& state : handState_) {
            pointer_click_gate::reset(state.clickGate);
            state.rawPrimaryPrevious = false;
            state.gameplayPressLatched = false;
        }
        for (auto& scroll : scrollState_) {
            contextual_scroll::reset(scroll);
        }
        pointer_hand_selection::reset(pointerSelection_);
        pointerPanelHandle_ = 0;
        activeResize_ = {};
    }

    void FrameworkRuntime::updatePointerLocked(
        const RockProviderFrameSnapshot& snapshot) noexcept
    {
        for (auto& panel : panels_) {
            panel.input = {};
        }

        std::array<HandSample, 2> samples{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto hand = rockHand(index);
            const auto& transform =
                hand == RockProviderHand::Left ?
                snapshot.leftHandTransform :
                snapshot.rightHandTransform;
            const float directionLength = std::sqrt(
                transform.rotate[0] * transform.rotate[0] +
                transform.rotate[1] * transform.rotate[1] +
                transform.rotate[2] * transform.rotate[2]);
            auto& sample = samples[index];
            if (std::isfinite(directionLength) &&
                directionLength > 0.0001f) {
                sample.ray = {
                    .origin = {
                        transform.translate[0],
                        transform.translate[1],
                        transform.translate[2],
                    },
                    .direction = {
                        transform.rotate[0] / directionLength,
                        transform.rotate[1] / directionLength,
                        transform.rotate[2] / directionLength,
                    },
                    .maxDistance = kPointerMaximumDistance,
                };
                sample.rayValid =
                    pointer_panel_intersection::finite(sample.ray.origin) &&
                    pointer_panel_intersection::finite(sample.ray.direction);
            }

            if (sample.rayValid) {
                float nearest = (std::numeric_limits<float>::max)();
                for (const auto& panel : panels_) {
                    if (!panel.open) {
                        continue;
                    }
                    pointer_panel_intersection::Hit hit{};
                    if (pointer_panel_intersection::intersect(
                            sample.ray,
                            intersectionPanel(panel.pose),
                            hit) &&
                        hit.inside && sdk::panelContains(panel.flags, hit.u, hit.v) &&
                        hit.distance < nearest) {
                        nearest = hit.distance;
                        sample.hit = hit;
                        sample.hitPanel = panel.panelHandle;
                        sample.hitDistance = hit.distance;
                    }
                }
            }

            RockProviderRawWandButtonStateV1 trigger{};
            RockProviderRawWandButtonStateV1 face{};
            sample.rawAvailable =
                RockProviderApi::inst &&
                RockProviderApi::inst->getRawWandButtonStateV1 &&
                RockProviderApi::inst->getRawWandButtonStateV1(
                    hand,
                    kTriggerButton,
                    &trigger) &&
                RockProviderApi::inst->getRawWandButtonStateV1(
                    hand,
                    kFaceButton,
                    &face) &&
                trigger.available != 0 &&
                face.available != 0;
            sample.rawPrimaryDown =
                sample.rawAvailable &&
                (trigger.held != 0 || face.held != 0);

            auto& handState = handState_[index];
            const bool risingRaw =
                sample.rawPrimaryDown &&
                !handState.rawPrimaryPrevious;
            if (!sample.rawPrimaryDown) {
                handState.gameplayPressLatched = false;
            } else if (risingRaw && sample.hitPanel == 0) {
                handState.gameplayPressLatched = true;
            }
            handState.rawPrimaryPrevious = sample.rawPrimaryDown;
        }

        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto hand = rockHand(index);
            const auto selectedPolicyHand = policyHand(hand);
            const bool ownsPress =
                pointerSelection_.active == selectedPolicyHand &&
                pointerSelection_.submittedPrimaryDown &&
                pointerSelection_.pressBeganOnPanel;
            const bool resizing =
                activeResize_.active &&
                activeResize_.hand == selectedPolicyHand;
            const bool wantsLease =
                !handState_[index].gameplayPressLatched &&
                (samples[index].hitPanel != 0 ||
                 ownsPress ||
                 resizing);
            if (wantsLease) {
                samples[index].leaseAccepted =
                    requestInputSuppressionLocked(snapshot, hand);
            } else {
                clearInputSuppressionLocked(hand);
            }
            samples[index].submittedPrimaryDown =
                pointer_click_gate::advance(
                    handState_[index].clickGate,
                    snapshot.frameIndex,
                    samples[index].rawAvailable,
                    samples[index].rawPrimaryDown,
                    samples[index].leaseAccepted);
        }

        if (activeResize_.active) {
            const std::size_t index =
                handIndex(rockHand(activeResize_.hand));
            auto& sample = samples[index];
            auto* panel = findPanelLocked(activeResize_.panelHandle);
            const auto resizeHand = activeResize_.hand;
            pointer_panel_intersection::Hit hit{};
            const bool planeHit =
                panel &&
                sample.rayValid &&
                pointer_panel_intersection::intersect(
                    sample.ray,
                    intersectionPanel(activeResize_.initialPose),
                    hit);
            if (!panel || !planeHit) {
                clearPointerStateLocked();
                return;
            }
            if (sample.rawPrimaryDown && !sample.leaseAccepted) {
                clearPointerStateLocked();
                return;
            }

            const panel_resize::Constraints constraints{
                .aspectRatio =
                    static_cast<float>(panel->pixelWidth) /
                    static_cast<float>(panel->pixelHeight),
                .minimumWidth = panel->minimumPhysicalWidth,
                .maximumWidth = panel->maximumPhysicalWidth,
            };
            if (sample.submittedPrimaryDown) {
                const auto geometry = panel_resize::update(
                    activeResize_.drag,
                    hit.horizontal,
                    hit.vertical,
                    constraints);
                panel->pose = activeResize_.initialPose;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    panel->pose.center[axis] +=
                        panel->pose.right[axis] * geometry.centerX +
                        panel->pose.up[axis] * geometry.centerY;
                }
                panel->pose.physicalWidth = geometry.width;
                panel->pose.physicalHeight = geometry.height;
                ++panel->stateSequence;
            } else if (!sample.rawPrimaryDown) {
                activeResize_ = {};
                pointerSelection_.active = resizeHand;
                pointerSelection_.submittedPrimaryDown = false;
                pointerSelection_.pressBeganOnPanel = false;
                pointerPanelHandle_ = 0;
            }

            panel->input.hand = sdkHand(resizeHand);
            panel->input.valid = true;
            panel->input.pixelX =
                hit.u * static_cast<float>(panel->pixelWidth);
            panel->input.pixelY =
                hit.v * static_cast<float>(panel->pixelHeight);
            panel->input.active = sdkResizeHandle(
                activeResize_.active ?
                    activeResize_.drag.handle :
                    panel_resize::Handle::None);
            panel->input.hovered = panel->input.active;
            return;
        }

        const pointer_hand_selection::Candidate leftCandidate{
            .valid = samples[0].rayValid,
            .hitsPanel = samples[0].hitPanel != 0,
            .primaryDown = samples[0].submittedPrimaryDown,
        };
        const pointer_hand_selection::Candidate rightCandidate{
            .valid = samples[1].rayValid,
            .hitsPanel = samples[1].hitPanel != 0,
            .primaryDown = samples[1].submittedPrimaryDown,
        };
        const auto preferred =
            snapshot.primaryHand == RockProviderHand::Left ?
            pointer_hand_selection::Hand::Left :
            pointer_hand_selection::Hand::Right;
        const auto decision = pointer_hand_selection::choose(
            pointerSelection_,
            leftCandidate,
            rightCandidate,
            preferred);
        if (decision.hand == pointer_hand_selection::Hand::None) {
            return;
        }

        const std::size_t selectedIndex =
            decision.hand == pointer_hand_selection::Hand::Left ? 0u : 1u;
        auto& sample = samples[selectedIndex];
        const auto& selectedCandidate =
            selectedIndex == 0 ? leftCandidate : rightCandidate;

        std::uint64_t selectedPanel =
            decision.previousPrimaryDown && pointerPanelHandle_ != 0 ?
            pointerPanelHandle_ :
            sample.hitPanel;
        auto* panel = findPanelLocked(selectedPanel);
        if (!panel || !sample.rayValid) {
            return;
        }

        pointer_panel_intersection::Hit hit{};
        if (!pointer_panel_intersection::intersect(
                sample.ray,
                intersectionPanel(panel->pose),
                hit)) {
            return;
        }
        const bool controlsPanel =
            sdk::panelContains(panel->flags, hit.u, hit.v) ||
            (decision.previousPrimaryDown && decision.pressBeganOnPanel);
        const auto resizeHandle =
            hit.inside && !sdk::hasPanelFlag(panel->flags, sdk::PanelFlagV1::FixedSize) ?
            panel_resize::hitTest(
                hit.u,
                hit.v,
                static_cast<float>(panel->pixelWidth),
                static_cast<float>(panel->pixelHeight)) :
            panel_resize::Handle::None;
        const bool sameHand =
            pointerSelection_.active == decision.hand;
        const bool risingPrimary =
            selectedCandidate.primaryDown &&
            !(sameHand && pointerSelection_.submittedPrimaryDown);

        if (risingPrimary &&
            resizeHandle != panel_resize::Handle::None) {
            const panel_resize::Constraints constraints{
                .aspectRatio =
                    static_cast<float>(panel->pixelWidth) /
                    static_cast<float>(panel->pixelHeight),
                .minimumWidth = panel->minimumPhysicalWidth,
                .maximumWidth = panel->maximumPhysicalWidth,
            };
            activeResize_.active = true;
            activeResize_.panelHandle = panel->panelHandle;
            activeResize_.hand = decision.hand;
            activeResize_.drag = panel_resize::begin(
                resizeHandle,
                panel->pose.physicalWidth,
                constraints);
            activeResize_.initialPose = panel->pose;
            pointerPanelHandle_ = panel->panelHandle;
            pointer_hand_selection::commit(
                pointerSelection_,
                decision,
                selectedCandidate);
            panel->input.hand = sdkHand(decision.hand);
            panel->input.valid = true;
            panel->input.pixelX =
                hit.u * static_cast<float>(panel->pixelWidth);
            panel->input.pixelY =
                hit.v * static_cast<float>(panel->pixelHeight);
            panel->input.hovered = sdkResizeHandle(resizeHandle);
            panel->input.active = sdkResizeHandle(resizeHandle);
            return;
        }

        if (risingPrimary && hit.inside) {
            pointerPanelHandle_ = panel->panelHandle;
        }
        pointer_hand_selection::commit(
            pointerSelection_,
            decision,
            selectedCandidate);

        panel->input.hand = sdkHand(decision.hand);
        panel->input.valid = hit.inside || controlsPanel;
        panel->input.pixelX =
            hit.u * static_cast<float>(panel->pixelWidth);
        panel->input.pixelY =
            hit.v * static_cast<float>(panel->pixelHeight);
        panel->input.primaryDown =
            controlsPanel && selectedCandidate.primaryDown;
        panel->input.hovered = sdkResizeHandle(resizeHandle);

        const auto stick =
            f4cf::vrcf::VRControllers.getThumbstickValue(
                controllerHand(decision.hand));
        const auto scroll = contextual_scroll::update(
            scrollState_[selectedIndex],
            controlsPanel,
            !panel->input.primaryDown,
            { stick.x, stick.y });
        if (scroll.routesWheel) {
            panel->input.scrollX = scroll.wheelX;
            panel->input.scrollY = scroll.wheelY;
        }
        contextual_scroll::reset(
            scrollState_[selectedIndex == 0 ? 1 : 0]);

        if (!selectedCandidate.primaryDown &&
            decision.previousPrimaryDown) {
            pointerPanelHandle_ = 0;
        }
    }

    void FrameworkRuntime::handleRockFrame(
        const RockProviderFrameSnapshot& snapshot) noexcept
    {
        try {
            std::scoped_lock lock(mutex_);
            if (!inputReady(snapshot)) {
                clearPointerStateLocked();
                clearAllInputSuppressionLocked();
                return;
            }
            f4cf::vrcf::VRControllers.update(
                snapshot.primaryHand == RockProviderHand::Left);
            updatePointerLocked(snapshot);
        } catch (...) {
            static std::atomic_bool logged = false;
            if (!logged.exchange(true, std::memory_order_relaxed)) {
                log::error("RPS UI Framework pointer frame failed");
            }
        }
    }
}
