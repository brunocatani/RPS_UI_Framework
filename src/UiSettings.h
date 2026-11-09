#pragma once

// RPS UI's own settings, Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml.
//
// Thread model: start() reads the file on the plugin load thread, then a
// watcher thread polls its write time and rereads it after an edit (PALM's
// settings pages or a text editor). Each read publishes a new immutable
// Snapshot; render and input threads take the current one with current(),
// lock-free, and never touch the file.

#include <cstdint>
#include <memory>

namespace rpsui::settings
{
    struct Snapshot
    {
        // Texture pixels per layout unit at a panel's default size.
        float sharpness{ 1.5f };
        std::uint64_t revision{ 0 };
    };

    inline constexpr float kMinimumSharpness = 1.0f;
    inline constexpr float kMaximumSharpness = 3.0f;

    // Plugin load thread, once.
    void start() noexcept;
    // Any thread.
    [[nodiscard]] std::shared_ptr<const Snapshot> current() noexcept;
}
