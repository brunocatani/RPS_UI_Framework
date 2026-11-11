#include "ConsoleShortcuts.h"

#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
}

int main()
{
    using rpsui::settings_menu::parseConsoleShortcuts;

    // Players edit this file in Notepad: BOM, CRLF, indentation and notes all occur.
    const auto shortcuts = parseConsoleShortcuts(
        "\xEF\xBB\xBF; notes\r\n"
        "orphan command before any header\r\n"
        "[ God mode ]\r\n"
        "  tgm  \r\n"
        "# also a note\r\n"
        "\r\n"
        "[Empty]\r\n"
        "[]\r\n"
        "ignored under a blank header\r\n"
        "[Review box]\r\n"
        "cgf \"ROCKOffsetReview.Spawn\"\r\n"
        "player.additem f 100");
    require(shortcuts.size() == 2, "headers without commands or labels became buttons");
    require(shortcuts[0].label == "God mode", "label not trimmed");
    require(shortcuts[0].commands.size() == 1 && shortcuts[0].commands[0] == "tgm", "command not trimmed");
    require(shortcuts[1].label == "Review box", "second button lost");
    require(shortcuts[1].commands.size() == 2 && shortcuts[1].commands[0] == "cgf \"ROCKOffsetReview.Spawn\"" &&
                shortcuts[1].commands[1] == "player.additem f 100",
        "multi-command button lost a command or its quotes");

    const auto defaults = parseConsoleShortcuts(rpsui::settings_menu::kDefaultConsoleShortcuts);
    require(!defaults.empty(), "the default file gives no buttons");
    return 0;
}
