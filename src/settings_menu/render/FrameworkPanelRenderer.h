#pragma once

namespace devui::render
{
    [[nodiscard]] bool InstallFrameworkPanel() noexcept;
    // Config back where it opened, at its default size.
    [[nodiscard]] bool ResetFrameworkPanelLayout() noexcept;
}
