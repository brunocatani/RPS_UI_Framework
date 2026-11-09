#pragma once

// RPS UI's built-in theme, "RobCo": a terminal's phosphor green on dark ink.
// The host writes it to Themes\RobCo.toml as the template for players' own
// themes and falls back to it for anything a theme file lacks; desktop
// previews and tests draw with it. Standard library only.

#include <RPS_UI_Framework/Framework.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace rpsui::theme_defaults
{
    inline constexpr std::string_view kName = "RobCo";

    // One theme colour: its file key, help line and "#RRGGBBAA" default.
    struct ColorEntry
    {
        std::string_view key;
        std::string_view help;
        std::string_view value;
        api::framework::ThemeColor api::framework::Theme::* member;
    };

    inline constexpr std::array<ColorEntry, 17> kColors{ {
        { "Background", "A panel's body.", "#051007FF", &api::framework::Theme::background },
        { "Surface", "Idle controls, headers and rails.", "#7CF06C0D", &api::framework::Theme::surface },
        { "Hover", "Under the pointer.", "#7CF06C29", &api::framework::Theme::hover },
        { "Pressed", "While pressed or dragged.", "#7CF06C3D", &api::framework::Theme::pressed },
        { "Selected", "The open page and the current choice.", "#7CF06C21", &api::framework::Theme::selected },
        { "Border", "Control outlines.", "#7CF06C55", &api::framework::Theme::border },
        { "Divider", "Rules and separators.", "#7CF06C2E", &api::framework::Theme::divider },
        { "Text", "Text.", "#7CF06CFF", &api::framework::Theme::text },
        { "TextMuted", "Help, units and secondary text.", "#7CF06CBF", &api::framework::Theme::textMuted },
        { "TextDisabled", "Text of things that cannot be used now.", "#7CF06C73", &api::framework::Theme::textDisabled },
        { "Accent", "Solid fills of chosen things, links and the cursor.", "#7CF06CFF", &api::framework::Theme::accent },
        { "OnAccent", "Text and marks on an Accent fill.", "#051007FF", &api::framework::Theme::onAccent },
        { "Focus", "The control the thumbstick adjusts.", "#7CF06CD1", &api::framework::Theme::focus },
        { "Pending", "A change that is not saved yet.", "#F2B84BFF", &api::framework::Theme::pending },
        { "Invalid", "A refused value and errors.", "#FF6E5EFF", &api::framework::Theme::invalid },
        { "Success", "A finished action.", "#7CF06CFF", &api::framework::Theme::success },
        { "Scrim", "Behind a dialog.", "#051007CC", &api::framework::Theme::scrim },
    } };

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
        { "Rounding", "Corner radius of controls and windows.", 0.0f, 0.0f, 24.0f, &api::framework::Theme::rounding },
        { "BorderWidth", "Outline width.", 1.0f, 0.0f, 4.0f, &api::framework::Theme::borderWidth },
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

    [[nodiscard]] inline api::framework::Theme robco() noexcept
    {
        api::framework::Theme theme{};
        theme.revision = 1;
        std::memcpy(theme.name, kName.data(), kName.size());
        for (const auto& entry : kColors) (void)parseColor(entry.value, theme.*entry.member);
        for (const auto& entry : kMetrics) theme.*entry.member = entry.value;
        return theme;
    }
}
