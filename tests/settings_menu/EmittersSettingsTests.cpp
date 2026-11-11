#include "IniSettingsStore.h"
#include "EmitterSettings.h"
#include "LoadedRpsMods.h"
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <fstream>

int main(int argc,char** argv)
{
    using namespace rpsui::settings_menu;
    namespace fs=std::filesystem;
    assert(argc==3);
    assert(!configurationModLoaded(RpsMod::Emitters));
    const auto module=LoadLibraryW(fs::path(argv[1]).c_str());assert(module);
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
    assert(configurationModLoaded(RpsMod::Emitters));
#else
    assert(!configurationModLoaded(RpsMod::Emitters));
#endif
    // Fixtures stay with the test build, never in the system temporary directory.
    const fs::path directory(SETTINGS_MENU_EMITTERS_TEST_DIR);std::error_code ignored;fs::remove_all(directory,ignored);fs::create_directories(directory);
    const auto path=directory/"ROCK_Emitters.toml";
    {std::ifstream source(argv[2]);assert(source);std::ofstream out(path);out<<source.rdbuf();out<<"\n# preserve user notes\n[Notes]\nText = 'unchanged'\n";}
    const auto read=[&]{std::ifstream in(path);return std::string(std::istreambuf_iterator<char>(in),{});};
    const auto before=read();
    IniSettingsStore store(path,RpsMod::Emitters);assert(store.load());assert(read()==before);
    assert(store.settings().size()==emitter_settings::settings.size()+1);
    for(const auto& expected:emitter_settings::settings) {
        const auto index=store.indexForId(std::string("Main.")+expected.key);assert(index);
        const auto& setting=store.settings()[*index];assert(setting.label==expected.label);
        if(expected.type==emitter_settings::Type::Number) {
            assert(setting.type==SettingType::Float&&store.isSlider(*index));
            assert(setting.control.minimum==expected.minimum&&setting.control.maximum==expected.maximum);
        } else if(expected.type==emitter_settings::Type::Toggle) assert(setting.control.kind==setting_control::Kind::Checkbox);
        else assert(setting.control.options.size()==3);
    }
    const auto brightness=*store.indexForId("Main.ReticleBrightness");
    assert(store.setNumericByIndex(brightness,1).saved);
    assert(store.settings()[brightness].type==SettingType::Float); // 1 must not become a checkbox.
    const auto radius=*store.indexForId("Main.HitLightRadius");assert(store.setNumericByIndex(radius,8).saved);
    const auto enabled=*store.indexForId("Main.Enabled");assert(store.setBooleanByIndex(enabled,false).saved);
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
    assert(configurationModLoaded(RpsMod::Emitters)); // Page remains available to re-enable it.
#endif
    const auto color=*store.indexForId("Main.LaserColor");assert(store.setOptionByIndex(color,1).saved);
    assert(!store.setOptionByIndex(color,3).saved);
    const auto flashlight=*store.indexForId("Main.FlashlightIntensity");
    assert(store.setNumericByIndex(flashlight,100).saved&&store.settings()[flashlight].numericValue==20);
    assert(store.setNumericByIndex(flashlight,0).saved&&store.settings()[flashlight].numericValue==0.1);
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
    assert(read()==before);
#else
    const auto saved=read();assert(saved.find("# preserve user notes")!=std::string::npos&&saved.find("Text = 'unchanged'")!=std::string::npos);
    IniSettingsStore reopened(path,RpsMod::Emitters);assert(reopened.load());
    assert(reopened.settings()[*reopened.indexForId("Main.ReticleBrightness")].numericValue==1);
    assert(reopened.settings()[*reopened.indexForId("Main.HitLightRadius")].numericValue==8);
    assert(reopened.settings()[*reopened.indexForId("Main.LaserColor")].value=="Green");
    assert(reopened.setBooleanByIndex(*reopened.indexForId("Main.Enabled"),true).saved);
#endif
    assert(FreeLibrary(module));assert(!configurationModLoaded(RpsMod::Emitters));
    fs::remove_all(directory,ignored);
}
