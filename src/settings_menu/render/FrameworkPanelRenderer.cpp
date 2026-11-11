#include "../PCH.h"

#include "ConfiguratorRuntime.h"
#include "ExternalPages.h"
#include "render/FrameworkPanelRenderer.h"
#include "render/MenuPanel.h"
#include "render/NativeRenderer.h"
#include "render/UiVisualStyle.h"

#include "RpsUiQuery.h"
#include <RPS_UI_Framework/Framework.h>

#include <imgui_internal.h>
#include <RPS_UI_Framework/ImGuiBuild.h>

#include "MenuTheme.h"

namespace devui::render
{
    namespace
    {
        struct FrameworkState
        {
            std::mutex mutex;
            const rpsui::api::framework::Api* api{ nullptr };
            std::uint64_t ownerToken{ 0 };
            std::uint64_t panelHandle{ 0 };
            std::uint64_t presentationSequence{ 1 };
            std::uint64_t keyboardHandle{ 0 };
            std::uint64_t keyboardSequence{ 1 };
            std::uint64_t keyboardFollows{ 0 };  // Config panel state sequence the keyboard pose was built from.
            bool keyboardOpen{ false };
            bool keyboardRejectLogged{ false };
            bool installed{ false };
        };

        [[nodiscard]] FrameworkState& frameworkState() noexcept
        {
            static FrameworkState state;
            return state;
        }
    }

    bool InstallFrameworkPanel() noexcept
    {
        auto& state = frameworkState();
        std::scoped_lock lock(state.mutex);
        if (state.installed) {
            return true;
        }

        if (rpsui::api::acquire(rpsui::settings_menu::rpsUiQuery(), state.api) != rpsui::api::Result::Ok) {
            logger::critical(
                "RPS UI Framework 2.0 is unavailable; Settings menu consumer disabled");
            return false;
        }

        rpsui::api::framework::ConsumerRegistration consumer{};
        std::snprintf(
            consumer.consumerId,
            sizeof(consumer.consumerId),
            "rpsui.settings");
        std::snprintf(
            consumer.displayName,
            sizeof(consumer.displayName),
            "Settings menu");
        rpsui::api::framework::ConsumerHandle handle{};
        if (state.api->registerConsumer(
                &consumer,
                &handle) != rpsui::api::Result::Ok ||
            handle.ownerToken == 0) {
            logger::critical(
                "Settings menu could not register with RPS UI Framework");
            state.api = nullptr;
            return false;
        }
        state.ownerToken = handle.ownerToken;

        rpsui::api::framework::PanelRegistration panel{};
        std::snprintf(
            panel.panelId,
            sizeof(panel.panelId),
            "rpsui.settings.main");
        std::snprintf(
            panel.displayName,
            sizeof(panel.displayName),
            "Settings menu");
        panel.layoutWidth = static_cast<float>(kPanelLayoutWidth);
        panel.layoutHeight = static_cast<float>(kPanelLayoutHeight);
        panel.defaultPhysicalWidth = kDefaultPanelPhysicalWidth;
        panel.minimumPhysicalWidth = kMinimumPanelPhysicalWidth;
        panel.maximumPhysicalWidth = kMaximumPanelPhysicalWidth;
        panel.sortOrder = 310;
        // Grip held on Config carries it; the keyboard below follows it.
        panel.flags = static_cast<std::uint32_t>(rpsui::api::framework::PanelFlag::ConfigNavigation) |
                      static_cast<std::uint32_t>(rpsui::api::framework::PanelFlag::Movable);
        panel.imgui = RPSUI_IMGUI_BUILD();
        panel.setupCallback = panelCallbacks(false).setup;
        panel.drawCallback = panelCallbacks(false).draw;
        panel.userData = panelCallbacks(false).userData;
        if (state.api->registerPanel(
                state.ownerToken,
                &panel,
                &state.panelHandle) != rpsui::api::Result::Ok ||
            state.panelHandle == 0) {
            if (state.api->unregisterConsumer) {
                (void)state.api->unregisterConsumer(state.ownerToken);
            }
            state.api = nullptr;
            state.ownerToken = 0;
            state.panelHandle = 0;
            state.presentationSequence = 1;
            state.installed = false;
            logger::critical(
                "Settings menu could not register its panel with RPS UI Framework");
            return false;
        }

        // Fixed size: it takes the Config panel's width and follows its resizes instead.
        rpsui::api::framework::PanelRegistration keyboard{};
        std::snprintf(keyboard.panelId, sizeof(keyboard.panelId), "rpsui.settings.keyboard");
        std::snprintf(keyboard.displayName, sizeof(keyboard.displayName), "Keyboard");
        keyboard.layoutWidth = static_cast<float>(kKeyboardPanelLayoutWidth);
        keyboard.layoutHeight = static_cast<float>(kKeyboardPanelLayoutHeight);
        keyboard.defaultPhysicalWidth = kDefaultPanelPhysicalWidth;
        keyboard.minimumPhysicalWidth = kMinimumPanelPhysicalWidth;
        keyboard.maximumPhysicalWidth = kMaximumPanelPhysicalWidth;
        keyboard.sortOrder = 311;
        keyboard.flags = static_cast<std::uint32_t>(rpsui::api::framework::PanelFlag::ConfigNavigation) |
                         static_cast<std::uint32_t>(rpsui::api::framework::PanelFlag::FixedSize);
        keyboard.imgui = RPSUI_IMGUI_BUILD();
        keyboard.setupCallback = panelCallbacks(true).setup;
        keyboard.drawCallback = panelCallbacks(true).draw;
        keyboard.userData = panelCallbacks(true).userData;
        if (state.api->registerPanel(state.ownerToken, &keyboard, &state.keyboardHandle) != rpsui::api::Result::Ok) {
            state.keyboardHandle = 0;
            logger::critical("Settings menu could not register its keyboard panel; the console and searches take the physical keyboard only");
        }

        state.installed = true;
        logger::info(
            "Settings menu registered RPS UI panel {} and keyboard panel {} as owner {}",
            state.panelHandle,
            state.keyboardHandle,
            state.ownerToken);
        return true;
    }

    namespace
    {
        void closeKeyboardLocked(FrameworkState& state) noexcept
        {
            if (!state.keyboardOpen) {
                return;
            }
            state.keyboardOpen = false;
            rpsui::api::framework::PanelPresentation presentation{};
            presentation.sequence = state.keyboardSequence++;
            const auto result = state.api->submitPanelPresentation(state.ownerToken, state.keyboardHandle, &presentation);
            if (result != rpsui::api::Result::Ok) {
                logger::error("Settings keyboard panel close was rejected ({})", static_cast<std::uint32_t>(result));
            }
        }
    }

    void SyncKeyboardPanel(bool wanted) noexcept
    {
        auto& state = frameworkState();
        std::scoped_lock lock(state.mutex);
        if (!state.installed || !state.api || !state.keyboardHandle || !state.api->getPanelState) {
            return;
        }
        rpsui::api::framework::PanelState config;
        if (!wanted || state.api->getPanelState(state.ownerToken, state.panelHandle, &config) != rpsui::api::Result::Ok || !config.open) {
            closeKeyboardLocked(state);
            return;
        }
        if (state.keyboardOpen && state.keyboardFollows == config.sequence) {
            return;
        }

        // The Config panel's own axes, one gap below its bottom edge.
        rpsui::api::framework::PanelPresentation presentation{};
        presentation.sequence = state.keyboardSequence++;
        presentation.open = 1;
        auto& pose = presentation.pose;
        pose = config.pose;
        pose.physicalHeight = pose.physicalWidth * static_cast<float>(kKeyboardPanelLayoutHeight) / static_cast<float>(kKeyboardPanelLayoutWidth);
        const float drop = config.pose.physicalHeight * 0.5f + config.pose.physicalWidth * 0.02f + pose.physicalHeight * 0.5f;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            pose.center[axis] = config.pose.center[axis] - config.pose.up[axis] * drop;
        }
        const auto result = state.api->submitPanelPresentation(state.ownerToken, state.keyboardHandle, &presentation);
        if (result != rpsui::api::Result::Ok) {
            if (!std::exchange(state.keyboardRejectLogged, true)) {
                logger::error("Settings keyboard panel was rejected ({}); the console and searches take the physical keyboard only",
                    static_cast<std::uint32_t>(result));
            }
            return;
        }
        state.keyboardOpen = true;
        state.keyboardFollows = config.sequence;
    }

    bool ResetFrameworkPanelLayout() noexcept
    {
        auto& state = frameworkState();
        std::scoped_lock lock(state.mutex);
        return state.installed &&
               state.api &&
               state.api->resetPanelLayout &&
               state.api->resetPanelLayout(
                   state.ownerToken,
                   state.panelHandle) ==
                   rpsui::api::Result::Ok;
    }

    bool SetPanelOpen(bool open, const PanelPose* pose) noexcept
    {
        auto& state = frameworkState();
        std::scoped_lock lock(state.mutex);
        if (!state.installed ||
            !state.api ||
            !state.api->submitPanelPresentation) {
            return false;
        }
        if (!open) {
            closeKeyboardLocked(state);
        }

        rpsui::api::framework::PanelPresentation presentation{};
        presentation.sequence = state.presentationSequence++;
        presentation.open = open ? 1 : 0;
        if (open) {
            if (!pose) {
                return false;
            }
            const auto copy = [](const DirectX::XMFLOAT3& value, float out[3]) {
                out[0] = value.x;
                out[1] = value.y;
                out[2] = value.z;
            };
            copy(pose->center, presentation.pose.center);
            copy(pose->right, presentation.pose.right);
            copy(pose->up, presentation.pose.up);
            copy(pose->front, presentation.pose.front);
            presentation.pose.physicalWidth = pose->physicalWidth;
            presentation.pose.physicalHeight = pose->physicalHeight;
        }

        const auto result = state.api->submitPanelPresentation(
            state.ownerToken,
            state.panelHandle,
            &presentation);
        if (result != rpsui::api::Result::Ok) {
            logger::error(
                "Settings menu panel presentation was rejected ({})",
                static_cast<std::uint32_t>(result));
            return false;
        }
        if (open && state.api->getPanelState) {
            rpsui::api::framework::PanelState accepted;
            const auto query = state.api->getPanelState(state.ownerToken, state.panelHandle, &accepted);
            if (query == rpsui::api::Result::Ok) {
                logger::info("Settings menu accepted pose: requested=({:.2f},{:.2f},{:.2f}) actual=({:.2f},{:.2f},{:.2f}) shift=({:.2f},{:.2f},{:.2f}) open={}",
                    presentation.pose.center[0], presentation.pose.center[1], presentation.pose.center[2],
                    accepted.pose.center[0], accepted.pose.center[1], accepted.pose.center[2],
                    accepted.pose.center[0]-presentation.pose.center[0],
                    accepted.pose.center[1]-presentation.pose.center[1],
                    accepted.pose.center[2]-presentation.pose.center[2], accepted.open);
            } else {
                logger::warn("Settings menu accepted-pose query failed ({})", static_cast<std::uint32_t>(query));
            }
        }
        return true;
    }

    void Shutdown() noexcept
    {
        auto& framework = frameworkState();
        {
            std::scoped_lock lock(framework.mutex);
            if (framework.api) {
                if (framework.keyboardHandle != 0 && framework.api->unregisterPanel) {
                    (void)framework.api->unregisterPanel(framework.ownerToken, framework.keyboardHandle);
                }
                if (framework.panelHandle != 0 &&
                    framework.api->unregisterPanel) {
                    (void)framework.api->unregisterPanel(
                        framework.ownerToken,
                        framework.panelHandle);
                }
                if (framework.ownerToken != 0 &&
                    framework.api->unregisterConsumer) {
                    (void)framework.api->unregisterConsumer(
                        framework.ownerToken);
                }
            }
            framework.api = nullptr;
            framework.ownerToken = 0;
            framework.panelHandle = 0;
            framework.presentationSequence = 1;
            framework.keyboardHandle = 0;
            framework.keyboardSequence = 1;
            framework.keyboardFollows = 0;
            framework.keyboardOpen = false;
            framework.keyboardRejectLogged = false;
            framework.installed = false;
        }

        // RPS UI retired both panels' contexts with their registrations.
        resetPanelStates();
    }
}
