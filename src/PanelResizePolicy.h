#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rpsui::panel_resize
{
    enum class Handle : std::uint8_t
    {
        None,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    struct Constraints
    {
        float aspectRatio{ 1.6f };
        float minimumWidth{ 72.0f };
        float maximumWidth{ 180.0f };
    };

    struct Geometry
    {
        float centerX{ 0.0f };
        float centerY{ 0.0f };
        float width{ 0.0f };
        float height{ 0.0f };
    };

    struct Drag
    {
        Handle handle{ Handle::None };
        float initialWidth{ 0.0f };
        float initialHeight{ 0.0f };
        float anchorX{ 0.0f };
        float anchorY{ 0.0f };
        float directionX{ 0.0f };
        float directionY{ 0.0f };
    };

    [[nodiscard]] constexpr bool isCorner(Handle handle) noexcept
    {
        return handle == Handle::TopLeft || handle == Handle::TopRight ||
               handle == Handle::BottomLeft || handle == Handle::BottomRight;
    }

    [[nodiscard]] inline Handle hitTest(
        float u,
        float v,
        float pixelWidth,
        float pixelHeight,
        float edgePixels = 22.0f,
        float cornerPixels = 72.0f) noexcept
    {
        if (!std::isfinite(u) || !std::isfinite(v) ||
            !std::isfinite(pixelWidth) || !std::isfinite(pixelHeight) ||
            pixelWidth <= 0.0f || pixelHeight <= 0.0f ||
            u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
            return Handle::None;
        }
        const float edgeU = std::clamp(edgePixels / pixelWidth, 0.0f, 0.25f);
        const float edgeV = std::clamp(edgePixels / pixelHeight, 0.0f, 0.25f);
        const float cornerU = std::clamp(cornerPixels / pixelWidth, edgeU, 0.35f);
        const float cornerV = std::clamp(cornerPixels / pixelHeight, edgeV, 0.35f);
        const bool left = u <= edgeU;
        const bool right = u >= 1.0f - edgeU;
        const bool top = v <= edgeV;
        const bool bottom = v >= 1.0f - edgeV;

        if ((left && v <= cornerV) || (top && u <= cornerU)) {
            return Handle::TopLeft;
        }
        if ((right && v <= cornerV) || (top && u >= 1.0f - cornerU)) {
            return Handle::TopRight;
        }
        if ((left && v >= 1.0f - cornerV) || (bottom && u <= cornerU)) {
            return Handle::BottomLeft;
        }
        if ((right && v >= 1.0f - cornerV) ||
            (bottom && u >= 1.0f - cornerU)) {
            return Handle::BottomRight;
        }
        if (left) {
            return Handle::Left;
        }
        if (right) {
            return Handle::Right;
        }
        if (top) {
            return Handle::Top;
        }
        if (bottom) {
            return Handle::Bottom;
        }
        return Handle::None;
    }

    [[nodiscard]] inline Drag begin(
        Handle handle,
        float width,
        const Constraints& constraints) noexcept
    {
        Drag drag{};
        if (handle == Handle::None || !std::isfinite(width) ||
            !std::isfinite(constraints.aspectRatio) || constraints.aspectRatio <= 0.0f ||
            !std::isfinite(constraints.minimumWidth) ||
            !std::isfinite(constraints.maximumWidth) ||
            constraints.minimumWidth <= 0.0f ||
            constraints.maximumWidth < constraints.minimumWidth) {
            return drag;
        }
        drag.handle = handle;
        drag.initialWidth = std::clamp(width, constraints.minimumWidth, constraints.maximumWidth);
        drag.initialHeight = drag.initialWidth / constraints.aspectRatio;
        const float halfWidth = drag.initialWidth * 0.5f;
        const float halfHeight = drag.initialHeight * 0.5f;
        switch (handle) {
        case Handle::Left:
            drag.anchorX = halfWidth;
            drag.directionX = -drag.initialWidth;
            break;
        case Handle::Right:
            drag.anchorX = -halfWidth;
            drag.directionX = drag.initialWidth;
            break;
        case Handle::Top:
            drag.anchorY = -halfHeight;
            drag.directionY = drag.initialHeight;
            break;
        case Handle::Bottom:
            drag.anchorY = halfHeight;
            drag.directionY = -drag.initialHeight;
            break;
        case Handle::TopLeft:
            drag.anchorX = halfWidth;
            drag.anchorY = -halfHeight;
            drag.directionX = -drag.initialWidth;
            drag.directionY = drag.initialHeight;
            break;
        case Handle::TopRight:
            drag.anchorX = -halfWidth;
            drag.anchorY = -halfHeight;
            drag.directionX = drag.initialWidth;
            drag.directionY = drag.initialHeight;
            break;
        case Handle::BottomLeft:
            drag.anchorX = halfWidth;
            drag.anchorY = halfHeight;
            drag.directionX = -drag.initialWidth;
            drag.directionY = -drag.initialHeight;
            break;
        case Handle::BottomRight:
            drag.anchorX = -halfWidth;
            drag.anchorY = halfHeight;
            drag.directionX = drag.initialWidth;
            drag.directionY = -drag.initialHeight;
            break;
        case Handle::None:
            break;
        }
        return drag;
    }

    [[nodiscard]] inline Geometry update(
        const Drag& drag,
        float pointerX,
        float pointerY,
        const Constraints& constraints) noexcept
    {
        Geometry result{
            .width = drag.initialWidth,
            .height = drag.initialHeight,
        };
        if (drag.handle == Handle::None ||
            !std::isfinite(pointerX) || !std::isfinite(pointerY) ||
            !std::isfinite(constraints.aspectRatio) || constraints.aspectRatio <= 0.0f ||
            !std::isfinite(constraints.minimumWidth) ||
            !std::isfinite(constraints.maximumWidth) ||
            constraints.minimumWidth <= 0.0f ||
            constraints.maximumWidth < constraints.minimumWidth) {
            return result;
        }

        float width = drag.initialWidth;
        if (isCorner(drag.handle)) {
            const float denominator =
                drag.directionX * drag.directionX + drag.directionY * drag.directionY;
            if (denominator <= 0.0001f) {
                return result;
            }
            const float relativeX = pointerX - drag.anchorX;
            const float relativeY = pointerY - drag.anchorY;
            const float scale =
                (relativeX * drag.directionX + relativeY * drag.directionY) / denominator;
            width = drag.initialWidth * scale;
        } else if (drag.handle == Handle::Left) {
            width = drag.anchorX - pointerX;
        } else if (drag.handle == Handle::Right) {
            width = pointerX - drag.anchorX;
        } else if (drag.handle == Handle::Top) {
            width = (pointerY - drag.anchorY) * constraints.aspectRatio;
        } else if (drag.handle == Handle::Bottom) {
            width = (drag.anchorY - pointerY) * constraints.aspectRatio;
        }
        width = std::clamp(width, constraints.minimumWidth, constraints.maximumWidth);
        const float height = width / constraints.aspectRatio;

        float draggedX = drag.anchorX;
        float draggedY = drag.anchorY;
        switch (drag.handle) {
        case Handle::Left:
            draggedX = drag.anchorX - width;
            draggedY = 0.0f;
            break;
        case Handle::Right:
            draggedX = drag.anchorX + width;
            draggedY = 0.0f;
            break;
        case Handle::Top:
            draggedX = 0.0f;
            draggedY = drag.anchorY + height;
            break;
        case Handle::Bottom:
            draggedX = 0.0f;
            draggedY = drag.anchorY - height;
            break;
        case Handle::TopLeft:
            draggedX = drag.anchorX - width;
            draggedY = drag.anchorY + height;
            break;
        case Handle::TopRight:
            draggedX = drag.anchorX + width;
            draggedY = drag.anchorY + height;
            break;
        case Handle::BottomLeft:
            draggedX = drag.anchorX - width;
            draggedY = drag.anchorY - height;
            break;
        case Handle::BottomRight:
            draggedX = drag.anchorX + width;
            draggedY = drag.anchorY - height;
            break;
        case Handle::None:
            break;
        }

        result.centerX = (drag.anchorX + draggedX) * 0.5f;
        result.centerY = (drag.anchorY + draggedY) * 0.5f;
        result.width = width;
        result.height = height;
        return result;
    }
}
