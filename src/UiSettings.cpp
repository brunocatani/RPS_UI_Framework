#include "PCH.h"

#include "Logger.h"
#include "UiSettings.h"

#include <RPS/Settings/Store.h>
#include <RPS_UI_Framework/ThemeDefaults.h>

#include <ShlObj.h>

namespace rpsui::settings
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr std::string_view kDisplay = "Display";
        constexpr std::string_view kControls = "Controls";

        std::vector<RPS::Settings::Definition> definitions()
        {
            return {
                { std::string(kDisplay), "Theme", "Display",
                    "The theme every mod's panels draw with: a file in the Themes folder, without .toml.\n"
                    "Built in: Pip-Boy (the game's green), New Vegas (amber) and Vault-Tec (blue and yellow).\n"
                    "Copy one of their files under a new name to make your own; a name with no file starts as a\n"
                    "copy of Pip-Boy.",
                    RPS::Settings::Group::Player, RPS::Settings::ValueType::String, std::string(theme_defaults::kName) },
                { std::string(kDisplay), "Sharpness", "Display",
                    "Texture pixels for each unit of a panel's layout, at the panel's default size.\n"
                    "A panel shown larger gets more pixels, so it stays as sharp.\n"
                    "Higher is sharper text and uses more video memory. 1.0 to 3.0.",
                    RPS::Settings::Group::Player, RPS::Settings::ValueType::Float, "1.5" },
                { std::string(kControls), "HapticStrength", "Controls",
                    "How strongly the controller ticks when you press something on a panel, and every other\n"
                    "pulse a mod's panel asks for. 0 turns them off, 1 is the strongest. 0.0 to 1.0.",
                    RPS::Settings::Group::Player, RPS::Settings::ValueType::Float, "0.6" },
                { std::string(kControls), "GripMoveSeconds", "Controls",
                    "Seconds to hold grip on a panel that can be moved before it follows your hand; release to\n"
                    "place it. A shorter grip press is Back. 0.2 to 1.5.",
                    RPS::Settings::Group::Player, RPS::Settings::ValueType::Float, "0.45" },
            };
        }

        struct State
        {
            std::atomic<std::shared_ptr<const Snapshot>> current{ std::make_shared<const Snapshot>() };
            // Load thread once, then only the watcher.
            std::filesystem::path directory;
            std::unique_ptr<RPS::Settings::Store> store;
            std::string themeName;
            std::filesystem::path themeFile;
            std::uint64_t themeRevision{ 0 };
            std::jthread watcher;
        };

        [[nodiscard]] State& state() noexcept
        {
            static auto* value = new State();
            return *value;
        }

        [[nodiscard]] std::filesystem::path configDirectory()
        {
            PWSTR documents{};
            if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents))) return {};
            const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(documents, &CoTaskMemFree);
            return std::filesystem::path(documents) / L"My Games" / L"Fallout4VR" / L"Mods_Config" / L"RPS_UI_Framework";
        }

        [[nodiscard]] float floatValue(const RPS::Settings::Store& store, std::string_view table, std::string_view key,
            float fallback, float minimum, float maximum)
        {
            const auto* setting = store.find(table, key);
            if (!setting) return fallback;
            float value = fallback;
            try {
                value = std::stof(setting->value);
            } catch (...) {
                return fallback;
            }
            if (!std::isfinite(value)) return fallback;
            const float clamped = std::clamp(value, minimum, maximum);
            if (clamped != value) log::warn("RPS UI setting {}.{} = {} is outside {} to {}; using {}", table, key, value, minimum, maximum, clamped);
            return clamped;
        }

        // Reads RPS_UI.toml, and the theme when its name changed or reloadTheme
        // is set, into a new snapshot. Throws on a store failure.
        void load(bool startup, bool reloadTheme)
        {
            auto& value = state();
            value.store->load(startup);
            for (const auto& notice : value.store->notices()) log::info("{}", notice);
            for (const auto& message : value.store->messages()) log::warn("{}", message);
            const auto previous = value.current.load(std::memory_order_acquire);
            auto next = std::make_shared<Snapshot>();
            next->sharpness = floatValue(*value.store, kDisplay, "Sharpness", next->sharpness, kMinimumSharpness, kMaximumSharpness);
            next->hapticStrength = floatValue(*value.store, kControls, "HapticStrength", next->hapticStrength, 0.0f, 1.0f);
            next->gripMoveSeconds = floatValue(*value.store, kControls, "GripMoveSeconds", next->gripMoveSeconds,
                kMinimumGripMoveSeconds, kMaximumGripMoveSeconds);
            const auto* themeSetting = value.store->find(kDisplay, "Theme");
            const std::string themeName = themeSetting ? themeSetting->value : std::string(theme_defaults::kName);
            if (reloadTheme || !previous->theme || themeName != value.themeName) {
                next->theme = theme_store::load(value.directory, themeName, ++value.themeRevision);
                value.themeName = themeName;
                value.themeFile = next->theme->file;
            } else {
                next->theme = previous->theme;
            }
            next->revision = value.store->revision();
            log::info("RPS UI settings {}: theme '{}', sharpness {:.2f}, haptic strength {:.2f}, grip move {:.2f} s",
                startup ? "loaded" : "reloaded", next->theme->theme.name, next->sharpness, next->hapticStrength, next->gripMoveSeconds);
            value.current.store(std::move(next), std::memory_order_release);
        }

        // Polls both files: RPS_UI.toml and the theme it names.
        void watch(std::stop_token stop, std::filesystem::path settingsFile)
        {
            auto& value = state();
            std::error_code error;
            auto settingsWritten = std::filesystem::last_write_time(settingsFile, error);
            auto themeFile = value.themeFile;
            auto themeWritten = std::filesystem::last_write_time(themeFile, error);
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(250ms);
                error.clear();
                const auto settingsNow = std::filesystem::last_write_time(settingsFile, error);
                const bool settingsChanged = !error && settingsNow != settingsWritten;
                error.clear();
                const auto themeNow = std::filesystem::last_write_time(themeFile, error);
                const bool themeChanged = !error && themeNow != themeWritten;
                if (!settingsChanged && !themeChanged) continue;
                // Let the writer finish before reading.
                std::this_thread::sleep_for(250ms);
                if (stop.stop_requested()) break;
                try {
                    load(false, themeChanged);
                } catch (const std::exception& failure) {
                    log::warn("RPS UI settings could not be reread: {}; the last values stay", failure.what());
                }
                // The theme file may have changed with the name; take both times after the read.
                settingsWritten = std::filesystem::last_write_time(settingsFile, error);
                themeFile = value.themeFile;
                themeWritten = std::filesystem::last_write_time(themeFile, error);
            }
        }
    }

    void start() noexcept
    {
        auto& value = state();
        if (value.store) return;
        try {
            value.directory = configDirectory();
            if (value.directory.empty()) {
                log::warn("RPS UI settings: Documents is unavailable; built-in defaults apply");
                auto next = std::make_shared<Snapshot>();
                next->theme = theme_store::builtIn({}, ++value.themeRevision);
                value.current.store(std::move(next), std::memory_order_release);
                return;
            }
            value.store = std::make_unique<RPS::Settings::Store>(RPS::Settings::Files{
                .directory = value.directory,
                .modName = "RPS UI",
                .playerFileName = "RPS_UI.toml",
                .playerHeader =
                    "## RPS_UI.toml - RPS UI Framework options\n"
                    "## Every mod's world panels follow them. Change them in the RobCo PALM wheel menu, or edit this\n"
                    "## file. Changes apply while the game runs.\n"
                    "## Each option has its help above it. Your own notes are kept when they start with a single #.\n",
            }, definitions());
            load(true, true);
        } catch (const std::exception& error) {
            log::error("RPS UI settings could not be read: {}; built-in defaults apply", error.what());
            if (!value.current.load()->theme) {
                auto next = std::make_shared<Snapshot>();
                next->theme = theme_store::builtIn(value.directory, ++value.themeRevision);
                value.current.store(std::move(next), std::memory_order_release);
            }
            if (!value.store) return;
        }
        value.watcher = std::jthread(watch, value.store->path(RPS::Settings::Group::Player));
    }

    std::shared_ptr<const Snapshot> current() noexcept
    {
        return state().current.load(std::memory_order_acquire);
    }
}
