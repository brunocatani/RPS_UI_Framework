#pragma once

#include <cstdint>
#include <d3d11.h>

namespace rpsui::world_draw
{
    struct RenderBatch;
}

namespace rpsui::render::StereoProjection
{
    struct Snapshot;
}

// Draws world-draw publications into the submitted stereo texture (both eyes
// side by side). Ported from ROCK's world overlay renderer so indicators and
// diagnostics look as they did there: instanced per-eye projection, depth test
// off, straight alpha blending. Render thread only; the caller owns the D3D
// state boundary.
namespace rpsui::render::WorldDrawRenderer
{
    void Draw(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        const D3D11_TEXTURE2D_DESC& targetDescription, const StereoProjection::Snapshot& projection,
        const world_draw::RenderBatch& batch, std::uint64_t frame) noexcept;
    void ReleaseDeviceResources() noexcept;
}
