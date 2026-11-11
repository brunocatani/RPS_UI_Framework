#include "IniSettingsStore.h"
#include <Windows.h>
#include <cassert>
int main(int argc,char** argv) {
    using namespace rpsui::settings_menu;
    namespace config=scissors::configuration;
    assert(argc==2);
    assert(!isRockConfiguration(RpsMod::Scissors)&&!isRockConfiguration(RpsMod::ScissorsDeveloper));
    assert(std::wstring_view(modInfo(RpsMod::Scissors).module)==L"SCISSORS.dll");
    assert(std::string_view(modInfo(RpsMod::ScissorsDeveloper).ini)=="SCISSORS_Developer.toml");
    IniSettingsStore absent("never-read.ini",RpsMod::Scissors);assert(!absent.load());
    const auto module=LoadLibraryA(argv[1]);assert(module);
    const auto query=reinterpret_cast<config::Query>(GetProcAddress(module,config::kQueryExport));assert(query);
    assert(!query(1,1,32)); // Major 1 is retired.
    const auto* api=query(config::kMajor,config::kMinor,sizeof(config::Api));assert(api);
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
    // A loaded real module is never auto-bound by preview.
    assert(!absent.load());
    const auto* fixture=api;
#else
    const config::Api* fixture=nullptr; // Exercise real named-module discovery.
#endif
    IniSettingsStore consumer("never-read.ini",RpsMod::Scissors,nullptr,{},0,fixture);
    IniSettingsStore developer("never-read-dev.ini",RpsMod::ScissorsDeveloper,nullptr,{},0,fixture);
    assert(consumer.load()&&developer.load());
    // Each page shows exactly its group's rows of SCISSORS' catalog, at the module's values.
    const auto indexOf=[](const IniSettingsStore& store,std::string_view key) {
        const auto& rows=store.settings();
        std::size_t index=0;
        while(index<rows.size()&&rows[index].key!=key)++index;
        assert(index<rows.size());
        return index;
    };
    std::uint32_t defaults=0;
    for(const auto& metadata:config::kSettings) {
        if(metadata.defaultValue)defaults|=config::bit(metadata.id);
        const auto& page=metadata.group==config::Group::Consumer?consumer:developer;
        const auto& row=page.settings()[indexOf(page,metadata.key)];
        assert(row.type==SettingType::Boolean&&row.value==(metadata.defaultValue?"true":"false"));
    }
    // Numbers are bounded by SCISSORS' catalog, in steps of it.
    for(const auto& metadata:config::kNumbers) {
        const auto& page=metadata.group==config::Group::Consumer?consumer:developer;
        const auto& row=page.settings()[indexOf(page,metadata.key)];
        assert(row.type==SettingType::Float&&row.control.bounded&&row.control.kind==setting_control::Kind::Numeric);
        assert(row.control.minimum==metadata.minimum&&row.control.maximum==metadata.maximum&&row.control.step==metadata.step);
        assert(row.value==row.defaultValue&&!row.overridden);
    }
    assert(consumer.settings().size()+developer.settings().size()==config::kSettings.size()+config::kNumbers.size());
    assert(!consumer.needsReload()&&!developer.needsReload());
    // Writes go through SCISSORS by key, from the page that owns the setting.
    const auto collisionIndex=indexOf(consumer,"NpcDynamicCollisions");
    const auto visualizerIndex=indexOf(developer,"DebugShowColliders");
    const auto collision=consumer.setBooleanByIndex(collisionIndex,false);assert(collision.changed&&collision.saved);
    const auto visualizer=developer.setBooleanByIndex(visualizerIndex,true);assert(visualizer.changed&&visualizer.saved);
    const auto physical=developer.setBooleanByIndex(indexOf(developer,"PhysicalMelee"),false);assert(physical.changed&&physical.saved);
    const auto hunchIndex=indexOf(developer,"RagdollHunch");
    const auto hunch=developer.setNumericByIndex(hunchIndex,2.5);assert(hunch.changed&&hunch.saved&&developer.settings()[hunchIndex].overridden);
    config::Snapshot state;assert(api->snapshot(&state)==config::Status::Ok);
    config::NumberSnapshot numbers;assert(api->numbers(&numbers)==config::Status::Ok);
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
    assert(state.values==defaults&&state.revision==1); // No persistence or live call.
    assert(numbers.values[static_cast<std::size_t>(config::Number::Hunch)]==1.0f);
#else
    assert(state.values==((defaults&~config::bit(config::Setting::Collision)&~config::bit(config::Setting::PhysicalMelee))|config::bit(config::Setting::Visualizer)));
    assert(numbers.values[static_cast<std::size_t>(config::Number::Hunch)]==2.5f);
    assert(state.revision==5);
    assert(consumer.needsReload()&&developer.needsReload());
    assert(consumer.reload()&&consumer.settings()[collisionIndex].value=="false");
    assert(developer.reload()&&developer.settings()[visualizerIndex].value=="true");
    assert(!developer.needsReload());
#endif
    assert(!std::filesystem::exists("never-read.ini")&&!std::filesystem::exists("never-read-dev.ini"));
    FreeLibrary(module);
}
