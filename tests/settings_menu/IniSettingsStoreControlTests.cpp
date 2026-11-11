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
    const fs::path iniPath = directory / "ROCK.ini";
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        std::cerr << "FAIL: could not create the isolated test directory\n";
        return 1;
    }
    {
        std::ofstream output(iniPath, std::ios::binary);
        output <<
            "; preserved header\n"
            "[PhysicsInteraction]\n"
            "bEnabled = true\n"
            "; 0 = permissive, 1 = hybrid, 2 = strict.\n"
            "iMode = 1\n"
            "fContactIntensity = 0.50\n"
            "fExperimentalRecovery = 5.0\n"
            "sFreeText = keep-me\n";
    }

    rpsui::settings_menu::IniSettingsStore store(iniPath);
    expect(store.load(), "the isolated existing ROCK.ini loads");
    expect(store.settings().size() == 5, "all test settings are indexed");
    if (store.settings().size() == 5) {
        expect(store.settings()[0].control.kind == Kind::Checkbox,
            "boolean rows are checkbox controls");
        expect(store.settings()[1].control.kind == Kind::Dropdown,
            "documented integer modes are dropdown controls");
        expect(store.settings()[2].control.kind == Kind::Numeric &&
               store.settings()[2].control.bounded,
            "normalized numeric rows are bounded sliders");
        expect(store.settings()[3].control.kind == Kind::Numeric &&
               !store.settings()[3].control.bounded,
            "unbounded numeric rows remain draggable");
        expect(store.settings()[4].control.kind == Kind::Text &&
               !store.isAdjustable(4),
            "free-form strings are non-adjustable without a keyboard backend");

        expect(store.setBooleanByIndex(0, false).saved,
            "checkbox values persist through the explicit boolean setter");
        expect(store.setOptionByIndex(1, 2).saved,
            "dropdown values persist through the selected option");
        expect(store.setNumericByIndex(2, 5.0).saved,
            "bounded slider values persist after policy clamping");
        expect(store.setNumericByIndex(3, 5.25).saved,
            "unbounded drag values persist at their fine step");
    }

    const auto saved = readAll(iniPath);
    expect(saved.find("; preserved header") != std::string::npos,
        "unrelated comments survive control writes");
    expect(saved.find("bEnabled = false") != std::string::npos,
        "checkbox writes the explicit false value");
    expect(saved.find("iMode = 2") != std::string::npos,
        "dropdown writes the option value rather than its label");
    expect(saved.find("fContactIntensity = 1.0") != std::string::npos,
        "bounded slider commits clamp to one");
    expect(saved.find("fExperimentalRecovery = 5.25") != std::string::npos,
        "unbounded numeric commits retain fine adjustment");

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
    expect(!paper.settings()[1].control.bounded && !emitters.settings()[1].control.bounded,
        "ROCK-specific ranges cannot constrain another mod's matching key");
    expect(paper.setBooleanByIndex(0, false).saved, "PAPER checkbox writes to its own file");
    expect(paper.setOptionByIndex(2, 2).saved && paper.settings()[2].value == "Harvest",
        "PAPER development modes use supported values");
    expect(paper.setOptionByIndex(3, 2).saved && paper.settings()[3].value == "ReadWrite",
        "PAPER cache access uses supported values");
    auto paperSaved = readAll(paperPath);
    std::erase(paperSaved, '\r'); // PALM writes through a text stream.
    expect(paperSaved.find("# my note\nEnabled = false\n") != std::string::npos &&
               paperSaved.find("MaximumMode = 'Harvest'\n") != std::string::npos &&
               paperSaved.find("Access = 'ReadWrite'\n") != std::string::npos,
        "TOML edits keep notes, write booleans bare and strings quoted");
    expect(readAll(emittersPath) == emittersBefore && readAll(iniPath) == saved,
        "editing PAPER leaves ROCK and Emitters unchanged");
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
