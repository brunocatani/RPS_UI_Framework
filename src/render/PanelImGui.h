#pragma once

#include "FrameworkRuntime.h"

#include <RPS_UI_Framework/Framework.h>

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

/*
 * The host's Dear ImGui: one context per registered panel, with its DX11
 * renderer and font atlas. A context lives from the panel's first drawn frame
 * until the panel is unregistered or the device changes, so ImGui state
 * (scroll, open trees, fonts the consumer added) survives closing and
 * reopening the panel.
 *
 * Thread model: every function except retire() and prepareFonts() runs on the
 * render thread with the host renderer's render mutex held, which also owns
 * this DLL's current ImGui context. retire() may run on any thread; the
 * context is destroyed on the next render.
 */
namespace rpsui::render::panel_imgui
{
    // Plugin load, before any render: reads the default text face once.
    void prepareFonts() noexcept;

    // The host's build, compared against each consumer's at registration.
    [[nodiscard]] api::framework::ImGuiBuild hostBuild() noexcept;
    [[nodiscard]] bool buildMatches(const api::framework::ImGuiBuild& consumer) noexcept;

    // Sets up the panel's context when needed (calling the consumer's setup
    // callback), feeds input, runs the consumer's draw callback between
    // NewFrame and Render and renders into target. False when the panel could
    // not be drawn; the reason is logged once per panel.
    [[nodiscard]] bool drawPanel(ID3D11Device* device, ID3D11DeviceContext* context, std::uint64_t deviceGeneration,
        std::uint64_t frameSequence, const RenderPanelSnapshot& panel, ID3D11RenderTargetView* target,
        std::uint32_t pixelWidth, std::uint32_t pixelHeight, float deltaSeconds) noexcept;

    // Any thread: the panel was unregistered.
    void retire(std::uint64_t panelHandle) noexcept;
    // Destroys retired panels' contexts.
    void collectRetired() noexcept;
    // Device change and shutdown: destroys every context.
    void releaseAll() noexcept;
}
