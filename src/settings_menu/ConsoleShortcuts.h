#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rpsui::settings_menu
{
    /**
     * One button on the Console tab: its label and the commands it runs in order.
     */
    struct ConsoleShortcut
    {
        std::string label;
        std::vector<std::string> commands;
    };

    /**
     * ConsoleCommands.ini, the player's list of console shortcuts. Each `[Label]` header
     * starts a button; every non-empty line under it is one console command. Lines whose
     * first non-blank character is `;` or `#` are notes. Lines before the first header and
     * headers with no commands are ignored. Pure text handling, no I/O.
     */
    [[nodiscard]] inline std::vector<ConsoleShortcut> parseConsoleShortcuts(std::string_view text)
    {
        constexpr std::string_view kBlank = " \t\r";
        constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
        if (text.starts_with(kUtf8Bom)) {
            text.remove_prefix(kUtf8Bom.size());
        }
        const auto trim = [&](std::string_view value) {
            const auto first = value.find_first_not_of(kBlank);
            if (first == std::string_view::npos) {
                return std::string_view{};
            }
            return value.substr(first, value.find_last_not_of(kBlank) - first + 1);
        };

        std::vector<ConsoleShortcut> shortcuts;
        bool inSection = false;
        while (!text.empty()) {
            const auto end = text.find('\n');
            const auto line = trim(text.substr(0, end));
            text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
            if (line.empty() || line.front() == ';' || line.front() == '#') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                const auto label = trim(line.substr(1, line.size() - 2));
                inSection = !label.empty();
                if (inSection) {
                    shortcuts.push_back({ std::string(label), {} });
                }
                continue;
            }
            if (inSection) {
                shortcuts.back().commands.emplace_back(line);
            }
        }
        std::erase_if(shortcuts, [](const ConsoleShortcut& shortcut) { return shortcut.commands.empty(); });
        return shortcuts;
    }

    // Written once when the file does not exist, so the format explains itself.
    inline constexpr std::string_view kDefaultConsoleShortcuts =
        "; RobCo Settings menu console shortcuts.\r\n"
        "; Each [Label] is one button on the Console tab. The lines under it run in order\r\n"
        "; when the button is pressed, exactly as if typed into the console.\r\n"
        "; Lines starting with ; or # are notes. Commands cannot contain %.\r\n"
        "; Edit this file while the game runs; the settings menu reloads it when the Console tab opens\r\n"
        "; or when Reload is pressed.\r\n"
        "\r\n"
        "[God mode]\r\n"
        "tgm\r\n"
        "\r\n"
        "[No clip]\r\n"
        "tcl\r\n";
}
