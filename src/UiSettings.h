#pragma once

// RPS UI's own settings, Documents\My Games\Fallout4VR\Mods_Config\RPS_UI_Framework\RPS_UI.toml,
// and the theme it names (ThemeStore.h).
//
// Thread model: start() reads both files on the plugin load thread, then a
// watcher thread polls their write times and rereads them after an edit (Mods
// Config FO4 VR's Save or a text editor). Each read publishes a new immutable
// Snapshot; render and input threads take the current one with current(),
// lock-free, and never touch the files.
//
// Previews (Mods Config's live preview, ModsConfigClient.h) arrive on the game
// thread. They are kept under a lock and wake the watcher, which lays them over
// the file's values and publishes the next Snapshot; only the watcher reads the
// settings store and theme files.

#include "ThemeStore.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rpsui::settings
{
    struct Snapshot
    {
        // Texture pixels per layout unit at a panel's default size.
        float sharpness{ 1.5f };
        // Scales every UI controller pulse; 0 turns them off.
        float hapticStrength{ 0.6f };
        // How long a grip is held on a movable panel before it moves with the hand.
        float gripMoveSeconds{ 0.45f };
        // Never null once start() ran; its revision changes with every theme change.
        std::shared_ptr<const theme_store::ThemeData> theme;
        std::uint64_t revision{ 0 };
    };

    inline constexpr float kMinimumSharpness = 1.0f;
    inline constexpr float kMaximumSharpness = 3.0f;
    inline constexpr float kMinimumGripMoveSeconds = 0.2f;
    inline constexpr float kMaximumGripMoveSeconds = 1.5f;

    // Plugin load thread, once.
    void start() noexcept;
    // Any thread. Before start() its theme is null.
    [[nodiscard]] std::shared_ptr<const Snapshot> current() noexcept;

    // Game thread. A pending value of an RPS_UI.toml option ([table] key), shown until it is
    // discarded or the previews end; a table and key RPS_UI.toml does not have are ignored.
    void setPreview(std::string_view table, std::string_view key, std::string value) noexcept;
    void discardPreview(std::string_view table, std::string_view key) noexcept;
    void clearPreviews() noexcept;
    // Game thread, after RPS_UI.toml was saved: the previews end and the file is read at once.
    void endPreviewsAndReload() noexcept;

    // Game thread, after start(): the names of the theme files in Themes, alphabetical, with
    // the theme in use added when its file is gone. Lists the folder; never call it per frame.
    [[nodiscard]] std::vector<std::string> themeChoices();
}
