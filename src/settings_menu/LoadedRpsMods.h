#pragma once
#include "RpsMod.h"
#include <optional>
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
#include <Windows.h>
#endif

namespace rpsui::settings_menu {
inline bool configurationModLoaded(RpsMod mod) noexcept {
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
 return GetModuleHandleW(modInfo(mod).module)!=nullptr;
#else
 return false; // Preview availability comes only from its explicit fixtures.
#endif
}
inline std::optional<RpsMod> loadedRockMod() noexcept {
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
 if(configurationModLoaded(RpsMod::Rock)) return RpsMod::Rock;
 return std::nullopt;
#else
 return std::nullopt;
#endif
}
}
