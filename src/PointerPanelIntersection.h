#pragma once

#include <cmath>

namespace rpsui::pointer_panel_intersection
{
    struct Vector
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float z{ 0.0f };
    };

    struct Ray
    {
        Vector origin{};
        Vector direction{};
        float maxDistance{ 0.0f };
    };

    struct Panel
    {
        Vector center{};
        Vector right{};
        Vector up{};
        Vector front{};
        float width{ 0.0f };
        float height{ 0.0f };
    };

    struct Hit
    {
        float distance{ 0.0f };
        float horizontal{ 0.0f };
        float vertical{ 0.0f };
        float u{ 0.0f };
        float v{ 0.0f };
        bool inside{ false };
    };

    [[nodiscard]] inline bool finite(Vector value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] inline float dot(Vector left, Vector right) noexcept
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    [[nodiscard]] inline Vector subtract(Vector left, Vector right) noexcept
    {
        return { left.x - right.x, left.y - right.y, left.z - right.z };
    }

    [[nodiscard]] inline Vector addScaled(Vector origin, Vector direction, float scale) noexcept
    {
        return {
            origin.x + direction.x * scale,
            origin.y + direction.y * scale,
            origin.z + direction.z * scale,
        };
    }

    [[nodiscard]] inline bool intersect(const Ray& ray, const Panel& panel, Hit& outHit) noexcept
    {
        outHit = {};
        constexpr float kRayPlaneEpsilon = 0.0001f;
        if (!finite(ray.origin) || !finite(ray.direction) || !finite(panel.center) ||
            !finite(panel.right) || !finite(panel.up) || !finite(panel.front) ||
            !std::isfinite(ray.maxDistance) || ray.maxDistance <= 0.0f ||
            !std::isfinite(panel.width) || panel.width <= 0.0f ||
            !std::isfinite(panel.height) || panel.height <= 0.0f) {
            return false;
        }

        const float denominator = dot(ray.direction, panel.front);
        if (!std::isfinite(denominator) || std::fabs(denominator) < kRayPlaneEpsilon) {
            return false;
        }
        const float distance = dot(subtract(panel.center, ray.origin), panel.front) / denominator;
        if (!std::isfinite(distance) || distance < 0.0f || distance > ray.maxDistance) {
            return false;
        }

        const Vector panelOffset = subtract(addScaled(ray.origin, ray.direction, distance), panel.center);
        const float horizontal = dot(panelOffset, panel.right);
        const float vertical = dot(panelOffset, panel.up);
        if (!std::isfinite(horizontal) || !std::isfinite(vertical)) {
            return false;
        }
        outHit.distance = distance;
        outHit.horizontal = horizontal;
        outHit.vertical = vertical;
        outHit.u = horizontal / panel.width + 0.5f;
        outHit.v = 0.5f - vertical / panel.height;
        outHit.inside = std::fabs(horizontal) <= panel.width * 0.5f &&
                        std::fabs(vertical) <= panel.height * 0.5f;
        return std::isfinite(outHit.u) && std::isfinite(outHit.v);
    }

    [[nodiscard]] inline bool intersects(const Ray& ray, const Panel& panel) noexcept
    {
        Hit hit{};
        return intersect(ray, panel, hit) && hit.inside;
    }
}
