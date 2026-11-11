#pragma once

#include "ConsoleLine.h"
#include "ConsoleShortcuts.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rpsui::settings_menu
{
    struct ConsoleStatus
    {
        std::optional<std::uint32_t> selectedReference;
        bool keyboardHeld{ false };
        bool windowFocused{ false };
        bool outputCaptured{ false };
    };

    /**
     * The Console tab: scrollback and command line. The on-screen keyboard lives on its own
     * panel under the Config panel and types into line(). Everything here is presentation
     * state. The caller holds the configurator runtime mutex around every call, because
     * drawing runs on the render thread and physical keys arrive from the game thread.
     * Commands wait in takeCommands() for the game thread. The shortcut buttons come from
     * ConsoleCommands.ini, which the game thread loads and hands over with setShortcuts().
     */
    class ConsoleTab
    {
    public:
        void draw(const ConsoleStatus& status);
        void onCharacter(std::uint32_t utf16Unit);
        void onKey(std::uint32_t virtualKey);
        void submit();
        [[nodiscard]] ConsoleLine& line() noexcept { return _line; }
        [[nodiscard]] std::vector<std::string> takeCommands();
        [[nodiscard]] bool hasCommands() const noexcept { return !_commands.empty(); }
        void setShortcuts(std::vector<ConsoleShortcut> shortcuts, std::string problem);
        // True once after Reload was pressed; the game thread then reloads the file.
        [[nodiscard]] bool takeReloadRequest() noexcept { return std::exchange(_reloadRequested, false); }
        [[nodiscard]] bool hasReloadRequest() const noexcept { return _reloadRequested; }

    private:
        void drawStatusRow(const ConsoleStatus& status);
        void drawShortcuts(float height);
        void drawLog(float height);
        void drawCommandRow();
        void addRow(std::string_view line, float wrapWidth);
        void absorbNewLines(float wrapWidth);
        void runShortcut(const ConsoleShortcut& shortcut);

        ConsoleLine _line;
        std::deque<std::string> _rows;
        std::vector<std::string> _incoming;
        std::vector<std::string> _commands;
        std::vector<ConsoleShortcut> _shortcuts;
        std::string _shortcutProblem{ "Loading console shortcuts" };
        bool _reloadRequested{ false };
        float _commandScroll{ 0.0f };
        float _pageRequest{ 0.0f };
        bool _followLatest{ true };
    };
}
