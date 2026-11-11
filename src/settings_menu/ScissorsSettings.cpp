#include "IniSettingsStore.h"
#include <Windows.h>
#include <array>
#include <charconv>

namespace rpsui::settings_menu {
    bool IniSettingsStore::loadScissorsSettings() {
        namespace config=scissors::configuration;
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        if(!_scissorsApi) {
            const auto module=GetModuleHandleW(L"SCISSORS.dll");
            const auto query=module?reinterpret_cast<config::Query>(GetProcAddress(module,config::kQueryExport)):nullptr;
            if(query)_scissorsApi=query(config::kMajor,config::kMinor,sizeof(config::Api));
        }
#endif
        if(!_scissorsApi || _scissorsApi->major!=config::kMajor || _scissorsApi->minor<config::kMinor || _scissorsApi->size<sizeof(config::Api) ||
            !_scissorsApi->snapshot || !_scissorsApi->setValue || !_scissorsApi->numbers || !_scissorsApi->setNumber) {
            _scissorsApi=nullptr;_lastError="SCISSORS Configuration 2.0 is unavailable";return false;
        }
        config::Snapshot snapshot;config::NumberSnapshot numbers;
        if(_scissorsApi->snapshot(&snapshot)!=config::Status::Ok || _scissorsApi->numbers(&numbers)!=config::Status::Ok || numbers.count<config::kNumbers.size()) {
            _lastError="SCISSORS configuration is not ready";return false;
        }
        const auto group=_mod==RpsMod::ScissorsDeveloper?config::Group::Developer:config::Group::Consumer;
        std::vector<SettingRecord> settings;
        for(const auto& metadata:config::kSettings) {
            if(metadata.group!=group)continue;
            SettingRecord setting;
            setting.section="PhysicsInteraction";setting.key=metadata.key;setting.id=setting.section+"."+setting.key;
            setting.value=(snapshot.values&config::bit(metadata.id))?"true":"false";
            setting.defaultValue=metadata.defaultValue?"true":"false";
            setting.category=group==config::Group::Consumer?"NPC interactions":"NPC diagnostics and experiments";
            setting.description=metadata.description;setting.overridden=setting.value!=setting.defaultValue;
            setting.type=SettingType::Boolean;
            refreshControl(setting);settings.push_back(std::move(setting));
        }
        for(const auto& metadata:config::kNumbers) {
            if(metadata.group!=group)continue;
            SettingRecord setting;
            setting.section="PhysicsInteraction";setting.key=metadata.key;setting.id=setting.section+"."+setting.key;
            setting.value=setting_control::formatFloat(numbers.values[static_cast<std::size_t>(metadata.id)],metadata.step);
            setting.defaultValue=setting_control::formatFloat(metadata.defaultValue,metadata.step);
            setting.category=group==config::Group::Consumer?"NPC interactions":"NPC diagnostics and experiments";
            setting.description=metadata.description;setting.overridden=setting.value!=setting.defaultValue;
            setting.type=SettingType::Float;
            refreshControl(setting);settings.push_back(std::move(setting));
        }
        _settings=std::move(settings);_loadedRevision=snapshot.revision;return true;
    }
    bool IniSettingsStore::saveScissorsSetting(const SettingRecord& setting,std::string_view value) {
        namespace config=scissors::configuration;
        const auto group=_mod==RpsMod::ScissorsDeveloper?config::Group::Developer:config::Group::Consumer;
        // The call goes to SCISSORS, which owns the file; preview edits stay memory-only, even with an injected provider.
        const auto write=[&](auto&& call) {
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
            std::array<char,512> error{};
            if(!_scissorsApi || call(error)!=config::Status::Ok) {
                _lastError=error[0]?error.data():"SCISSORS configuration write failed";return false;
            }
#else
            (void)call;
#endif
            return true;
        };
        for(const auto& metadata:config::kSettings) {
            if(metadata.group!=group || setting.key!=metadata.key)continue;
            return write([&](std::array<char,512>& error){return _scissorsApi->setValue(metadata.id,value=="true"?1u:0u,error.data(),static_cast<std::uint32_t>(error.size()));});
        }
        for(const auto& metadata:config::kNumbers) {
            if(metadata.group!=group || setting.key!=metadata.key)continue;
            float number{};
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
            if(parsed.ec!=std::errc{}){_lastError="Invalid SCISSORS number";return false;}
            return write([&](std::array<char,512>& error){return _scissorsApi->setNumber(metadata.id,number,error.data(),static_cast<std::uint32_t>(error.size()));});
        }
        _lastError="Unknown SCISSORS setting";return false;
    }
}
