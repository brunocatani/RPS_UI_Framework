#include "ConsoleLine.h"

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
    using rpsui::settings_menu::ConsoleLine;
    using rpsui::settings_menu::commandIsSafe;

    // A % reaches the engine's print function as a format specifier and can crash it.
    require(!commandIsSafe("player.additem %s"), "format specifier accepted");
    require(commandIsSafe("player.additem f 100; coc qasmoke"), "ordinary command refused");

    // The caret must stay on a character boundary, or the text stops being UTF-8.
    ConsoleLine line;
    line.insert("a\xC3\xA7z"); // a, c-cedilla, z
    line.moveLeft();
    line.moveLeft();
    require(line.caret() == 1, "left stopped inside a two-byte character");
    line.moveRight();
    require(line.caret() == 3, "right stopped inside a two-byte character");
    line.backspace();
    require(line.text() == "az" && line.caret() == 1, "backspace split a two-byte character");
    line.moveHome();
    line.erase();
    require(line.text() == "z", "delete removed the wrong character");

    ConsoleLine bounded;
    bounded.insert(std::string(ConsoleLine::kMaximumBytes, 'x'));
    bounded.insert("y");
    require(bounded.text().size() == ConsoleLine::kMaximumBytes, "line grew past its limit");

    ConsoleLine history;
    history.insert("one");
    require(history.take() == "one" && history.text().empty() && history.caret() == 0, "take left text behind");
    history.insert("two");
    (void)history.take();
    history.insert("two");
    (void)history.take();
    history.historyPrevious();
    require(history.text() == "two" && history.caret() == 3, "newest history entry not shown first");
    history.historyPrevious();
    require(history.text() == "one", "repeated command stored twice");
    history.historyPrevious();
    require(history.text() == "one", "history walked past its oldest entry");
    history.historyNext();
    history.historyNext();
    require(history.text().empty(), "history did not return to an empty line");
    return 0;
}
