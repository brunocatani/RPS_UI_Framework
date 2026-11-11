#pragma once
#include <cctype>
#include <span>
#include <string>
#include <string_view>

/*
 * Reads a setting's help the way RPS mods write it, so a settings page can show a title,
 * one line about the setting and one line under each choice, as the setup wizard does:
 *
 *   Far Grab Style                          <- title: short, no full stop
 *   1 = Immediate. 2 = Pull.                <- the choices (the control builds the list)
 *   Immediate: brings it to your hand.      <- "label: line" under that choice
 *   Pull: hold grab to lock on.
 *   False: the game's own throw.            <- the line under Off, for on/off settings
 *   Anything else                           <- the summary is the first such line
 */
namespace rpsui::settings_menu::setting_help
{
    [[nodiscard]] inline bool isTitle(std::string_view line) noexcept
    {
        return !line.empty() && line.size() <= 48 && line.back() != '.' && line.find(": ") == std::string_view::npos &&
               line.find(" = ") == std::string_view::npos && line.find(". ") == std::string_view::npos;
    }

    [[nodiscard]] inline std::string_view title(std::span<const std::string> lines) noexcept
    {
        return !lines.empty() && isTitle(lines.front()) ? std::string_view(lines.front()) : std::string_view{};
    }

    // "label: line" for this choice's label, case-insensitive on the label.
    [[nodiscard]] inline std::string_view choiceLine(std::span<const std::string> lines, std::string_view label) noexcept
    {
        if (label.empty()) return {};
        for (const std::string_view line : lines) {
            if (line.size() <= label.size() + 2 || line.substr(label.size(), 2) != ": ") continue;
            bool same = true;
            for (std::size_t i = 0; i < label.size() && same; ++i)
                same = std::tolower(static_cast<unsigned char>(line[i])) == std::tolower(static_cast<unsigned char>(label[i]));
            if (same) return line.substr(label.size() + 2);
        }
        return {};
    }

    [[nodiscard]] inline std::string_view offLine(std::span<const std::string> lines) noexcept
    {
        return choiceLine(lines, "False");
    }

    // The first line that is not the title, the choice list or a choice's line.
    [[nodiscard]] inline std::string_view summary(std::span<const std::string> lines) noexcept
    {
        for (std::size_t i = title(lines).empty() ? 0 : 1; i < lines.size(); ++i) {
            const std::string_view line = lines[i];
            const auto colon = line.find(": ");
            const bool choiceList = line.find(" = ") != std::string_view::npos && std::isdigit(static_cast<unsigned char>(line.front()));
            const bool labelled = colon != std::string_view::npos && colon <= 32 && line.substr(0, colon).find('.') == std::string_view::npos;
            if (!choiceList && !labelled) return line;
        }
        return {};
    }

    // "06. Grabbing" -> "Grabbing".
    [[nodiscard]] inline std::string_view categoryTitle(std::string_view category) noexcept
    {
        std::size_t i = 0;
        while (i < category.size() && std::isdigit(static_cast<unsigned char>(category[i]))) ++i;
        if (i > 0 && category.substr(i, 2) == ". ") return category.substr(i + 2);
        return category;
    }
}
