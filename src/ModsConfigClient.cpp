#include "PCH.h"

#include "ModsConfigClient.h"

#include "Logger.h"
#include "ModsConfigFO4Api.h"
#include "UiSettings.h"

#include <variant>

namespace rpsui::mods_config_client
{
    namespace
    {
        using ::mods_config::api::Change;
        using ::mods_config::api::MenuEventType;
        using ::mods_config::api::ModsConfigFO4Client;
        using ::mods_config::api::Option;
        using ::mods_config::api::Saved;
        using ::mods_config::api::Value;

        // The modName of RPS UI's config spec, and the id of its Theme choice.
        constexpr const char* kModName = "RPS_UI_Framework";
        constexpr const char* kThemeId = "theme";

        // A value of the menu as the text RPS_UI.toml holds; UiSettings reads and clamps it.
        [[nodiscard]] std::string valueText(const Value& value)
        {
            return std::visit(
                [](const auto& held) -> std::string {
                    using Held = std::decay_t<decltype(held)>;
                    if constexpr (std::is_same_v<Held, bool>)
                        return held ? "true" : "false";
                    else if constexpr (std::is_same_v<Held, std::string>)
                        return held;
                    else
                        return std::format("{}", held);
                },
                value);
        }

        // The Theme choice's options: every theme file, and the theme in use even without one.
        std::vector<Option> themeOptions()
        {
            std::vector<Option> options;
            try {
                for (auto& name : settings::themeChoices()) options.push_back({ .value = std::move(name), .text = {} });
            } catch (...) {
                log::warn("RPS UI theme list for Mods Config could not be built");
            }
            return options;
        }

        // A live setting's preview, or its Discard back to the file's value. Its TOML path is
        // its table, then its key.
        void onChange(const Change& change)
        {
            if (change.path.size() != 2) return;
            if (change.discard)
                settings::discardPreview(change.path[0], change.path[1]);
            else
                settings::setPreview(change.path[0], change.path[1], valueText(change.value));
        }

        // A Save wrote RPS_UI.toml: the previews end and the file is read at once, so the
        // saved values follow the previews without the watcher's poll delay.
        void onSaved(const Saved&)
        {
            settings::endPreviewsAndReload();
        }

        // The menu closing ends every preview, should a Discard not have come for one.
        void onMenuEvent(MenuEventType event, std::string_view)
        {
            if (event == MenuEventType::MenuClosed) settings::clearPreviews();
        }

        // Mods Config's F4SE message after a Save, sent to the spec's f4sePluginRequirements.
        // onSaved already tells of a Save; without a listener F4SE logs a failed dispatch.
        void onModsConfigMessage(F4SE::MessagingInterface::Message*) {}
    }

    void connect() noexcept
    {
        try {
            auto& client = ModsConfigFO4Client::get();
            client.addOptions(kThemeId, &themeOptions);
            client.subscribeToChanges(&onChange);
            client.subscribeToSaves(&onSaved);
            client.subscribeToMenuEvents(&onMenuEvent);
            if (const auto* messaging = F4SE::GetMessagingInterface())
                (void)messaging->RegisterListener(&onModsConfigMessage, ::mods_config::api::MODS_CONFIG_FO4_F4SE_NAME);
            if (const int error = client.connect(kModName); error != 0)
                log::info("Mods Config FO4 VR not connected ({}): RPS UI settings come from RPS_UI.toml alone", error);
            else
                log::info("Connected to Mods Config FO4 VR (API {})", client.apiVersion());
        } catch (...) {
            log::warn("Connecting to Mods Config FO4 VR failed");
        }
    }
}
