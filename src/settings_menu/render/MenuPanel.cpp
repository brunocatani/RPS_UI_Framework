#include "../PCH.h"

#include "ConfiguratorRuntime.h"
#include "ExternalPages.h"
#include "render/MenuPanel.h"
#include "render/NativeRenderer.h"
#include "render/UiVisualStyle.h"

#include <RPS_UI_Framework/Framework.h>

#include <imgui_internal.h>
#include <RPS_UI_Framework/ImGuiBuild.h>

#include "MenuTheme.h"

namespace devui::render
{
    namespace
    {
        // One per panel. RPS UI owns each panel's ImGui context, so pointer
        // state and fonts never mix; the fonts added in setupPanel live with
        // that context generation. Render thread only, except Shutdown.
        struct RenderState
        {
            std::mutex mutex;
            ImGuiContext* imguiContext{ nullptr }; // Identity only, for GetFont.
            std::array<ImFont*,
                static_cast<std::size_t>(FontRole::Count)> fonts{};
            std::uint64_t contextGeneration{ 0 };
            bool backRequested{ false };
            bool keyboard{ false };
        };


        [[nodiscard]] RenderState& configState() noexcept
        {
            static RenderState state;
            return state;
        }

        [[nodiscard]] RenderState& keyboardState() noexcept
        {
            static RenderState state{ .keyboard = true };
            return state;
        }


        // The menu's roles on the theme's faces; each role keeps its own size.
        void configureFonts(RenderState& state, const rpsui::api::framework::PanelSetup& setup)
        {
            using Face = rpsui::api::framework::FontRole;
            const auto face = [&](Face role) { return static_cast<ImFont*>(setup.fonts[static_cast<std::size_t>(role)]); };
            state.fonts[static_cast<std::size_t>(FontRole::Body)] = face(Face::Interface);
            state.fonts[static_cast<std::size_t>(FontRole::Medium)] = face(Face::Interface);
            state.fonts[static_cast<std::size_t>(FontRole::Heading)] = face(Face::Strong);
            state.fonts[static_cast<std::size_t>(FontRole::Display)] = face(Face::Strong);
            state.fonts[static_cast<std::size_t>(FontRole::Mono)] = face(Face::Mono);
            ImGui::GetIO().FontDefault = state.fonts[static_cast<std::size_t>(FontRole::Body)];
            ImGui::GetStyle().FontSizeBase = FontSize(FontRole::Body);
            ImGui::GetStyle().FontScaleMain = 1.0f;
        }

        void RPSUI_CALL setupPanel(
            const rpsui::api::framework::PanelSetup* setup,
            void* userData) noexcept
        {
            if (!setup || setup->size < sizeof(*setup) || !userData) {
                return;
            }
            auto& state = *static_cast<RenderState*>(userData);
            std::scoped_lock lock(state.mutex);
            try {
                RPSUI_IMGUI_BIND(setup->imgui);
                rpsui::settings_menu::theme::Scope theme(setup->theme);
                auto& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                configureFonts(state, *setup);
                devui::visual::applyStyle();
                if (state.keyboard) {
                    // The keyboard panel has more layout units per metre; keep its cursor the Config panel's size.
                    ImGui::GetStyle().MouseCursorScale =
                        static_cast<float>(kKeyboardPanelLayoutWidth) / static_cast<float>(kPanelLayoutWidth);
                }
                state.imguiContext = ImGui::GetCurrentContext();
                state.contextGeneration = setup->contextGeneration;
                logger::info(
                    "Settings menu set up its RPS UI panel context (generation {})",
                    setup->contextGeneration);
            } catch (const std::exception& error) {
                logger::critical("Settings menu panel setup failed: {}", error.what());
            } catch (...) {
                logger::critical("Settings menu panel setup failed");
            }
        }

        [[nodiscard]] rpsui::settings_menu::panel_resize::Handle localResizeHandle(
            rpsui::api::framework::ResizeHandle handle) noexcept
        {
            return static_cast<rpsui::settings_menu::panel_resize::Handle>(
                static_cast<std::uint32_t>(handle));
        }

        void RPSUI_CALL drawPanel(
            const rpsui::api::framework::PanelDrawFrame* frame,
            void* userData) noexcept
        {
            if (!frame ||
                frame->size < sizeof(*frame) ||
                !userData) {
                return;
            }
            auto& state = *static_cast<RenderState*>(userData);
            std::scoped_lock lock(state.mutex);
            if (state.contextGeneration != frame->contextGeneration) {
                return;
            }
            RPSUI_IMGUI_BIND(frame->imgui);
            rpsui::settings_menu::theme::Scope theme(frame->theme);

            if (!state.keyboard) {
                rpsui::settings_menu::onFrameworkPanelFrame(
                    frame->physicalWidth,
                    localResizeHandle(frame->hoveredResizeHandle),
                    localResizeHandle(frame->activeResizeHandle));
            }

            state.backRequested |= frame->backPressed != 0;
            // Pages other mods add draw in this frame with the panel's context and faces.
            rpsui::settings_menu::external_pages::FrameContext pageContext{};
            pageContext.contextGeneration = frame->contextGeneration;
            pageContext.d3dDevice = frame->d3dDevice;
            pageContext.imgui = frame->imgui;
            pageContext.theme = frame->theme;
            for (std::size_t role = 0; role < rpsui::api::framework::kFontRoleCount; ++role) pageContext.fonts[role] = frame->fonts[role];
            pageContext.deltaSeconds = frame->deltaSeconds;
            if (!state.keyboard) rpsui::settings_menu::external_pages::setFrameContext(&pageContext);
            struct ClearPageContext { bool keyboard; ~ClearPageContext() { if (!keyboard) rpsui::settings_menu::external_pages::setFrameContext(nullptr); } } clearPageContext{ state.keyboard };
            const bool drawn = state.keyboard ?
                rpsui::settings_menu::drawKeyboardPanel(0, 0, kKeyboardPanelLayoutWidth, kKeyboardPanelLayoutHeight, state.backRequested) :
                rpsui::settings_menu::drawImGui(0, 0, kPanelLayoutWidth, kPanelLayoutHeight, state.backRequested);
            if (drawn) {
                state.backRequested = false;
            }
        }
    }

    float FontSize(FontRole role) noexcept
    {
        return kFontSizes[static_cast<std::size_t>(role)] * rpsui::settings_menu::theme::textScale();
    }
    ImFont* GetFont(FontRole role) noexcept
    {
        const auto index = static_cast<std::size_t>(role);
        auto& keyboard = keyboardState();
        auto& state = ImGui::GetCurrentContext() == keyboard.imguiContext ? keyboard : configState();
        return index < state.fonts.size() ?
            state.fonts[index] :
            nullptr;
    }

    void resetPanelStates() noexcept
    {
        // RPS UI retired both panels' contexts with their registrations.
        for (auto* state : { &configState(), &keyboardState() }) {
            std::scoped_lock lock(state->mutex);
            state->imguiContext = nullptr;
            state->fonts = {};
            state->contextGeneration = 0;
        }
    }

    PanelCallbacks panelCallbacks(bool keyboard) noexcept
    {
        return { &setupPanel, &drawPanel, keyboard ? &keyboardState() : &configState() };
    }
}
