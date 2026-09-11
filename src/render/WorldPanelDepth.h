#pragma once

#include <d3d11.h>

namespace rpsui::render::world_panel_depth
{
    // FO4VR scene-camera projections use forward Z. Native depth clears in
    // 1D9B190 are 1.0 / 0.999; the writing state in 1DBDFC0 uses LESS_EQUAL.
    // A later read-only/stencil pass can use GREATER_EQUAL on that same texture.
    // Its comparison belongs to that pass, never to world-panel composition.
    inline constexpr auto kComparison = D3D11_COMPARISON_LESS_EQUAL;
    inline constexpr float kMinDepth = 0.0f, kMaxDepth = 1.0f;

    [[nodiscard]] constexpr D3D11_DEPTH_STENCIL_DESC readOnlyDescription() noexcept
    {
        D3D11_DEPTH_STENCIL_DESC description{};
        description.DepthEnable = TRUE;
        description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        description.DepthFunc = kComparison;
        description.StencilEnable = FALSE;
        return description;
    }
}
