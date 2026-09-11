#pragma once

namespace rpsui::render::RenderHooks
{
    enum class InstallResult { Installed, Conflict, Unavailable };
    [[nodiscard]] InstallResult Install() noexcept;
    [[nodiscard]] bool IsInstalled() noexcept;
}
