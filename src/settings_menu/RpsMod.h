#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace rpsui::settings_menu {
enum class RpsMod : std::uint8_t { Rock, Paper, RockDeveloper, Csah, Diamond, Emitters, Scissors, ScissorsDeveloper, RpsUi };
struct RpsModInfo {
 RpsMod id;
 const char* name;
 const wchar_t* module;
 const char* directory;
 const char* ini;
 const char* applyHint;
};
inline constexpr std::array kRpsMods{
 RpsModInfo{RpsMod::Rock,"ROCK",L"ROCK.dll","Mods_Config/ROCK","ROCK.toml","Changes save to ROCK.toml and apply live"},
 RpsModInfo{RpsMod::Paper,"PAPER",L"PAPER.dll","Mods_Config/PAPER","PAPER.toml","Changes save to PAPER.toml and apply live"},
 RpsModInfo{RpsMod::RockDeveloper,"ROCK Dev",L"ROCK.dll","Mods_Config/ROCK","ROCK_Developer.toml","Changes save to ROCK_Developer.toml; only values off their defaults are kept"},
 RpsModInfo{RpsMod::Csah,"CSAH",L"CSAH.dll","Mods_Config/CSAH","CSAH.toml","Changes apply through CSAH's watcher; controls marked Restart apply next launch"},
 RpsModInfo{RpsMod::Diamond,"DIAMOND",L"DIAMOND.dll","Mods_Config/DIAMOND","DIAMOND.ini","PouchUI moves and rotates the pouch indicator. Enable Show grab zone boundary to preview while adjusting. Changes apply immediately and save to DIAMOND.ini"},
 RpsModInfo{RpsMod::Emitters,"Emitters",L"ROCK_Emitters.dll","Mods_Config/ROCK_Emitters","ROCK_Emitters.toml","Changes save to ROCK_Emitters.toml and apply live within about 250 ms"},
 RpsModInfo{RpsMod::Scissors,"SCISSORS",L"SCISSORS.dll","Mods_Config/SCISSORS","SCISSORS.toml","NPC settings are saved by SCISSORS and apply next frame"},
 RpsModInfo{RpsMod::ScissorsDeveloper,"SCISSORS Dev",L"SCISSORS.dll","Mods_Config/SCISSORS","SCISSORS_Developer.toml","NPC visualization is independent of ROCK. Only developer overrides are saved by SCISSORS"},
 RpsModInfo{RpsMod::RpsUi,"RPS UI",L"RPS_UI_Framework.dll","Mods_Config/RPS_UI_Framework","RPS_UI.toml","Every mod's panels follow these. Changes save to RPS_UI.toml and apply within a second; themes are files in the Themes folder beside it"}
};
inline constexpr const RpsModInfo& modInfo(RpsMod mod){return kRpsMods[static_cast<std::size_t>(mod)];}
inline constexpr std::array kRpsModDisplayOrder{
 RpsMod::Rock,RpsMod::RockDeveloper,
 RpsMod::Paper,RpsMod::Csah,RpsMod::Diamond,RpsMod::Emitters,RpsMod::Scissors,RpsMod::ScissorsDeveloper,RpsMod::RpsUi
};
inline constexpr bool isRockConfiguration(RpsMod mod) {
 return mod==RpsMod::Rock || mod==RpsMod::RockDeveloper;
}
inline constexpr bool isScissorsConfiguration(RpsMod mod) {
 return mod==RpsMod::Scissors || mod==RpsMod::ScissorsDeveloper;
}
}
