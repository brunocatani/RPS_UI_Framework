#include "IniSettingsStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    [[nodiscard]] std::string readAll(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }
}

int main()
{
    namespace fs = std::filesystem;
    using rpsui::settings_menu::setting_control::Kind;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path directory = fs::temp_directory_path() /
        ("devui-setting-controls-" + std::to_string(unique));
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        std::cerr << "FAIL: could not create the isolated test directory\n";
        return 1;
    }

    // A TOML file as RPS Framework's settings store writes it.
    const auto paperPath = directory / "PAPER.toml";
    const auto emittersPath = directory / "ROCK_Emitters.ini";
    {
        std::ofstream paper(paperPath);
        paper << "## PAPER.toml - PAPER options\n\n## === 01. General ===\n[Main]\n## Enables PAPER.\n"
              << "# my note\nEnabled = true\n\n## Contact help.\nContactIntensity = 0.5\n\n"
              << "## === 04. Development Capture ===\n[DevelopmentCapture]\n## User, Observe, Harvest or Capture.\n"
              << "MaximumMode = 'User'\n\n## === 05. Weapon Motion Cache ===\n[WeaponMotionCache]\nAccess = \"Off\"\n";
        std::ofstream emitters(emittersPath);
        emitters << "; untouched sibling\n[Main]\nbEnabled = true\nfContactIntensity = 0.5\n";
    }
    const auto emittersBefore = readAll(emittersPath);
    rpsui::settings_menu::IniSettingsStore paper(paperPath, rpsui::settings_menu::RpsMod::Paper);
    rpsui::settings_menu::IniSettingsStore emitters(emittersPath, rpsui::settings_menu::RpsMod::Emitters);
    expect(paper.load() && emitters.load(), "each mod loads its own file");
    expect(paper.settings().size() == 4, "TOML comments, headings and notes are not settings");
    expect(paper.settings()[0].control.kind == Kind::Checkbox, "PAPER boolean remains a checkbox");
    expect(paper.settings()[0].category == "01. General" && paper.settings()[0].description == "Enables PAPER.",
        "a TOML option takes its category heading and its ## help, never the player's note");
    expect(paper.settings()[2].category == "04. Development Capture" && paper.settings()[2].value == "User",
        "a TOML string shows without its quotes");
    expect(paper.settings()[1].control.kind == Kind::Numeric && !paper.settings()[1].control.bounded,
        "a number without a published range is a free numeric control");
    expect(paper.setBooleanByIndex(0, false).saved, "PAPER checkbox writes to its own file");
    expect(paper.setNumericByIndex(1, 0.75).saved && paper.settings()[1].value == "0.75",
        "a numeric edit keeps its fine step");
    expect(paper.setOptionByIndex(2, 2).saved && paper.settings()[2].value == "Harvest",
        "PAPER development modes use supported values");
    expect(paper.setOptionByIndex(3, 2).saved && paper.settings()[3].value == "ReadWrite",
        "PAPER cache access uses supported values");
    auto paperSaved = readAll(paperPath);
    std::erase(paperSaved, '\r'); // PALM writes through a text stream.
    expect(paperSaved.find("# my note\nEnabled = false\n") != std::string::npos &&
               paperSaved.find("ContactIntensity = 0.75\n") != std::string::npos &&
               paperSaved.find("MaximumMode = 'Harvest'\n") != std::string::npos &&
               paperSaved.find("Access = 'ReadWrite'\n") != std::string::npos,
        "TOML edits keep notes, write booleans and numbers bare and strings quoted");
    expect(readAll(emittersPath) == emittersBefore, "editing PAPER leaves Emitters unchanged");
    expect(emitters.setBooleanByIndex(0, false).saved, "Emitters checkbox writes its own file");
    expect(readAll(emittersPath).find("; untouched sibling") != std::string::npos,
        "Emitters comments survive edits");

    fs::remove_all(directory, error);
    if (error) {
        std::cerr << "FAIL: could not remove the isolated test directory\n";
        ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " INI-control integration assertion(s) failed.\n";
        return 1;
    }
    std::cout << "DevUI INI-control integration tests passed.\n";
    return 0;
}
