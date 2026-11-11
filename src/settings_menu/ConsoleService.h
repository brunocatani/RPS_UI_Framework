#pragma once

#include "ConsoleShortcuts.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The game console's output and command queue, without its menu.
namespace rpsui::settings_menu::console
{
    // Plugin load, before the engine prints anything. Fails closed and logs when the
    // call site is not the expected code; the Console tab then reports no output.
    bool installCapture() noexcept;
    [[nodiscard]] bool captureInstalled() noexcept;
    // Any thread. Appends the UTF-8 lines printed since the last call; the oldest are
    // dropped past a fixed limit while nobody reads them.
    void takeLines(std::vector<std::string>& output);
    // Game thread. Queues a UTF-8 command as if typed into the console, so it runs on
    // a later frame and handles `;` chains and commands that load or quit.
    void run(const std::string& command);
    // Game thread. The form ID of the console's selected reference.
    [[nodiscard]] std::optional<std::uint32_t> selectedReference();

    struct ShortcutFile
    {
        std::vector<ConsoleShortcut> shortcuts;
        // Shown on the Console tab when the file gives no buttons.
        std::string problem;
    };
    // Game thread, on Console tab entry and Reload only (file I/O). Reads
    // Mods_Config/RPS_UI_Framework/ConsoleCommands.ini, first writing the default list when
    // the file does not exist. The preview build returns a fixture and touches no file.
    [[nodiscard]] ShortcutFile loadShortcuts();
}
