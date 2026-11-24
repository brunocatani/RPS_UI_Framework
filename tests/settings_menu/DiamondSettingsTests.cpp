#include "IniSettingsStore.h"
#include <Windows.h>
#include <cassert>

int main(int argc,char** argv) {
    using namespace rpsui::settings_menu;
    namespace config=diamond::configuration;
    assert(argc==2);
    assert(std::wstring_view(modInfo(RpsMod::Diamond).module)==L"DIAMOND.dll");
    assert(std::string_view(modInfo(RpsMod::Diamond).ini)=="DIAMOND.toml");
    IniSettingsStore absent("never-read.toml",RpsMod::Diamond);assert(!absent.load());
    const auto module=LoadLibraryA(argv[1]);assert(module);
    const auto query=reinterpret_cast<config::Query>(GetProcAddress(module,config::kQueryExport));assert(query);
    assert(!query(1,0,32)); // Major 1 is retired.
    const auto* api=query(config::kMajor,config::kMinor,sizeof(config::Api));assert(api);
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
    assert(!absent.load()); // A loaded real module is never auto-bound by preview.
    const auto* fixture=api;
#else
    const config::Api* fixture=nullptr; // Exercise real named-module discovery.
#endif
    IniSettingsStore store("never-read.toml",RpsMod::Diamond,{},nullptr,fixture);
    assert(store.load()&&store.settings().size()==config::kSettingCount);
    // Every row is DIAMOND's catalog entry, in its order, with its control.
    for(std::size_t i=0;i<config::kSettingCount;++i) {
        const auto& metadata=config::kSettings[i];const auto& row=store.settings()[i];
        assert(row.section==metadata.table&&row.key==metadata.key&&row.label==metadata.label&&row.category==metadata.category);
        assert(row.value==row.defaultValue&&!row.overridden);
        switch(metadata.type) {
        case config::Type::Toggle:assert(row.type==SettingType::Boolean&&row.control.kind==setting_control::Kind::Checkbox);break;
        case config::Type::Choice:
            assert(row.type==SettingType::String&&row.control.kind==setting_control::Kind::Dropdown&&row.control.options.size()==metadata.choiceCount);
            assert(row.selectedOptionIndex&&*row.selectedOptionIndex==static_cast<std::size_t>(metadata.defaultValue));break;
        default:
            assert(row.type==SettingType::Float&&row.control.kind==setting_control::Kind::Numeric&&row.control.bounded);
            assert(row.control.minimum==metadata.minimum&&row.control.maximum==metadata.maximum&&row.control.step==metadata.step);
        }
    }
    // Writes go through DIAMOND by name: numbers clamp to the catalog's range, choices by name.
    const auto up=store.indexForId("Pouch.Up");const auto style=store.indexForId("MagazineCounter.Style");
    const auto enabled=store.indexForId("Pouch.Enabled");assert(up&&style&&enabled&&store.isSlider(*up));
    assert(store.setNumericByIndex(*up,1000).saved&&store.settings()[*up].numericValue==80);
    assert(store.setNumericByIndex(*up,12.5).saved);
    assert(store.setOptionByIndex(*style,0).saved&&store.settings()[*style].value=="Plain");
    assert(store.setBooleanByIndex(*enabled,false).saved);
    config::Snapshot state;assert(api->snapshot(&state)==config::Status::Ok);
    const auto value=[&](config::Setting id){return state.values[static_cast<std::size_t>(id)];};
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
    assert(state.revision==1&&value(config::Setting::PouchUp)==config::metadata(config::Setting::PouchUp).defaultValue); // No live call.
#else
    assert(state.revision==5&&value(config::Setting::PouchUp)==12.5f&&value(config::Setting::MagazineStyle)==0&&value(config::Setting::PouchEnabled)==0);
    assert(store.needsReload()&&store.reload()&&store.settings()[*up].value=="12.5"&&!store.needsReload());
#endif
    assert(!std::filesystem::exists("never-read.toml"));
    FreeLibrary(module);
}
