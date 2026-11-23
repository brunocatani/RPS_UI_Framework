#include "IniSettingsStore.h"
#include <Windows.h>
#include <array>
#include <charconv>

namespace rpsui::settings_menu {
    namespace {
        namespace config=diamond::configuration;
        // A setting's value as the page shows it: true/false, the number in its
        // steps, or the choice's name.
        std::string textOf(const config::Metadata& metadata,float value) {
            switch(metadata.type) {
            case config::Type::Toggle:return value!=0?"true":"false";
            case config::Type::Choice: {
                const auto index=static_cast<std::uint32_t>(value);
                return index<metadata.choiceCount?metadata.choices[index]:metadata.choices[0];
            }
            default:return setting_control::formatFloat(value,metadata.step);
            }
        }
    }
    bool IniSettingsStore::loadDiamondSettings() {
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        if(!_diamondApi) {
            const auto module=GetModuleHandleW(modInfo(RpsMod::Diamond).module);
            const auto query=module?reinterpret_cast<config::Query>(GetProcAddress(module,config::kQueryExport)):nullptr;
            if(query)_diamondApi=query(config::kMajor,config::kMinor,sizeof(config::Api));
        }
#endif
        if(!_diamondApi||_diamondApi->major!=config::kMajor||_diamondApi->minor<config::kMinor||_diamondApi->size<sizeof(config::Api)||
            !_diamondApi->snapshot||!_diamondApi->setValue) {
            _diamondApi=nullptr;_lastError="DIAMOND Configuration 2.0 is unavailable";return false;
        }
        config::Snapshot snapshot;
        if(_diamondApi->snapshot(&snapshot)!=config::Status::Ok||snapshot.count<config::kSettingCount) {
            _lastError="DIAMOND configuration is not ready";return false;
        }
        std::vector<SettingRecord> settings;
        for(const auto& metadata:config::kSettings) {
            SettingRecord setting;
            setting.section=metadata.table;setting.key=metadata.key;setting.id=setting.section+"."+setting.key;
            setting.value=textOf(metadata,snapshot.values[static_cast<std::size_t>(metadata.id)]);
            setting.defaultValue=textOf(metadata,metadata.defaultValue);
            setting.category=metadata.category;setting.description=metadata.description;
            setting.overridden=setting.value!=setting.defaultValue;
            refreshControl(setting);settings.push_back(std::move(setting));
        }
        _settings=std::move(settings);_loadedRevision=snapshot.revision;return true;
    }
    bool IniSettingsStore::saveDiamondSetting(const SettingRecord& setting,std::string_view value) {
        const auto* metadata=config::find(setting.section,setting.key);
        if(!metadata){_lastError="Unknown DIAMOND setting";return false;}
        float number{};
        switch(metadata->type) {
        case config::Type::Toggle:number=value=="true"?1.f:0.f;break;
        case config::Type::Choice: {
            std::uint32_t index=0;
            while(index<metadata->choiceCount&&!setting_control::equalsIgnoreCase(metadata->choices[index],value))++index;
            if(index==metadata->choiceCount){_lastError="Unknown DIAMOND choice";return false;}
            number=static_cast<float>(index);break;
        }
        default: {
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
            if(parsed.ec!=std::errc{}){_lastError="Invalid DIAMOND number";return false;}
        }
        }
        // The call goes to DIAMOND, which owns the file; preview edits stay memory-only.
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        std::array<char,512> error{};
        if(!_diamondApi||_diamondApi->setValue(metadata->id,number,error.data(),static_cast<std::uint32_t>(error.size()))!=config::Status::Ok) {
            _lastError=error[0]?error.data():"DIAMOND configuration write failed";return false;
        }
#endif
        return true;
    }
}
