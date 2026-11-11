#include "PCH.h"

#include "SettingsMenuHost.h"

#include "Logger.h"
#include "SettingsHolotape.h"

#include "settings_menu/ConfiguratorRuntime.h"
#include "settings_menu/ConsoleService.h"
#include "settings_menu/RpsUiQuery.h"
#include "settings_menu/render/FrameworkPanelRenderer.h"
#include "settings_menu/render/NativeRenderer.h"

#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/Input.h>

namespace rpsui::settings_menu_host
{
    namespace
    {
        // Distance and drop below eye height of a menu opened in front of the headset.
        constexpr float kOpenDistanceGameUnits = 58.0f;
        constexpr float kOpenDropGameUnits = 10.0f;

        struct State
        {
            const api::input::Api* input{ nullptr };
            std::uint64_t inputToken{ 0 };
            std::atomic_bool installed{ false };
            std::atomic_bool sessionReady{ false };
        };

        State& state() noexcept
        {
            static State value;
            return value;
        }

        // Input callback (game thread): the menu is usable only while gameplay input is
        // ready; the physical keyboard is asked for only while the menu's keyboard is up,
        // so its capture lapses by itself otherwise.
        void RPSUI_CALL onInput(const api::input::Frame* frame, void*) noexcept
        {
            auto& s = state();
            const bool usable = frame && frame->ready && s.sessionReady.load(std::memory_order_acquire);
            settings_menu::setAvailable(usable);
            if (!usable) {
                return;
            }
            // The holotape was played: the menu opens once gameplay input is back.
            if (settings_holotape::takePlayed()) {
                (void)open(nullptr);
            }
            api::input::KeyboardRead keyboard;
            std::array<settings_menu::ConsoleKeyEvent, api::input::kKeyboardEventCapacity> keys{};
            const bool held = settings_menu::keyboardInputActive() && s.input->captureKeyboard &&
                              s.input->captureKeyboard(s.inputToken, &keyboard);
            const std::uint32_t count = held ? (std::min)(keyboard.count, api::input::kKeyboardEventCapacity) : 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                keys[i] = { keyboard.events[i].kind == api::input::KeyboardEventKind::Character, keyboard.events[i].code };
            }
            settings_menu::serviceConsole(held, held && keyboard.focused != 0,
                std::span<const settings_menu::ConsoleKeyEvent>(keys.data(), count));
        }

        // The headset's position and its forward direction flattened onto the floor.
        std::optional<devui::render::PanelPose> poseInFrontOfHeadset() noexcept
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto* hmd = player ? player->vrNodes.hmdNode : nullptr;
            if (!hmd) {
                return std::nullopt;
            }
            const auto& world = hmd->world;
            // A node's forward is its local +Y in world space.
            DirectX::XMFLOAT3 forward{ world.rotate.entry[1].x, world.rotate.entry[1].y, 0.0f };
            const float length = std::hypot(forward.x, forward.y);
            if (!std::isfinite(length) || length < 0.05f || !std::isfinite(world.translate.x) ||
                !std::isfinite(world.translate.y) || !std::isfinite(world.translate.z)) {
                return std::nullopt;
            }
            forward.x /= length;
            forward.y /= length;
            devui::render::PanelPose pose;
            pose.center = { world.translate.x + forward.x * kOpenDistanceGameUnits,
                world.translate.y + forward.y * kOpenDistanceGameUnits, world.translate.z - kOpenDropGameUnits };
            pose.up = { 0.0f, 0.0f, 1.0f };
            pose.front = { -forward.x, -forward.y, 0.0f };
            pose.right = { forward.y, -forward.x, 0.0f }; // forward x up
            return pose;
        }
    }

    void onPluginLoad() noexcept
    {
        (void)settings_menu::console::installCapture();
    }

    void onHostReady() noexcept
    {
        auto& s = state();
        if (s.installed.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (!devui::render::InstallFrameworkPanel()) {
            log::error("Settings menu unavailable: its panels could not be registered");
            return;
        }
        if (api::acquire(settings_menu::rpsUiQuery(), s.input) != api::Result::Ok || !s.input || !s.input->subscribe) {
            log::error("Settings menu unavailable: RPS UI input could not be bound");
            return;
        }
        s.inputToken = s.input->subscribe(&onInput, nullptr);
        if (!s.inputToken) {
            log::error("Settings menu unavailable: input subscription refused");
            return;
        }
        settings_menu::onGameDataReady();
        settings_holotape::onGameDataReady();
        log::info("Settings menu ready");
    }

    void onSessionReady(bool ready) noexcept
    {
        auto& s = state();
        s.sessionReady.store(ready, std::memory_order_release);
        if (!ready) {
            settings_menu::setAvailable(false);
            return;
        }
        settings_menu::onGameSessionReady();
        settings_holotape::onSessionReady();
    }

    bool open(const Pose* request) noexcept
    {
        if (settings_menu::isOpen() || settings_menu::isOpening()) {
            return false;
        }
        std::optional<devui::render::PanelPose> pose;
        if (request) {
            devui::render::PanelPose value;
            value.center = { request->center[0], request->center[1], request->center[2] };
            value.right = { request->right[0], request->right[1], request->right[2] };
            value.up = { request->up[0], request->up[1], request->up[2] };
            value.front = { request->front[0], request->front[1], request->front[2] };
            pose = value;
        } else {
            pose = poseInFrontOfHeadset();
        }
        if (!pose) {
            log::warn("Settings menu not opened: no headset pose");
            return false;
        }
        settings_menu::openAt(*pose);
        return true;
    }

    void close() noexcept
    {
        settings_menu::close();
    }

    bool isOpen() noexcept
    {
        return settings_menu::isOpen() || settings_menu::isOpening();
    }

    std::uint64_t closedCount() noexcept
    {
        return settings_menu::closedCount();
    }
}
