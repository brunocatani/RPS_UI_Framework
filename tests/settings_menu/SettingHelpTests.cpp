#include "SettingHelp.h"
#include <cstdio>
#include <string>
#include <vector>

// The help convention ROCK, PAPER and the other RPS mods publish, read the way the settings
// page shows it: a title, a summary, and one line per choice.
namespace
{
    int failures = 0;
    void require(bool value, const char* message)
    {
        if (!value) {
            std::fprintf(stderr, "FAILED: %s\n", message);
            ++failures;
        }
    }
}

int main()
{
    namespace help = rpsui::settings_menu::setting_help;
    const std::vector<std::string> choice{
        "Far Grab Style",
        "1 = Immediate. 2 = Pull.",
        "Immediate: grabbing a far object brings it to your hand at once.",
        "Pull: hold grab to lock on, then flick your hand toward yourself.",
        "A pulled object follows your hand while you move or turn.",
    };
    require(help::title(choice) == "Far Grab Style", "title not read");
    require(help::summary(choice) == "A pulled object follows your hand while you move or turn.", "summary skipped past the choices");
    require(help::choiceLine(choice, "pull") == "hold grab to lock on, then flick your hand toward yourself.", "choice line not matched without case");
    require(help::choiceLine(choice, "Pul").empty(), "choice line matched a label prefix");

    const std::vector<std::string> toggle{
        "Bolt Hold Open",
        "An empty gun's bolt stays open until a reload starts.",
        "False: the bolt follows the animation.",
    };
    require(help::summary(toggle) == "An empty gun's bolt stays open until a reload starts.", "toggle summary not read");
    require(help::offLine(toggle) == "the bolt follows the animation.", "off line not read");

    const std::vector<std::string> untitled{ "Applies at once. Two sentences make no title." };
    require(help::title(untitled).empty(), "a sentence was read as a title");
    require(help::summary(untitled) == untitled.front(), "untitled summary lost");
    require(help::title({}).empty() && help::summary({}).empty(), "empty help produced text");

    require(help::categoryTitle("03. Prompts and Quick Loot") == "Prompts and Quick Loot", "category number kept");
    require(help::categoryTitle("Developer") == "Developer", "plain category changed");
    if (failures == 0) std::puts("Setting help checks passed.");
    return failures == 0 ? 0 : 1;
}
