#pragma once

// RPS UI's built-in themes, in the colours of Fallout's own screens: Pip-Boy
// (the game's HUD green, the default), New Vegas (its amber) and Vault-Tec (blue
// and yellow). The host writes each to Themes\<name>.toml, where players pick one
// in RPS_UI.toml or copy one to make their own, and falls back to a theme's
// built-in values for anything its file lacks; desktop previews and tests draw
// with the default. Standard library only.

#include <RPS_UI_Framework/Framework.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace rpsui::theme_defaults
{
    // One theme colour: its file key, help line and the Theme member it fills.
    struct ColorEntry
    {
        std::string_view key;
        std::string_view help;
        api::framework::ThemeColor api::framework::Theme::* member;
    };

    inline constexpr std::array<ColorEntry, 17> kColors{ {
        { "Background", "A panel's body.", &api::framework::Theme::background },
        { "Surface", "Idle controls, headers and rails.", &api::framework::Theme::surface },
        { "Hover", "Under the pointer.", &api::framework::Theme::hover },
        { "Pressed", "While pressed or dragged.", &api::framework::Theme::pressed },
        { "Selected", "The open page and the current choice.", &api::framework::Theme::selected },
        { "Border", "Control outlines.", &api::framework::Theme::border },
        { "Divider", "Rules and separators.", &api::framework::Theme::divider },
        { "Text", "Text.", &api::framework::Theme::text },
        { "TextMuted", "Help, units and secondary text.", &api::framework::Theme::textMuted },
        { "TextDisabled", "Text of things that cannot be used now.", &api::framework::Theme::textDisabled },
        { "Accent", "Chosen things, links and the cursor; the solid fill of a page's main action.", &api::framework::Theme::accent },
        { "OnAccent", "Text and marks on an Accent fill.", &api::framework::Theme::onAccent },
        { "Focus", "The control the thumbstick adjusts.", &api::framework::Theme::focus },
        { "Pending", "A change that is not saved yet.", &api::framework::Theme::pending },
        { "Invalid", "A refused value and errors.", &api::framework::Theme::invalid },
        { "Success", "A finished action.", &api::framework::Theme::success },
        { "Scrim", "Behind a dialog.", &api::framework::Theme::scrim },
    } };

    // A built-in theme: its name (also its file name) and a "#RRGGBBAA" value per
    // kColors entry, in that order. Bodies are slightly see-through, so the world
    // shows behind a panel.
    struct BuiltInTheme
    {
        std::string_view name;
        std::array<std::string_view, kColors.size()> colors;
    };

    inline constexpr std::array<BuiltInTheme, 3> kBuiltIns{ {
        // Fallout 4's default HUD and Pip-Boy green, #12FF15.
        { "Pip-Boy",
            { "#03140AD9", "#12FF1512", "#12FF152E", "#12FF1547", "#12FF1526", "#12FF1566", "#12FF1533", "#12FF15FF",
                "#12FF15B8", "#12FF155C", "#12FF15FF", "#03140AFF", "#12FF15D1", "#FFB642FF", "#FF5A4AFF", "#12FF15FF",
                "#03140ACC" } },
        // Fallout: New Vegas's default HUD amber, #FFB642.
        { "New Vegas",
            { "#140D02D9", "#FFB64212", "#FFB6422E", "#FFB64247", "#FFB64226", "#FFB64266", "#FFB64233", "#FFB642FF",
                "#FFB642B8", "#FFB6425C", "#FFB642FF", "#140D02FF", "#FFB642D1", "#FFF1C9FF", "#FF5A4AFF", "#FFB642FF",
                "#140D02CC" } },
        // Vault-Tec's navy and yellow.
        { "Vault-Tec",
            { "#0A1A33E6", "#E8EFF812", "#FFC72C2E", "#FFC72C47", "#FFC72C26", "#FFC72C66", "#E8EFF833", "#E8EFF8FF",
                "#E8EFF8B8", "#E8EFF85C", "#FFC72CFF", "#0A1A33FF", "#FFC72CD1", "#FF9F43FF", "#FF6B5EFF", "#5FD38AFF",
                "#0A1A33CC" } },
    } };

    // The default theme's name, the one RPS_UI.toml starts with.
    inline constexpr std::string_view kName = kBuiltIns[0].name;

    [[nodiscard]] constexpr const BuiltInTheme* findBuiltIn(std::string_view name) noexcept
    {
        for (const auto& theme : kBuiltIns)
            if (theme.name == name) return &theme;
        return nullptr;
    }

    // One face: its file key, help line and default file. A file is looked
    // up in Mods_Config\RPS_UI_Framework\Fonts, then in Windows' fonts.
    struct FontEntry
    {
        std::string_view key;
        std::string_view help;
        std::string_view file;
    };

    inline constexpr std::array<FontEntry, api::framework::kFontRoleCount> kFonts{ {
        { "Interface", "Labels and controls.", "consola.ttf" },
        { "Strong", "Headings and emphasis.", "consolab.ttf" },
        { "Mono", "Numbers, code and console text.", "consola.ttf" },
        { "Prose", "Long reading text such as dialogue.", "segoeui.ttf" },
    } };

    struct MetricEntry
    {
        std::string_view key;
        std::string_view help;
        float value;
        float minimum;
        float maximum;
        float api::framework::Theme::* member;
    };

    inline constexpr std::array<MetricEntry, 8> kMetrics{ {
        { "FontSize", "Body text size, in layout units.", 20.0f, 10.0f, 48.0f, &api::framework::Theme::fontSize },
        { "TitleFontSize", "Heading size.", 27.0f, 12.0f, 72.0f, &api::framework::Theme::titleFontSize },
        { "SmallFontSize", "The smallest text drawn.", 17.0f, 10.0f, 40.0f, &api::framework::Theme::smallFontSize },
        { "TargetHeight", "The least height of anything you press.", 40.0f, 24.0f, 96.0f, &api::framework::Theme::targetHeight },
        { "Padding", "Space inside controls.", 10.0f, 0.0f, 48.0f, &api::framework::Theme::padding },
        { "Spacing", "Space between controls.", 8.0f, 0.0f, 48.0f, &api::framework::Theme::spacing },
        { "Rounding", "Corner radius of controls and windows.", 8.0f, 0.0f, 24.0f, &api::framework::Theme::rounding },
        { "BorderWidth", "Outline width.", 1.5f, 0.0f, 4.0f, &api::framework::Theme::borderWidth },
    } };

    // "#RRGGBB" or "#RRGGBBAA". False for anything else.
    [[nodiscard]] constexpr bool parseColor(std::string_view text, api::framework::ThemeColor& out) noexcept
    {
        if (text.size() != 7 && text.size() != 9) return false;
        if (text[0] != '#') return false;
        std::uint32_t channels[4]{ 0, 0, 0, 255 };
        for (std::size_t i = 1; i < text.size(); ++i) {
            const char c = text[i];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
            const std::size_t channel = (i - 1) / 2;
            channels[channel] = ((i - 1) % 2 == 0) ? digit * 16 : channels[channel] + digit;
        }
        out = { channels[0] / 255.0f, channels[1] / 255.0f, channels[2] / 255.0f, channels[3] / 255.0f };
        return true;
    }

    [[nodiscard]] inline api::framework::Theme builtIn(const BuiltInTheme& source) noexcept
    {
        api::framework::Theme theme{};
        theme.revision = 1;
        std::memcpy(theme.name, source.name.data(), std::min(source.name.size(), sizeof(theme.name) - 1));
        for (std::size_t i = 0; i < kColors.size(); ++i) (void)parseColor(source.colors[i], theme.*kColors[i].member);
        for (const auto& entry : kMetrics) theme.*entry.member = entry.value;
        return theme;
    }

    [[nodiscard]] inline api::framework::Theme defaultTheme() noexcept { return builtIn(kBuiltIns[0]); }
}
