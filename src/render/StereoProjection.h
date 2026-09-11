#pragma once

#include <DirectXMath.h>

#include <cstdint>

namespace rpsui::render::StereoProjection
{
    enum class CaptureStage : std::uint8_t
    {
        None,
        RelocationResolved,
        RuntimeRootRead,
        RootStereoStateRead,
        StereoRecordsRead,
        Validated
    };

    struct Snapshot
    {
        DirectX::XMFLOAT4X4 composite[2]{};
        DirectX::XMFLOAT4 origin[2]{};
        // Diagnostic identities only; these addresses are never dereferenced.
        std::uintptr_t sourceRoot = 0;
        std::uintptr_t sourceRecords = 0;
    };

    struct ReadFailure
    {
        CaptureStage stage = CaptureStage::None;
        std::uint32_t win32Error = 0;
    };

    // Read without changing the compositor's failure/recovery log state.
    [[nodiscard]] bool ReadSnapshot(Snapshot& outSnapshot, ReadFailure& failure) noexcept;

    [[nodiscard]] bool CaptureSnapshot(
        Snapshot& outSnapshot) noexcept;
}
