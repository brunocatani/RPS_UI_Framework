#include "IniSettingsStore.h"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace rpsui::settings_menu {
namespace {
using Json = nlohmann::json;

std::string scalar(const Json& value)
{
    if (value.is_string()) {
        auto text = value.get<std::string>();
        if (text.empty() || text.size() > 128 ||
            text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-+") != std::string::npos)
            throw std::runtime_error("Invalid CSAH setting value");
        return text;
    }
    if (value.is_boolean() || value.is_number()) return value.dump();
    throw std::runtime_error("Setting value must be a scalar");
}

std::string identifier(const Json& value)
{
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > 96 ||
        text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        throw std::runtime_error("Invalid CSAH setting identifier");
    return text;
}

std::string label(const Json& value)
{
    auto text = value.get<std::string>();
    if (text.empty() || text.size() > 2048 || text.find_first_of("\r\n\t") != std::string::npos || text.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid CSAH setting text");
    return text;
}

std::filesystem::path productionCatalog()
{
    std::array<wchar_t, 32768> executable{};
    const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!size || size >= executable.size()) return {};
    return std::filesystem::path(executable.data()).parent_path() / "Data/F4SE/Plugins/CSAH/PALMSettings.json";
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("CSAH.toml is unavailable; CSAH must start once to write its settings");
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

// Every "key = value" line of CSAH.toml by "table.key": the line's position and
// its value. CSAH writes only simple keys and scalar values.
struct TomlEntry
{
    std::size_t begin{}, end{}; // the value's bytes in the file
    std::string value;
};
std::map<std::string, TomlEntry, std::less<>> readEntries(const std::string& text)
{
    std::map<std::string, TomlEntry, std::less<>> entries;
    std::string table;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        auto lineEnd = end;
        if (lineEnd > start && text[lineEnd - 1] == '\r') --lineEnd;
        const auto first = text.find_first_not_of(" \t", start);
        if (first < lineEnd && text[first] != '#') {
            if (text[first] == '[') {
                const auto close = text.find(']', first);
                if (close < lineEnd) table = IniSettingsStore::trim(std::string_view(text).substr(first + 1, close - first - 1));
            } else if (const auto equals = text.find('=', first); equals < lineEnd) {
                const auto key = IniSettingsStore::trim(std::string_view(text).substr(first, equals - first));
                auto valueBegin = text.find_first_not_of(" \t", equals + 1);
                if (valueBegin > lineEnd) valueBegin = lineEnd;
                auto valueEnd = lineEnd;
                while (valueEnd > valueBegin && (text[valueEnd - 1] == ' ' || text[valueEnd - 1] == '\t')) --valueEnd;
                entries.insert_or_assign(table + "." + key, TomlEntry{ valueBegin, valueEnd, text.substr(valueBegin, valueEnd - valueBegin) });
            }
        }
        start = end + 1;
    }
    return entries;
}
}

bool IniSettingsStore::loadCsahSettings()
{
    try {
        auto catalog = _catalogPath;
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        if (catalog.empty()) catalog = productionCatalog();
#endif
        std::error_code error;
        if (catalog.empty() || std::filesystem::file_size(catalog, error) > 1024 * 1024 || error)
            throw std::runtime_error("CSAH's PALM settings catalog is missing or unreadable; install the matching CSAH build");
        std::ifstream input(catalog);
        const auto menu = Json::parse(input);
        if (menu.at("schemaVersion") != 2 || menu.at("id") != "csah")
            throw std::runtime_error("Unsupported CSAH settings catalog; install the matching CSAH and RPS UI Framework builds");
        const auto entries = readEntries(readFile(_path));
        std::vector<SettingRecord> settings;
        std::set<std::string> ids, bindings;
        for (const auto& tab : menu.at("tabs")) {
            for (const auto& page : tab.at("pages")) {
                const auto category = label(tab.at("label")) + " / " + label(page.at("label"));
                for (const auto& group : page.at("groups")) {
                    for (const auto& control : group.at("controls")) {
                        if (settings.size() >= 512) throw std::runtime_error("Too many CSAH settings");
                        SettingRecord setting;
                        const auto& binding = control.at("binding");
                        if (binding.at("type") != "toml" || binding.at("root") != "documents" ||
                            binding.at("path") != "Mods_Config/CSAH/CSAH.toml")
                            throw std::runtime_error("CSAH settings must belong to CSAH.toml");
                        setting.id = identifier(control.at("id"));
                        setting.section = identifier(binding.at("table"));
                        setting.key = identifier(binding.at("key"));
                        if (!ids.insert(setting.id).second || !bindings.insert(setting.section + "." + setting.key).second)
                            throw std::runtime_error("Duplicate CSAH setting or binding");
                        const auto valueType = binding.at("value").get<std::string>();
                        setting.label = label(control.at("label"));
                        setting.category = category;
                        if (control.contains("help")) setting.description = label(control.at("help"));
                        if (group.contains("description")) {
                            if (!setting.description.empty()) setting.description += " ";
                            setting.description += label(group.at("description"));
                        }
                        setting.defaultValue = scalar(control.at("default"));
                        const auto type = control.at("type").get<std::string>();
                        if (type == "toggle") {
                            if (valueType != "boolean" || !control.at("default").is_boolean()) throw std::runtime_error("Invalid toggle");
                            setting.type = SettingType::Boolean;
                            setting.control.kind = setting_control::Kind::Checkbox;
                        } else if (type == "choice") {
                            if (valueType != "integer") throw std::runtime_error("CSAH choices must be integers");
                            setting.type = SettingType::Integer;
                            setting.control.kind = setting_control::Kind::Dropdown;
                            const auto& choices = control.at("choices");
                            if (choices.empty() || choices.size() > 64) throw std::runtime_error("Invalid CSAH choices");
                            std::set<std::string> values;
                            for (const auto& choice : choices) {
                                auto value = scalar(choice.at("value"));
                                if (!values.insert(value).second) throw std::runtime_error("Duplicate CSAH choice");
                                setting.control.options.push_back({std::move(value), label(choice.at("label"))});
                            }
                            if (!values.contains(setting.defaultValue)) throw std::runtime_error("Invalid CSAH choice default");
                        } else if (type == "slider" || type == "number") {
                            if (valueType != "integer" && valueType != "float") throw std::runtime_error("CSAH numbers must be integer or float");
                            setting.type = valueType == "integer" ? SettingType::Integer : SettingType::Float;
                            auto& spec = setting.control;
                            spec.kind = setting_control::Kind::Numeric;
                            spec.bounded = true;
                            spec.minimum = control.at("min").get<double>();
                            spec.maximum = control.at("max").get<double>();
                            spec.step = control.at("step").get<double>();
                            const auto fallback = control.at("default").get<double>();
                            if (!std::isfinite(spec.minimum) || !std::isfinite(spec.maximum) || !std::isfinite(spec.step) ||
                                !std::isfinite(fallback) || spec.minimum >= spec.maximum || spec.step <= 0 ||
                                fallback < spec.minimum || fallback > spec.maximum)
                                throw std::runtime_error("Invalid CSAH numeric range or default");
                        } else throw std::runtime_error("Unsupported CSAH control type");

                        // CSAH writes every option at startup; a missing one shows its
                        // default and cannot be saved until CSAH has started once.
                        const auto found = entries.find(setting.section + "." + setting.key);
                        setting.value = found != entries.end() ? found->second.value : setting.defaultValue;
                        if (setting.type == SettingType::Boolean && setting.value != "true" && setting.value != "false")
                            setting.value = setting.defaultValue;
                        refreshControl(setting);
                        settings.push_back(std::move(setting));
                    }
                }
            }
        }
        if (settings.empty()) throw std::runtime_error("CSAH settings catalog is empty");
        _settings = std::move(settings);
        return true;
    } catch (const std::exception& error) {
        _lastError = std::string("CSAH settings: ") + error.what();
        return false;
    }
}

bool IniSettingsStore::saveCsahSetting(const SettingRecord& setting, std::string_view value)
{
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
    try {
        // Change only the selected value in the file as it is now, never an
        // earlier snapshot, so edits from CSAH or another editor survive.
        auto text = readFile(_path);
        const auto entries = readEntries(text);
        const auto found = entries.find(setting.section + "." + setting.key);
        if (found == entries.end()) {
            _lastError = std::format("CSAH.toml has no [{}] {}; start the game once so CSAH writes every option", setting.section, setting.key);
            return false;
        }
        std::string stored(value);
        // TOML floats carry a decimal point or an exponent.
        if (setting.type == SettingType::Float && stored.find_first_of(".eE") == std::string::npos) stored += ".0";
        text.replace(found->second.begin, found->second.end - found->second.begin, stored);
        const auto temporary = std::filesystem::path(_path.native() + L".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << text;
            if (!output.flush()) throw std::runtime_error("CSAH.toml temporary write failed");
        }
        if (!MoveFileExW(temporary.c_str(), _path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            _lastError = "CSAH setting write failed: Windows error " + std::to_string(GetLastError());
            return false;
        }
    } catch (const std::exception& error) {
        _lastError = std::string("CSAH setting write failed: ") + error.what();
        return false;
    }
#endif
    return true;
}
}
