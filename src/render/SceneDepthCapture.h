#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace rpsui::render::SceneDepthCapture
{
    struct FrameDepth
    {
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
        D3D11_COMPARISON_FUNC comparison =
            D3D11_COMPARISON_LESS_EQUAL;
        std::uint64_t frameEpoch = 0;
        std::uint64_t requestedFrameEpoch = 0;
        std::uintptr_t submittedTexture = 0;
        std::uintptr_t expectedColor = 0, expectedDepth = 0, capturedDepth = 0;
        std::uint32_t logicalDepth = 0;
        const char* failureReason = "capture-not-requested";

        [[nodiscard]] bool IsValid() const noexcept
        {
            return view &&
                   comparison >= D3D11_COMPARISON_NEVER &&
                   comparison <= D3D11_COMPARISON_ALWAYS;
        }
    };

    [[nodiscard]] bool Install() noexcept;
    [[nodiscard]] bool IsInstalled() noexcept;
    void Uninstall() noexcept;
    void SetCaptureRequested(bool requested) noexcept;
    [[nodiscard]] FrameDepth AcquireForSubmittedTarget(
        ID3D11Texture2D* colorTexture,
        const D3D11_TEXTURE2D_DESC& colorDescription) noexcept;
    // Called only by the compositor's existing five-second warning gate.
    void ReportUnavailable(const FrameDepth& depth,
        const D3D11_TEXTURE2D_DESC& submittedDescription) noexcept;
    void AdvanceSubmittedFrame() noexcept;
    void Reset() noexcept;
}
