#include "PCH.h"

#include "Logger.h"
#include "UiSettings.h"

#include <RPS/Settings/Store.h>

#include <ShlObj.h>

namespace rpsui::settings
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr std::string_view kDisplay = "Display";

        std::vector<RPS::Settings::Definition> definitions()
        {
            return {
                { std::string(kDisplay), "Sharpness", "Display",
                    "Texture pixels for each unit of a panel's layout, at the panel's default size.\n"
                    "A panel shown larger gets more pixels, so it stays as sharp.\n"
                    "Higher is sharper text and uses more video memory. 1.0 to 3.0.",
                    RPS::Settings::Group::Player, RPS::Settings::ValueType::Float, "1.5" },
            };
        }

        struct State
        {
            std::atomic<std::shared_ptr<const Snapshot>> current{ std::make_shared<const Snapshot>() };
            // The store is used by the load thread once, then only by the watcher.
            std::unique_ptr<RPS::Settings::Store> store;
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

        // Reads the file into a new snapshot. Throws on a store failure.
        void load(bool startup)
        {
            auto& value = state();
            value.store->load(startup);
            for (const auto& notice : value.store->notices()) log::info("{}", notice);
            for (const auto& message : value.store->messages()) log::warn("{}", message);
            auto next = std::make_shared<Snapshot>();
            next->sharpness = floatValue(*value.store, kDisplay, "Sharpness", next->sharpness, kMinimumSharpness, kMaximumSharpness);
            next->revision = value.store->revision();
            log::info("RPS UI settings {}: sharpness {:.2f}", startup ? "loaded" : "reloaded", next->sharpness);
            value.current.store(std::move(next), std::memory_order_release);
        }
    }

    void start() noexcept
    {
        auto& value = state();
        if (value.store) return;
        try {
            const auto directory = configDirectory();
            if (directory.empty()) {
                log::warn("RPS UI settings: Documents is unavailable; built-in defaults apply");
                return;
            }
            value.store = std::make_unique<RPS::Settings::Store>(RPS::Settings::Files{
                .directory = directory,
                .modName = "RPS UI",
                .playerFileName = "RPS_UI.toml",
                .playerHeader = "RPS UI Framework settings. Every mod's panels follow them.",
            }, definitions());
            load(true);
        } catch (const std::exception& error) {
            log::error("RPS UI settings could not be read: {}; built-in defaults apply", error.what());
            if (!value.store) return;
        }

        const auto path = value.store->path(RPS::Settings::Group::Player);
        value.watcher = std::jthread([path](std::stop_token stop) {
            std::error_code error;
            auto observed = std::filesystem::last_write_time(path, error);
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(250ms);
                error.clear();
                const auto written = std::filesystem::last_write_time(path, error);
                if (error || written == observed) continue;
                observed = written;
                // Let the writer finish before reading.
                std::this_thread::sleep_for(250ms);
                if (stop.stop_requested()) break;
                try {
                    load(false);
                } catch (const std::exception& failure) {
                    log::warn("RPS UI settings could not be reread: {}; the last values stay", failure.what());
                }
            }
        });
    }

    std::shared_ptr<const Snapshot> current() noexcept
    {
        return state().current.load(std::memory_order_acquire);
    }
}
