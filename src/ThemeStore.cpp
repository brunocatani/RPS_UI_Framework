#include "PCH.h"

#include "Logger.h"
#include "ThemeStore.h"

#include <RPS/Settings/Store.h>
#include <RPS_UI_Framework/ThemeDefaults.h>

#include <fstream>

namespace rpsui::theme_store
{
    namespace
    {
        using api::framework::Theme;
        using api::framework::ThemeColor;
        namespace defaults = theme_defaults;

        constexpr std::string_view kColors = "Colors";
        constexpr std::string_view kFonts = "Fonts";
        constexpr std::string_view kMetrics = "Metrics";

        [[nodiscard]] std::string canonicalFloat(float value)
        {
            auto text = std::format("{}", value);
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";
            return text;
        }


        // A theme file of a built-in name starts from that theme; any other name starts as a
        // copy of the default theme.
        [[nodiscard]] const defaults::BuiltInTheme& basisOf(std::string_view name) noexcept
        {
            const auto* builtIn = defaults::findBuiltIn(name);
            return builtIn ? *builtIn : defaults::kBuiltIns[0];
        }

        [[nodiscard]] std::vector<RPS::Settings::Definition> definitions(const defaults::BuiltInTheme& basis)
        {
            using RPS::Settings::Group;
            using RPS::Settings::ValueType;
            std::vector<RPS::Settings::Definition> list;
            for (std::size_t i = 0; i < defaults::kColors.size(); ++i) {
                const auto& entry = defaults::kColors[i];
                list.push_back({ std::string(kColors), std::string(entry.key), "Colors",
                    std::format("{}\n\"#RRGGBB\" or \"#RRGGBBAA\"; AA is opacity.", entry.help), Group::Player,
                    ValueType::String, std::string(basis.colors[i]) });
            }

            for (const auto& entry : defaults::kFonts) {
                list.push_back({ std::string(kFonts), std::string(entry.key), "Fonts",
                    std::format("{}\nA .ttf or .otf file in the Fonts folder beside Themes, or one Windows has.", entry.help),
                    Group::Player, ValueType::String, std::string(entry.file) });
            }
            for (const auto& entry : defaults::kMetrics) {
                list.push_back({ std::string(kMetrics), std::string(entry.key), "Sizes",
                    std::format("{} {} to {}.", entry.help, entry.minimum, entry.maximum), Group::Player, ValueType::Float,
                    canonicalFloat(entry.value) });
            }
            return list;
        }

        [[nodiscard]] std::string header(std::string_view name)
        {
            return std::format(
                "## {}.toml - an RPS UI theme\n"
                "## Every mod's world panels draw with the theme RPS_UI.toml names (Display, Theme).\n"
                "## To make your own, copy this file under a new name, edit it and set Theme to that name.\n"
                "## A theme file can be shared: put it in Themes and any fonts it names in Fonts.\n"
                "## Changes apply while the game runs.\n",
                name);
        }

        [[nodiscard]] bool validFontFile(std::string_view file) noexcept
        {
            if (file.empty() || file.size() > 96 || file.find_first_of("\\/:") != std::string_view::npos ||
                file.find("..") != std::string_view::npos)
                return false;
            const auto dot = file.rfind('.');
            if (dot == std::string_view::npos) return false;
            std::string extension(file.substr(dot));
            std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension == ".ttf" || extension == ".otf" || extension == ".ttc";
        }

        [[nodiscard]] FaceBytes readFace(const std::filesystem::path& configDirectory, std::string_view file)
        {
            if (!validFontFile(file)) {
                log::warn("RPS UI theme font '{}' is not a font file name; the Interface face is used", file);
                return {};
            }
            std::vector<std::filesystem::path> candidates{ configDirectory / L"Fonts" / std::filesystem::path(std::string(file)) };
            wchar_t windows[MAX_PATH]{};
            if (GetWindowsDirectoryW(windows, MAX_PATH)) candidates.push_back(std::filesystem::path(windows) / L"Fonts" / std::string(file));
            for (const auto& path : candidates) {
                std::ifstream input(path, std::ios::binary | std::ios::ate);
                if (!input) continue;
                const auto size = input.tellg();
                if (size <= 0 || size > 32 * 1024 * 1024) {
                    log::warn("RPS UI theme font '{}' has an unusable size", path.string());
                    return {};
                }
                auto bytes = std::make_shared<std::vector<char>>(static_cast<std::size_t>(size));
                input.seekg(0);
                if (!input.read(bytes->data(), static_cast<std::streamsize>(bytes->size()))) return {};
                return bytes;
            }
            log::warn("RPS UI theme font '{}' was not found in the Fonts folder or Windows' fonts", file);
            return {};
        }

        void readFaces(ThemeData& data, const std::filesystem::path& configDirectory,
            const std::array<std::string, api::framework::kFontRoleCount>& files)
        {
            for (std::size_t role = 0; role < files.size(); ++role) {
                // Roles often share a file; read it once.
                for (std::size_t earlier = 0; earlier < role && !data.faces[role]; ++earlier)
                    if (files[earlier] == files[role]) data.faces[role] = data.faces[earlier];
                if (!data.faces[role]) data.faces[role] = readFace(configDirectory, files[role]);
            }
        }

        // WCAG relative luminance and contrast, over a backdrop for translucent colours.
        [[nodiscard]] float channel(float value) noexcept
        {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }
        [[nodiscard]] ThemeColor over(const ThemeColor& top, const ThemeColor& bottom) noexcept
        {
            const float a = top.a;
            return { top.r * a + bottom.r * (1 - a), top.g * a + bottom.g * (1 - a), top.b * a + bottom.b * (1 - a), 1.0f };
        }
        [[nodiscard]] float luminance(const ThemeColor& c) noexcept
        {
            return 0.2126f * channel(c.r) + 0.7152f * channel(c.g) + 0.0722f * channel(c.b);
        }
        [[nodiscard]] float contrast(const ThemeColor& a, const ThemeColor& b) noexcept
        {
            const float la = luminance(a), lb = luminance(b);
            return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
        }

        // The worst case of a translucent body is the brightest and the darkest world behind it.
        void reportContrast(const Theme& theme, std::string_view name)
        {
            const ThemeColor black{ 0, 0, 0, 1 }, white{ 1, 1, 1, 1 };
            struct Pair { std::string_view label; const ThemeColor& text; float required; };
            for (const auto& backdrop : { black, white }) {
                const auto body = over(theme.background, backdrop);
                for (const Pair pair : { Pair{ "Text", theme.text, 4.5f }, Pair{ "TextMuted", theme.textMuted, 3.0f },
                         Pair{ "Pending", theme.pending, 3.0f }, Pair{ "Invalid", theme.invalid, 3.0f } }) {
                    const float ratio = contrast(over(pair.text, body), body);
                    if (ratio < pair.required) {
                        log::warn("RPS UI theme '{}': {} on Background has contrast {:.1f}:1, under {:.1f}:1{}", name, pair.label,
                            ratio, pair.required, theme.background.a < 1.0f ? " with a bright world behind a see-through panel" : "");
                    }
                }
                if (theme.background.a >= 1.0f) break;
            }
            const auto fill = over(theme.accent, over(theme.background, black));
            if (const float ratio = contrast(over(theme.onAccent, fill), fill); ratio < 4.5f)
                log::warn("RPS UI theme '{}': OnAccent on Accent has contrast {:.1f}:1, under 4.5:1", name, ratio);
        }

        [[nodiscard]] std::shared_ptr<const ThemeData> fromStore(const RPS::Settings::Store& store,
            const std::filesystem::path& configDirectory, std::string_view name, std::uint64_t revision)
        {

            auto data = std::make_shared<ThemeData>();
            const auto& basis = basisOf(name);
            data->theme = defaults::builtIn(basis);
            data->theme.revision = revision;
            std::memset(data->theme.name, 0, sizeof(data->theme.name));

            std::memcpy(data->theme.name, name.data(), std::min(name.size(), sizeof(data->theme.name) - 1));
            data->file = store.path(RPS::Settings::Group::Player);

            for (std::size_t i = 0; i < defaults::kColors.size(); ++i) {
                const auto& entry = defaults::kColors[i];
                const auto* setting = store.find(kColors, entry.key);
                if (setting && !defaults::parseColor(setting->value, data->theme.*entry.member))
                    log::warn("RPS UI theme '{}': Colors.{} = \"{}\" is not \"#RRGGBB\" or \"#RRGGBBAA\"; using {}", name,
                        entry.key, setting->value, basis.colors[i]);
            }

            for (const auto& entry : defaults::kMetrics) {
                const auto* setting = store.find(kMetrics, entry.key);
                if (!setting) continue;
                float value = entry.value;
                try {
                    value = std::stof(setting->value);
                } catch (...) {
                }
                if (!std::isfinite(value)) value = entry.value;
                const float clamped = std::clamp(value, entry.minimum, entry.maximum);
                if (clamped != value)
                    log::warn("RPS UI theme '{}': Metrics.{} = {} is outside {} to {}; using {}", name, entry.key, value,
                        entry.minimum, entry.maximum, clamped);
                data->theme.*entry.member = clamped;
            }
            std::array<std::string, api::framework::kFontRoleCount> files{};
            for (std::size_t role = 0; role < files.size(); ++role) {
                const auto* setting = store.find(kFonts, defaults::kFonts[role].key);
                files[role] = setting ? setting->value : std::string(defaults::kFonts[role].file);
            }
            readFaces(*data, configDirectory, files);
            reportContrast(data->theme, name);
            return data;
        }

        [[nodiscard]] std::unique_ptr<RPS::Settings::Store> openStore(const std::filesystem::path& configDirectory, std::string_view name)
        {
            return std::make_unique<RPS::Settings::Store>(RPS::Settings::Files{
                .directory = configDirectory / L"Themes",
                .modName = std::format("RPS UI theme {}", name),
                .playerFileName = std::format("{}.toml", name),
                .playerHeader = header(name),
            }, definitions(basisOf(name)));
        }
    }

    bool validName(std::string_view name) noexcept
    {
        if (name.empty() || name.size() > 48 || name.front() == '.' || name.front() == ' ') return false;
        return std::ranges::all_of(name, [](unsigned char c) {
            return std::isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.';
        });
    }

    std::shared_ptr<const ThemeData> builtIn(const std::filesystem::path& configDirectory, std::uint64_t revision)
    {
        auto data = std::make_shared<ThemeData>();
        data->theme = defaults::defaultTheme();
        data->theme.revision = revision;
        std::array<std::string, api::framework::kFontRoleCount> files{};
        for (std::size_t role = 0; role < files.size(); ++role) files[role] = std::string(defaults::kFonts[role].file);
        readFaces(*data, configDirectory, files);
        return data;
    }

    std::shared_ptr<const ThemeData> load(const std::filesystem::path& configDirectory, std::string_view name,
        std::uint64_t revision)
    {
        try {

            if (!validName(name)) {
                log::warn("RPS UI theme name '{}' may hold only letters, digits, spaces, '-', '_' and '.'; using {}",
                    name, defaults::kName);
                name = defaults::kName;
            }
            // Every built-in theme is always there as a file, so a player can pick one or copy it.
            std::unique_ptr<RPS::Settings::Store> store;
            for (const auto& builtIn : defaults::kBuiltIns) {
                auto file = openStore(configDirectory, builtIn.name);
                file->load(true);
                if (builtIn.name == name) store = std::move(file);
            }
            if (!store) {
                store = openStore(configDirectory, name);
                store->load(true);
            }

            for (const auto& message : store->messages()) log::warn("{}", message);
            auto data = fromStore(*store, configDirectory, name, revision);
            log::info("RPS UI theme '{}' loaded from {}", name, data->file.string());
            return data;
        } catch (const std::exception& error) {
            log::error("RPS UI theme '{}' could not be read: {}; the built-in {} theme applies", name, error.what(), defaults::kName);
            return builtIn(configDirectory, revision);
        }
    }
}
