#include "PanelResizePolicy.h"

#include <array>
#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    [[nodiscard]] bool near(float left, float right) noexcept
    {
        return std::fabs(left - right) < 0.001f;
    }
}

int main()
{
    using namespace rpsui::settings_menu::panel_resize;
    constexpr Constraints constraints{};

    expect(hitTest(0.001f, 0.5f, 1440.0f, 900.0f) == Handle::Left,
        "the left border selects the left resize handle");
    expect(hitTest(0.999f, 0.001f, 1440.0f, 900.0f) == Handle::TopRight,
        "the top-right corner takes precedence over its edges");
    expect(hitTest(0.5f, 0.5f, 1440.0f, 900.0f) == Handle::None,
        "the panel interior remains normal UI input");

    constexpr std::array handles{
        Handle::Left,
        Handle::Right,
        Handle::Top,
        Handle::Bottom,
        Handle::TopLeft,
        Handle::TopRight,
        Handle::BottomLeft,
        Handle::BottomRight,
    };
    for (const auto handle : handles) {
        const auto drag = begin(handle, 112.5f, constraints);
        const auto unchanged = update(
            drag,
            drag.anchorX + drag.directionX,
            drag.anchorY + drag.directionY,
            constraints);
        expect(near(unchanged.centerX, 0.0f) && near(unchanged.centerY, 0.0f) &&
               near(unchanged.width, 112.5f) && near(unchanged.height, 70.3125f),
            "every handle preserves the initial rectangle before pointer movement");
    }

    const auto right = begin(Handle::Right, 112.5f, constraints);
    const auto rightGeometry = update(right, 83.75f, 0.0f, constraints);
    expect(near(rightGeometry.width, 140.0f) &&
           near(rightGeometry.height, 87.5f) &&
           near(rightGeometry.centerX - rightGeometry.width * 0.5f, -56.25f),
        "right-edge dragging preserves the opposite left edge and aspect ratio");

    const auto top = begin(Handle::Top, 112.5f, constraints);
    const auto topGeometry = update(top, 0.0f, 55.0f, constraints);
    expect(near(topGeometry.width, 144.25f) &&
           near(topGeometry.centerY - topGeometry.height * 0.5f, -35.15625f),
        "top-edge dragging preserves the opposite bottom edge");

    const auto corner = begin(Handle::TopRight, 112.5f, constraints);
    const auto doubled = update(
        corner,
        corner.anchorX + corner.directionX * 2.0f,
        corner.anchorY + corner.directionY * 2.0f,
        constraints);
    expect(near(doubled.width, constraints.maximumWidth) &&
           near(doubled.height, constraints.maximumWidth / constraints.aspectRatio),
        "corner dragging projects onto the aspect diagonal and clamps at maximum size");

    const auto minimum = update(right, -1000.0f, 0.0f, constraints);
    expect(near(minimum.width, constraints.minimumWidth),
        "edge dragging cannot invert or shrink below the safe minimum");

    if (failures != 0) {
        std::cerr << failures << " panel-resize assertion(s) failed.\n";
        return 1;
    }
    std::cout << "DevUI panel-resize policy tests passed.\n";
    return 0;
}
