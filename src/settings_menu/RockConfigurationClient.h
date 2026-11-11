#pragma once
#include <ROCK/Client.h>
#include <ROCK/Configuration.h>
#include "LoadedRpsMods.h"
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
#include <Windows.h>
#endif
namespace rpsui::settings_menu {
struct ConfigurationConnection {
    const rock::api::configuration::Api* api{};
    rock::api::OwnerToken owner{};
    rock::api::Status status{rock::api::Status::NotReady};
};
inline ConfigurationConnection configurationConnection(std::optional<RpsMod> requested = std::nullopt) noexcept {
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
    const auto selected = requested ? requested : loadedRockMod();
    if (!selected || !isRockConfiguration(*selected)) return {};
    // One process-lifetime registration shared by ROCK's consumer and developer
    // pages. Connection and visits are performed on the game thread.
    static auto* client=new rock::api::Client();
    static const rock::api::configuration::Api* api{};
    if (!api) {
        const auto module=GetModuleHandleW(modInfo(*selected).module);
        const auto query=module?reinterpret_cast<rock::api::QueryInterfaceV1>(GetProcAddress(module,rock::api::kQueryExportName)):nullptr;
        if(!client->owner()) {
            const auto status=client->connect(query,"RobCo Settings menuuration");
            if(status!=rock::api::Status::Ok) return {nullptr,0,status};
        }
        const auto status=client->acquire(3,api);
        if(status!=rock::api::Status::Ok) return {nullptr,client->owner(),status};
    }
    return {api,client->owner(),rock::api::Status::Ok};
#else
    return {};
#endif
}
}
