#pragma once
#include <cstddef>
#include <cstdint>

namespace rpsui::render::native_scene_targets
{
    // FO4VR 1.2.72 raw-disassembly witnesses:
    // Renderer-data root: 1D8C050 / 2B23EE0 (Renderer + 10).
    // Color map/slot 4: 1DBB4D0, 1D8DAB0 / 1D8DBF0.
    // Scene depth logical slot 1 or 12: 291B30F / 28D2C8E;
    // selector 1D947D0; depth map 1DBB4F0 / initialization 1DBB640.
    // Data arrays: 1DA29E0; depth stride/texture: 1D99880 / 1D99EF0 /
    // binding 1D9B190. The CommonLib depth-array offset is not the VR offset.
    inline constexpr std::uintptr_t kRendererDataRva = 0x60F3CE8;
    inline constexpr std::uintptr_t kManagerRva = 0x38AC010;
    inline constexpr std::uintptr_t kColorMapOffset = 0x13BC;
    inline constexpr std::uintptr_t kDepthMapOffset = 0x15FC;
    inline constexpr std::uintptr_t kColorArrayOffset = 0xA58;
    inline constexpr std::uintptr_t kDepthArrayOffset = 0x2588;
    inline constexpr std::uintptr_t kColorStride = 0x30, kDepthStride = 0x98;
    inline constexpr std::uint32_t kColorCapacity = 0x1B30 / kColorStride;
    inline constexpr std::uint32_t kDepthCapacity = 0xAB0 / kDepthStride;

    struct Snapshot
    {
        // Identity values only. Engine COM pointers are never dereferenced here;
        // callers compare them with resources already retained through D3D11.
        std::uintptr_t rendererData{}, depthTexture{}, colorTexture{};
        std::uint32_t logicalDepth{}, depthIndex{}, colorIndex{};
        const char* stage = "renderer-root";
    };

    inline bool plausible(std::uintptr_t value) noexcept
    {
        return value >= 0x10000 && value <= 0x00007FFFFFFEFFFF && value % alignof(void*) == 0;
    }

    template <class Reader, class Selector>
    bool readDepth(std::uintptr_t base, Reader read, Selector select, Snapshot& out) noexcept
    {
        out = {};
        if (!read(base + kRendererDataRva, &out.rendererData, sizeof(out.rendererData)) || !plausible(out.rendererData)) return false;
        out.stage = "scene-selector";
        std::uint8_t alternate{};
        // The engine calls the selector. True Scopes replaces it with a
        // thread-specific answer; reading renderer+4 would bypass that owner.
        if (!select(alternate) || alternate > 1) return false;
        out.logicalDepth = alternate ? 12u : 1u;
        out.stage = "depth-slot";
        if (!read(base + kManagerRva + kDepthMapOffset + out.logicalDepth * 4, &out.depthIndex, sizeof(out.depthIndex)) ||
            out.depthIndex >= kDepthCapacity) return false;
        out.stage = "depth-texture";
        if (!read(out.rendererData + kDepthArrayOffset + out.depthIndex * kDepthStride, &out.depthTexture, sizeof(out.depthTexture)) ||
            !plausible(out.depthTexture)) return false;
        out.stage = "depth-ready";
        return true;
    }

    template <class Reader>
    bool readColor(std::uintptr_t base, Reader read, Snapshot& out) noexcept
    {
        out.colorTexture = 0;
        out.stage = "color-slot";
        if (!read(base + kManagerRva + kColorMapOffset + 4 * 4, &out.colorIndex, sizeof(out.colorIndex)) ||
            out.colorIndex >= kColorCapacity) return false;
        out.stage = "color-texture";
        if (!plausible(out.rendererData) ||
            !read(out.rendererData + kColorArrayOffset + out.colorIndex * kColorStride, &out.colorTexture, sizeof(out.colorTexture)) ||
            !plausible(out.colorTexture)) return false;
        out.stage = "ready";
        return true;
    }
}
