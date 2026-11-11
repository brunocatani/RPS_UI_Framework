#include "IniSettingsStore.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace rpsui::settings_menu;
using Json = nlohmann::json;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}
Json fixture() {
    return Json::parse(R"({"schemaVersion":2,"id":"csah","tabs":[{"label":"Features","pages":[
      {"label":"Lighting","groups":[{"controls":[
        {"id":"enabled","type":"toggle","label":"Visual suite","default":false,
         "binding":{"type":"toml","root":"documents","path":"Mods_Config/CSAH/CSAH.toml","table":"CommunityShaders","key":"Enabled","value":"boolean"}},
        {"id":"strength","type":"slider","label":"Strength","default":0.5,"min":0,"max":2,"step":0.05,
         "binding":{"type":"toml","root":"documents","path":"Mods_Config/CSAH/CSAH.toml","table":"Lighting","key":"Strength","value":"float"}},
        {"id":"quality","type":"choice","label":"Quality","help":"Restart after changing quality.","default":2,
         "choices":[{"label":"Low","value":0},{"label":"High","value":2}],
         "binding":{"type":"toml","root":"documents","path":"Mods_Config/CSAH/CSAH.toml","table":"Lighting","key":"Quality","value":"integer"}}
      ]}]}]}]})");
}
}

int main(int argc, char** argv) {
    // Fixtures stay with the test build, never in the system temporary directory.
    const std::filesystem::path directory(SETTINGS_MENU_CSAH_TEST_DIR);
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto toml = directory / "CSAH.toml";
    const auto catalog = directory / "PALMSettings.json";
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } cleanup{directory};
    try {
        write(toml, "## === 01. Suite ===\n[CommunityShaders]\n## help\n# keep note\nEnabled = true\n[Lighting]\nStrength = 1.0\n[Unrelated]\nOther = 'keep'\n");
        const auto original = read(toml);
        auto menu = fixture();
        const auto writeCatalog = [&] { std::ofstream file(catalog); file << menu; };
        writeCatalog();
        IniSettingsStore store(toml, RpsMod::Csah, nullptr, catalog);
        require(store.load(), store.lastError().c_str());
        require(store.settings().size() == 3, "catalog omitted missing keys or exposed unrelated keys");
        require(store.settings()[0].type == SettingType::Boolean && store.settings()[0].value == "true", "boolean switch became a numeric field");
        require(store.settings()[1].type == SettingType::Float, "float lost slider precision");
        require(store.settings()[2].type == SettingType::Integer && store.settings()[2].selectedOptionIndex == 1,
            "missing choice did not use the authored default");
        require(read(toml) == original, "loading settings modified CSAH.toml");

        // Another writer changes the file after our snapshot.
        write(toml, "## === 01. Suite ===\n[CommunityShaders]\n## help\n# keep note\nEnabled = true\n[Lighting]\nStrength = 1.0\n[Unrelated]\nOther = 'external'\n");
        const auto beforeWrites = read(toml);
        require(store.setBooleanByIndex(0, false).saved, "boolean write failed");
        require(store.setNumericByIndex(1, 1.15).saved && std::abs(store.settings()[1].numericValue - 1.15) < 0.001, "float precision was lost");
        require(!store.setNumericByIndex(1, std::numeric_limits<double>::quiet_NaN()).saved, "NaN was accepted");
        require(!store.setOptionByIndex(2, 3).saved, "invalid choice was accepted");
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        require(read(toml) == beforeWrites, "preview wrote CSAH.toml");
#else
        require(!store.setOptionByIndex(2, 0).saved, "an option CSAH has not written yet must not be created by the menu");
        require(store.setNumericByIndex(1, 2.0).saved, "whole-number float write failed");
        require(store.reload(), store.lastError().c_str());
        require(store.settings()[0].value == "false" && store.settings()[1].value == "2.0", "saved values did not round-trip");
        const auto contents = read(toml);
        require(contents.find("Other = 'external'") != std::string::npos && contents.find("# keep note\nEnabled = false") != std::string::npos &&
                contents.find("## === 01. Suite ===") != std::string::npos,
            "single-value write overwrote another writer's change, a note or a heading");
#endif
        menu["tabs"][0]["pages"][0]["groups"][0]["controls"][1]["binding"]["path"] = "../other.toml";
        writeCatalog();
        require(!store.reload() && store.settings().empty(), "foreign binding was accepted");
        menu = fixture();
        menu["tabs"][0]["pages"][0]["groups"][0]["controls"].push_back(menu["tabs"][0]["pages"][0]["groups"][0]["controls"][0]);
        writeCatalog();
        require(!store.reload() && store.settings().empty(), "duplicate setting was accepted");
        menu = fixture(); menu["tabs"][0]["pages"][0]["groups"][0]["controls"][1]["step"] = 0;
        writeCatalog(); require(!store.reload(), "zero-step slider was accepted");
        menu = fixture(); menu["schemaVersion"] = 1;
        writeCatalog(); require(!store.reload(), "an INI-era catalog was accepted");
        if (argc > 1) {
            IniSettingsStore authored(toml, RpsMod::Csah, nullptr, std::filesystem::path(argv[1]));
            require(authored.load(), authored.lastError().c_str());
            std::cout << "Validated all " << authored.settings().size() << " authored CSAH controls\n";
        }
        std::filesystem::remove(toml);
        menu = fixture(); writeCatalog();
        require(!store.reload(), "missing CSAH.toml was created by the menu");
        std::cout << "CSAH settings bridge passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
