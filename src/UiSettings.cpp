#include "PCH.h"

#include "Logger.h"
#include "UiSettings.h"

#include <RPS/Settings/Store.h>
#include <RPS_UI_Framework/ThemeDefaults.h>

#include <ShlObj.h>

#include <condition_variable>
#include <map>

namespace rpsui::settings
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr std::string_view kDisplay = "Display";
        constexpr std::string_view kControls = "Controls";

        struct OptionPath
        {
            std::string_view table;
            std::string_view key;
        };
        // Every option of RPS_UI.toml, the only ones a preview may set.
        constexpr std::array kOptions{
            OptionPath{ kDisplay, "Theme" },
            OptionPath{ kDisplay, "Sharpness" },
            OptionPath{ kControls, "HapticStrength" },
            OptionPath{ kControls, "GripMoveSeconds" },
        };

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

        // Preview values by "table.key", as RPS::Settings::settingPath names them.
        using Previews = std::map<std::string, std::string, std::less<>>;

        struct State
        {
            std::atomic<std::shared_ptr<const Snapshot>> current{ std::make_shared<const Snapshot>() };
            // Written once by start() on the load thread, read-only afterwards.
            std::filesystem::path directory;
            // Load thread once, then only the watcher.
            std::unique_ptr<RPS::Settings::Store> store;
            std::string themeName;
            std::filesystem::path themeFile;
            std::uint64_t themeRevision{ 0 };
            std::jthread watcher;
            // Guarded by requestMutex: written on the game thread, taken by the watcher,
            // which wake rouses.
            std::mutex requestMutex;
            std::condition_variable_any wake;
            Previews previews;
            bool previewsChanged{ false };
            bool readFileNow{ false };
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

        [[nodiscard]] bool knownOption(std::string_view table, std::string_view key) noexcept
        {
            return std::ranges::any_of(kOptions, [&](const OptionPath& option) { return option.table == table && option.key == key; });
        }

        // An option's text: its preview, else the file's value; null when neither has one.
        [[nodiscard]] const std::string* valueText(const RPS::Settings::Store& store, const Previews& previews,
            std::string_view table, std::string_view key)
        {
            if (const auto preview = previews.find(RPS::Settings::settingPath(table, key)); preview != previews.end())
                return &preview->second;
            const auto* setting = store.find(table, key);
            return setting ? &setting->value : nullptr;
        }

        [[nodiscard]] float floatValue(const RPS::Settings::Store& store, const Previews& previews, std::string_view table,
            std::string_view key, float fallback, float minimum, float maximum)
        {
            const auto* text = valueText(store, previews, table, key);
            if (!text) return fallback;
            float value = fallback;
            try {
                value = std::stof(*text);
            } catch (...) {
                return fallback;
            }
            if (!std::isfinite(value)) return fallback;
            const float clamped = std::clamp(value, minimum, maximum);
            if (clamped != value) log::warn("RPS UI setting {}.{} = {} is outside {} to {}; using {}", table, key, value, minimum, maximum, clamped);
            return clamped;
        }

        // Publishes a new snapshot of the store's last values with the previews over them,
        // reading the theme when its name changed or reloadTheme is set. Watcher (or load
        // thread at start) only.
        void publish(std::string_view reason, bool reloadTheme)
        {
            auto& value = state();
            Previews previews;
            {
                std::scoped_lock lock(value.requestMutex);
                previews = value.previews;
            }
            const auto& store = *value.store;
            const auto previous = value.current.load(std::memory_order_acquire);
            auto next = std::make_shared<Snapshot>();
            next->sharpness = floatValue(store, previews, kDisplay, "Sharpness", next->sharpness, kMinimumSharpness, kMaximumSharpness);
            next->hapticStrength = floatValue(store, previews, kControls, "HapticStrength", next->hapticStrength, 0.0f, 1.0f);
            next->gripMoveSeconds = floatValue(store, previews, kControls, "GripMoveSeconds", next->gripMoveSeconds,
                kMinimumGripMoveSeconds, kMaximumGripMoveSeconds);
            const auto* themeText = valueText(store, previews, kDisplay, "Theme");
            const std::string themeName = themeText ? *themeText : std::string(theme_defaults::kName);
            if (reloadTheme || !previous->theme || themeName != value.themeName) {
                next->theme = theme_store::load(value.directory, themeName, ++value.themeRevision);
                value.themeName = themeName;
                value.themeFile = next->theme->file;
            } else {
                next->theme = previous->theme;
            }
            next->revision = store.revision();
            log::info("RPS UI settings {}: theme '{}', sharpness {:.2f}, haptic strength {:.2f}, grip move {:.2f} s, previews {}",
                reason, next->theme->theme.name, next->sharpness, next->hapticStrength, next->gripMoveSeconds, previews.size());
            value.current.store(std::move(next), std::memory_order_release);
        }

        // Reads RPS_UI.toml, then publishes. Throws on a store failure.
        void load(bool startup, bool reloadTheme)
        {
            auto& value = state();
            value.store->load(startup);
            for (const auto& notice : value.store->notices()) log::info("{}", notice);
            for (const auto& message : value.store->messages()) log::warn("{}", message);
            publish(startup ? "loaded" : "reloaded", reloadTheme);
        }

        // Polls both files, RPS_UI.toml and the theme it names, and wakes at once for a
        // preview or a Save.
        void watch(std::stop_token stop, std::filesystem::path settingsFile)
        {
            auto& value = state();
            std::error_code error;
            auto settingsWritten = std::filesystem::last_write_time(settingsFile, error);
            auto themeFile = value.themeFile;
            auto themeWritten = std::filesystem::last_write_time(themeFile, error);
            while (!stop.stop_requested()) {
                bool previewsChanged = false;
                bool readFileNow = false;
                {
                    std::unique_lock lock(value.requestMutex);
                    (void)value.wake.wait_for(lock, stop, 250ms, [&] { return value.previewsChanged || value.readFileNow; });
                    previewsChanged = std::exchange(value.previewsChanged, false);
                    readFileNow = std::exchange(value.readFileNow, false);
                }
                if (stop.stop_requested()) break;
                error.clear();
                const auto settingsNow = std::filesystem::last_write_time(settingsFile, error);
                const bool settingsChanged = !error && settingsNow != settingsWritten;
                error.clear();
                const auto themeNow = std::filesystem::last_write_time(themeFile, error);
                const bool themeChanged = !error && themeNow != themeWritten;
                const bool readFiles = readFileNow || settingsChanged || themeChanged;
                if (!readFiles && !previewsChanged) continue;
                // An editor's write: let it finish before reading. A Save has finished writing.
                if (readFiles && !readFileNow) {
                    std::this_thread::sleep_for(250ms);
                    if (stop.stop_requested()) break;
                }
                try {
                    if (readFiles)
                        load(false, themeChanged);
                    else
                        publish("previewed", false);
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
                    "## Every mod's world panels follow them. Change them on RPS UI Framework's page in Mods Config\n"
                    "## FO4 VR, or edit this file. Changes apply while the game runs.\n"
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

    void setPreview(std::string_view table, std::string_view key, std::string text) noexcept
    {
        if (!knownOption(table, key)) {
            log::warn("RPS UI preview of [{}] {} ignored: RPS_UI.toml has no such option", table, key);
            return;
        }
        auto& value = state();
        try {
            std::scoped_lock lock(value.requestMutex);
            auto& entry = value.previews[RPS::Settings::settingPath(table, key)];
            if (entry == text) return;
            entry = std::move(text);
            value.previewsChanged = true;
        } catch (...) {
            log::warn("RPS UI preview of [{}] {} could not be kept", table, key);
            return;
        }
        value.wake.notify_one();
    }

    void discardPreview(std::string_view table, std::string_view key) noexcept
    {
        auto& value = state();
        try {
            std::scoped_lock lock(value.requestMutex);
            const auto found = value.previews.find(RPS::Settings::settingPath(table, key));
            if (found == value.previews.end()) return;
            value.previews.erase(found);
            value.previewsChanged = true;
        } catch (...) {
            return;
        }
        value.wake.notify_one();
    }

    void clearPreviews() noexcept
    {
        auto& value = state();
        {
            std::scoped_lock lock(value.requestMutex);
            if (value.previews.empty()) return;
            value.previews.clear();
            value.previewsChanged = true;
        }
        value.wake.notify_one();
    }

    void endPreviewsAndReload() noexcept
    {
        auto& value = state();
        {
            std::scoped_lock lock(value.requestMutex);
            value.previews.clear();
            value.readFileNow = true;
        }
        value.wake.notify_one();
    }

    std::vector<std::string> themeChoices()
    {
        std::vector<std::string> names;
        const auto& value = state();
        std::error_code error;
        const auto folder = value.directory.empty() ? std::filesystem::path() : value.directory / L"Themes";
        for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
            const auto& path = entry.path();
            if (!entry.is_regular_file(error) || _wcsicmp(path.extension().c_str(), L".toml") != 0) continue;
            const auto stem = path.stem().u8string();
            const std::string name(stem.begin(), stem.end());
            if (theme_store::validName(name)) names.push_back(name);
        }
        const auto snapshot = current();
        if (snapshot->theme) {
            std::string inUse(snapshot->theme->theme.name);
            if (std::ranges::find(names, inUse) == names.end()) names.push_back(std::move(inUse));
        }
        std::ranges::sort(names, [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });
        return names;
    }
}
