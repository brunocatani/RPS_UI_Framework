#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

namespace rpsui::settings_menu
{
    // The engine passes a typed command to its print function as the format string
    // when it echoes it (0x1412DC460 calls 0x1412E3E50 with the command as format), so
    // a % specifier would read unrelated registers as arguments and can crash the game.
    [[nodiscard]] constexpr bool commandIsSafe(std::string_view command) noexcept
    {
        return command.find('%') == std::string_view::npos;
    }

    /**
     * A line both keyboards type into: the console command line and the Spawner's
     * searches. It is separate from ImGui's text field because clicking a key on the
     * keyboard panel would take focus from that field.
     * Holds UTF-8; the caret is always on a character boundary.
     */
    class ConsoleLine
    {
    public:
        static constexpr std::size_t kMaximumBytes = 512;
        static constexpr std::size_t kMaximumHistory = 64;

        void insert(std::string_view text)
        {
            if (_text.size() + text.size() > kMaximumBytes) {
                return;
            }
            _text.insert(_caret, text);
            _caret += text.size();
            _historyIndex = _history.size();
        }

        void backspace()
        {
            if (_caret == 0) {
                return;
            }
            const auto start = previousBoundary(_caret);
            _text.erase(start, _caret - start);
            _caret = start;
        }

        void erase()
        {
            if (_caret < _text.size()) {
                _text.erase(_caret, nextBoundary(_caret) - _caret);
            }
        }

        void moveLeft() { if (_caret > 0) _caret = previousBoundary(_caret); }
        void moveRight() { if (_caret < _text.size()) _caret = nextBoundary(_caret); }
        void moveHome() { _caret = 0; }
        void moveEnd() { _caret = _text.size(); }

        void clear()
        {
            _text.clear();
            _caret = 0;
            _historyIndex = _history.size();
        }

        void historyPrevious()
        {
            if (_historyIndex > 0) {
                --_historyIndex;
                showHistory();
            }
        }

        void historyNext()
        {
            if (_historyIndex < _history.size()) {
                ++_historyIndex;
                showHistory();
            }
        }

        // Empties the line and records it, without repeating the newest history entry.
        [[nodiscard]] std::string take()
        {
            auto command = std::move(_text);
            clear();
            if (!command.empty() && (_history.empty() || _history.back() != command)) {
                _history.push_back(command);
                if (_history.size() > kMaximumHistory) {
                    _history.pop_front();
                }
            }
            _historyIndex = _history.size();
            return command;
        }

        [[nodiscard]] const std::string& text() const noexcept { return _text; }
        [[nodiscard]] std::size_t caret() const noexcept { return _caret; }

    private:
        [[nodiscard]] bool continuation(std::size_t index) const noexcept
        {
            return (static_cast<unsigned char>(_text[index]) & 0xC0) == 0x80;
        }

        [[nodiscard]] std::size_t previousBoundary(std::size_t index) const noexcept
        {
            do { --index; } while (index > 0 && continuation(index));
            return index;
        }

        [[nodiscard]] std::size_t nextBoundary(std::size_t index) const noexcept
        {
            do { ++index; } while (index < _text.size() && continuation(index));
            return index;
        }

        void showHistory()
        {
            _text = _historyIndex < _history.size() ? _history[_historyIndex] : std::string{};
            _caret = _text.size();
        }

        std::string _text;
        std::size_t _caret{ 0 };
        std::deque<std::string> _history;
        std::size_t _historyIndex{ 0 };
    };
}
