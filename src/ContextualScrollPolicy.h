#pragma once

#include <algorithm>
#include <cmath>

namespace rpsui::contextual_scroll
{
    inline constexpr float kDeadZone = 0.30f;
    inline constexpr float kReleaseZone = 0.24f;

    struct Stick
    {
        float x{ 0.0f };
        float y{ 0.0f };
    };

    struct State
    {
        bool capturedUntilNeutral{ false };
    };

    struct Result
    {
        float wheelX{ 0.0f };
        float wheelY{ 0.0f };
        bool routesWheel{ false };
        bool blocksLegacyNavigation{ false };
    };

    [[nodiscard]] inline float remapAxis(float value) noexcept
    {
        if (!std::isfinite(value)) {
            return 0.0f;
        }
        const float magnitude = std::clamp(std::fabs(value), 0.0f, 1.0f);
        if (magnitude <= kDeadZone) {
            return 0.0f;
        }
        const float normalized = (magnitude - kDeadZone) / (1.0f - kDeadZone);
        return std::copysign(normalized, value);
    }

    [[nodiscard]] inline bool isNeutral(Stick stick) noexcept
    {
        if (!std::isfinite(stick.x) || !std::isfinite(stick.y)) {
            return true;
        }
        return std::fabs(stick.x) <= kReleaseZone &&
               std::fabs(stick.y) <= kReleaseZone;
    }

    [[nodiscard]] inline Result update(
        State& state,
        bool pointerOwnsPanel,
        bool wheelAllowed,
        Stick stick) noexcept
    {
        const bool neutral = isNeutral(stick);
        if (state.capturedUntilNeutral && neutral) {
            state.capturedUntilNeutral = false;
        }
        if (pointerOwnsPanel && !neutral) {
            state.capturedUntilNeutral = true;
        }

        Result result{};
        result.blocksLegacyNavigation = pointerOwnsPanel || state.capturedUntilNeutral;
        result.routesWheel = pointerOwnsPanel && wheelAllowed;
        if (result.routesWheel) {
            // ImGui subtracts wheel deltas from scroll position. Negating X makes
            // right-stick motion move the viewport toward content on the right.
            result.wheelX = -remapAxis(stick.x);
            result.wheelY = remapAxis(stick.y);
        }
        return result;
    }

    inline void reset(State& state) noexcept
    {
        state = {};
    }
}
