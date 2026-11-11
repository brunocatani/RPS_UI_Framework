#pragma once

#include "RockSettingControlPolicy.h"
#include "RpsMod.h"
#include <ROCK/Configuration.h>
#include <SCISSORS/Configuration.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpsui::settings_menu
{
    enum class SettingType : std::uint8_t
    {
        Boolean,
        Integer,
        Float,
        String
    };

    struct SettingRecord
    {
        std::string id;
        std::string section;
        std::string key;
        std::string label;
        std::string value;
        std::string category;
        std::string description;
        // The help as the mod wrote it, one entry per line: a title first when it has one,
        // "N = label" choices, "label: line" explanations, "False: line" for off.
        std::vector<std::string> helpLines;
        std::string defaultValue;
        bool fromRockApi = false;
        // A TOML string: shown without its quotes and written back quoted.
        bool tomlString = false;
        bool overridden = false;
        SettingType type{ SettingType::String };
        setting_control::Spec control;
        double numericValue{ 0.0 };
        bool numericValueValid{ false };
        std::optional<std::size_t> selectedOptionIndex;
        std::size_t lineIndex{ 0 };
        // One row of a ROCK Float3 setting ("[x, y, z]"): which component it
        // edits and its name (X, Y, Z or Pitch, Yaw, Roll). Writes send all three.
        std::optional<std::size_t> vectorComponent;
        std::string componentLabel;
    };

    struct SettingChangeResult
    {
        bool changed{ false };
        bool saved{ false };
        std::string message;
        SettingRecord setting{};
    };

    class IniSettingsStore
    {
    public:
        explicit IniSettingsStore(std::filesystem::path path = {}, RpsMod mod = RpsMod::Rock,
            const rock::api::configuration::Api* api = nullptr, std::filesystem::path catalogPath = {}, rock::api::OwnerToken owner = 0,
            const scissors::configuration::Api* scissorsApi = nullptr) :
            _path(std::move(path)), _catalogPath(std::move(catalogPath)), _mod(mod), _configurationApi(api), _configurationOwner(owner), _scissorsApi(scissorsApi),
            _useRockApi(api || (_path.empty() && isRockConfiguration(mod))) {}

        [[nodiscard]] bool load();
        [[nodiscard]] bool reload();
        [[nodiscard]] bool needsReload() const noexcept;

        [[nodiscard]] const std::vector<SettingRecord>& settings() const noexcept { return _settings; }
        [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }
        [[nodiscard]] const std::string& lastError() const noexcept { return _lastError; }
        [[nodiscard]] rock::api::Status lastProviderStatus() const noexcept { return _lastProviderStatus; }

        [[nodiscard]] std::optional<std::size_t> indexForId(std::string_view id) const;
        [[nodiscard]] bool isSlider(std::size_t index) const;
        [[nodiscard]] bool isAdjustable(std::size_t index) const;
        [[nodiscard]] std::size_t moveWithinCategory(std::size_t index, int direction) const;
        [[nodiscard]] std::size_t moveToAdjacentCategory(std::size_t index, int direction) const;
        [[nodiscard]] SettingChangeResult adjustByIndex(std::size_t index, int direction);
        [[nodiscard]] SettingChangeResult activateByIndex(std::size_t index);
        [[nodiscard]] SettingChangeResult setBooleanByIndex(std::size_t index, bool value);
        [[nodiscard]] SettingChangeResult setNumericByIndex(std::size_t index, double value);
        [[nodiscard]] SettingChangeResult setOptionByIndex(std::size_t index, std::size_t optionIndex);

        [[nodiscard]] static std::string trim(std::string_view value);
        [[nodiscard]] static std::string collapseWhitespace(std::string value);

        // The desktop preview's sandbox stores save to their files; every other preview store
        // stays read-only, so a preview can never change a mod's real settings.
        void allowPreviewWrites() noexcept { _previewWrites = true; }
        // Back to the mod's default, where the mod publishes one (ROCK's catalog).
        [[nodiscard]] SettingChangeResult resetByIndex(std::size_t index);
    private:
        bool _previewWrites{ false };
        struct IniLine
        {
            std::string raw;
            std::string section;
            std::string key;
            std::string value;
            bool isSetting{ false };
        };

        [[nodiscard]] std::filesystem::path resolveProductionIniPath() const;
        [[nodiscard]] static SettingType inferType(std::string_view value);
        void refreshControl(SettingRecord& setting) const;
        // RPS UI's Theme: the theme files beside RPS_UI.toml, and the current
        // value even when its file is missing (RPS UI then creates it).
        [[nodiscard]] setting_control::Spec themeChoices(std::string_view current) const;
        [[nodiscard]] bool connectRockApi();
        [[nodiscard]] bool reloadRockSnapshot();
        [[nodiscard]] bool loadScissorsSettings();
        [[nodiscard]] bool saveScissorsSetting(const SettingRecord& setting, std::string_view value);
        [[nodiscard]] bool loadCsahSettings();
        [[nodiscard]] bool saveCsahSetting(const SettingRecord& setting, std::string_view value);
        [[nodiscard]] rock::api::configuration::Group rockGroup() const noexcept;
        [[nodiscard]] static std::string cycleStringValue(const SettingRecord& setting, int direction);
        [[nodiscard]] static std::string adjustNumericValue(const SettingRecord& setting, int direction);

        [[nodiscard]] bool save();
        [[nodiscard]] SettingChangeResult setValueByIndex(std::size_t index, std::string_view value);
        [[nodiscard]] std::pair<std::size_t, std::size_t> categoryBounds(std::size_t index) const;

        std::filesystem::path _path;
        std::filesystem::path _catalogPath;
        RpsMod _mod;
        const rock::api::configuration::Api* _configurationApi = nullptr;
        rock::api::OwnerToken _configurationOwner{};
        rock::api::Status _lastProviderStatus{rock::api::Status::Ok};
        const scissors::configuration::Api* _scissorsApi = nullptr;
        bool _useRockApi = false;
        std::uint64_t _loadedRevision = 0;
        // A .toml file as RPS Framework's settings store writes it: "## === "
        // category headings, "##" help lines, single-"#" player notes, quoted
        // strings. Any other file is an INI.
        bool _toml = false;
        std::vector<IniLine> _lines;
        std::vector<SettingRecord> _settings;
        std::string _lastError;
    };
}
