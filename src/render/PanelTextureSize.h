#pragma once

// A panel's texture size. A consumer declares only its layout; the host keeps
// the texel density constant in the world: sharpness texels per layout unit at
// the panel's default size, more as the panel is shown larger, fewer smaller.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rpsui::render::panel_texture
{
    inline constexpr std::uint32_t kMinimumExtent = 16;
    inline constexpr std::uint32_t kMaximumExtent = 4096;
    // Scales move in eighths, so a resize drag reallocates the texture a few
    // times rather than on every frame.
    inline constexpr float kScaleSteps = 8.0f;

    struct Extent
    {
        std::uint32_t width{ 0 };
        std::uint32_t height{ 0 };
        bool operator==(const Extent&) const = default;
    };

    [[nodiscard]] inline std::uint32_t axis(float layout, float shown, float standard, float sharpness) noexcept
    {
        const bool proportional = std::isfinite(shown) && std::isfinite(standard) && shown > 0.0f && standard > 0.0f;
        float scale = sharpness * (proportional ? shown / standard : 1.0f);
        if (!std::isfinite(scale) || scale <= 0.0f) scale = 1.0f;
        scale = std::ceil(scale * kScaleSteps) / kScaleSteps;
        const double pixels = std::ceil(static_cast<double>(layout) * scale);
        return static_cast<std::uint32_t>(std::clamp(pixels, static_cast<double>(kMinimumExtent), static_cast<double>(kMaximumExtent)));
    }

    // Each axis on its own: a panel shown off its layout's shape stretches.
    [[nodiscard]] inline Extent size(float layoutWidth, float layoutHeight, float physicalWidth, float physicalHeight,
        float defaultPhysicalWidth, float sharpness) noexcept
    {
        const float defaultPhysicalHeight = defaultPhysicalWidth * layoutHeight / layoutWidth;
        return { axis(layoutWidth, physicalWidth, defaultPhysicalWidth, sharpness),
            axis(layoutHeight, physicalHeight, defaultPhysicalHeight, sharpness) };
    }

    // The full mip chain, so a small or distant panel is filtered rather than aliased.
    [[nodiscard]] inline std::uint32_t mipLevels(Extent extent) noexcept
    {
        std::uint32_t levels = 1;
        for (auto largest = std::max(extent.width, extent.height); largest > 1; largest >>= 1) ++levels;
        return levels;
    }
}
