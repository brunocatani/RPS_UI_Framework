#pragma once

#include "render/StereoProjection.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <optional>

namespace rpsui::render::SceneDepthCapture
{
    struct CaptureDiagnostic
    {
        StereoProjection::Snapshot projection;
        StereoProjection::ReadFailure projectionFailure;
        bool projectionValid = false;
        UINT viewportCount = 0;
        std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
        std::uintptr_t vertexShader = 0;
        std::uintptr_t colorTexture = 0;
        D3D11_TEXTURE2D_DESC colorDescription{};
    };

    struct FrameDepth
    {
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
        // Diagnostic only: this is the captured pass, not the scene's ordering.
        D3D11_COMPARISON_FUNC sourceComparison = D3D11_COMPARISON_NEVER;
        std::uint64_t frameEpoch = 0;
        std::uint64_t requestedFrameEpoch = 0;
        std::uintptr_t submittedTexture = 0;
        std::uintptr_t expectedColor = 0, expectedDepth = 0, capturedDepth = 0;
        std::uint32_t logicalDepth = 0;
        const char* failureReason = "capture-not-requested";
        D3D11_DEPTH_WRITE_MASK sourceWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        UINT sourceViewFlags = 0;
        bool sourceStencilEnabled = false;
        // Bounded sample of the selected draw, carried only with its own epoch.
        std::optional<CaptureDiagnostic> diagnostic;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return view.Get() != nullptr;
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
