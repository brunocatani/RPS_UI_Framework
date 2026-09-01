#pragma once

namespace rpsui::render::RenderHooks
{
    [[nodiscard]] bool Install() noexcept;
    [[nodiscard]] bool IsInstalled() noexcept;
}
