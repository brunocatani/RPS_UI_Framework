#pragma once

// A panel carried by the hand's pointer. The point the pointer grabbed stays at
// the end of the ray, at the grab distance (pushed or pulled with the stick),
// and the panel turns with the hand's heading about the world's vertical axis
// (+Z), so it stays upright and keeps facing the player as it is carried.

#include "PointerPanelIntersection.h"

#include <RPS_UI_Framework/Framework.h>

#include <algorithm>
#include <cmath>

namespace rpsui::panel_move
{
    using pointer_panel_intersection::Vector;

    // Game units.
    inline constexpr float kMinimumDistance = 20.0f;
    inline constexpr float kMaximumDistance = 450.0f;
    // Game units per second at full stick.
    inline constexpr float kStickSpeed = 120.0f;
    // Below this, a stick deflection does not push or pull.
    inline constexpr float kStickDeadzone = 0.35f;

    struct Grab
    {
        api::framework::PanelPose initialPose{};
        // From the grab point to the panel's centre, at grab time.
        Vector offset{};
        float initialHeading{ 0.0f };
        float distance{ 0.0f };
    };

    // Heading of a direction about +Z; the previous one when the ray is near vertical.
    [[nodiscard]] inline float heading(const Vector& direction, float previous) noexcept
    {
        const float horizontal = std::hypot(direction.x, direction.y);
        return horizontal > 0.05f ? std::atan2(direction.y, direction.x) : previous;
    }

    [[nodiscard]] inline Grab begin(const api::framework::PanelPose& pose, const Vector& origin, const Vector& direction,
        float distance) noexcept
    {
        // The panel stays where it was grabbed; only the stick moves it into range.
        Grab grab{ .initialPose = pose };
        grab.distance = distance;
        grab.offset = {
            pose.center[0] - (origin.x + direction.x * distance),
            pose.center[1] - (origin.y + direction.y * distance),
            pose.center[2] - (origin.z + direction.z * distance),
        };
        grab.initialHeading = heading(direction, 0.0f);
        return grab;
    }

    // Stick Y pushes the panel away (up) or pulls it closer (down).
    inline void push(Grab& grab, float stickY, float deltaSeconds) noexcept
    {
        if (!std::isfinite(stickY) || std::abs(stickY) < kStickDeadzone || !(deltaSeconds > 0.0f)) return;
        const float amount = (std::abs(stickY) - kStickDeadzone) / (1.0f - kStickDeadzone);
        const float next = grab.distance + std::copysign(amount, stickY) * kStickSpeed * std::min(deltaSeconds, 0.1f);
        // Toward the range only: a panel grabbed outside it is never snapped into it.
        if (stickY > 0.0f) grab.distance = std::max(grab.distance, std::min(next, kMaximumDistance));
        else grab.distance = std::min(grab.distance, std::max(next, kMinimumDistance));
    }

    [[nodiscard]] inline api::framework::PanelPose follow(const Grab& grab, const Vector& origin, const Vector& direction) noexcept
    {
        const float turn = heading(direction, grab.initialHeading) - grab.initialHeading;
        const float c = std::cos(turn), s = std::sin(turn);
        const auto rotate = [&](const float in[3], float out[3]) {
            const float x = in[0], y = in[1];
            out[0] = x * c - y * s;
            out[1] = x * s + y * c;
            out[2] = in[2];
        };
        auto pose = grab.initialPose;
        rotate(grab.initialPose.right, pose.right);
        rotate(grab.initialPose.up, pose.up);
        rotate(grab.initialPose.front, pose.front);
        const float offset[3]{ grab.offset.x, grab.offset.y, grab.offset.z };
        float turned[3]{};
        rotate(offset, turned);
        pose.center[0] = origin.x + direction.x * grab.distance + turned[0];
        pose.center[1] = origin.y + direction.y * grab.distance + turned[1];
        pose.center[2] = origin.z + direction.z * grab.distance + turned[2];
        return pose;
    }
}
