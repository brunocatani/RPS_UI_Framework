#pragma once

// The settings menu's place in the plugin's lifecycle (src/settings_menu holds the menu).
// RPS UI runs it as a consumer of its own Framework and Input tables, so it starts only
// once the host is ready, and it takes input only during gameplay.
namespace rpsui::settings_menu_host
{
    // F4SEPlugin_Load: the console output capture, which patches a call site at load.
    void onPluginLoad() noexcept;
    // kGameDataReady, after the host started: registers the menu's panels, subscribes to
    // input and builds the Spawner's index.
    void onHostReady() noexcept;
    // kPreLoadGame (false), kNewGame and kPostLoadGame (true). A session start also gives the
    // player the settings holotape when they have none.
    void onSessionReady(bool ready) noexcept;

    struct Pose
    {
        float center[3]{}, right[3]{}, up[3]{}, front[3]{};
        float physicalWidth{ 0.0f };
    };
    // Game thread. Opens the menu at the pose, or in front of the headset without one.
    // False when the menu is unavailable (no gameplay, the host not ready) or already open.
    bool open(const Pose* pose) noexcept;
    void close() noexcept;
    // Open or opening.
    [[nodiscard]] bool isOpen() noexcept;
    [[nodiscard]] std::uint64_t closedCount() noexcept;
}
