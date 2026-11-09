#pragma once

// RPS UI's own settings, Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml,
// and the theme it names (ThemeStore.h).
//
// Thread model: start() reads both files on the plugin load thread, then a
// watcher thread polls their write times and rereads them after an edit (PALM's
// settings pages or a text editor). Each read publishes a new immutable
// Snapshot; render and input threads take the current one with current(),
// lock-free, and never touch the files.

#include "ThemeStore.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rpsui::settings
{
    struct Snapshot
    {
        // Texture pixels per layout unit at a panel's default size.
        float sharpness{ 1.5f };
        // Never null once start() ran; its revision changes with every theme change.
        std::shared_ptr<const theme_store::ThemeData> theme;
        std::uint64_t revision{ 0 };
    };

    inline constexpr float kMinimumSharpness = 1.0f;
    inline constexpr float kMaximumSharpness = 3.0f;

    // Plugin load thread, once.
    void start() noexcept;
    // Any thread. Before start() its theme is null.
    [[nodiscard]] std::shared_ptr<const Snapshot> current() noexcept;
}
