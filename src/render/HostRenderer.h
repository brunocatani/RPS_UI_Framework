#pragma once

#include <d3d11.h>

namespace rpsui::render
{
    void RenderSubmittedTexture(ID3D11Texture2D* texture) noexcept;
    void Shutdown() noexcept;
}
