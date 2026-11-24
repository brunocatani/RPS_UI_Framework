#include "PCH.h"
#include "ModsConfigMenu.h"
#include "Logger.h"
#include "ModsConfigFO4Api.h"

#include <Windows.h>

namespace rpsui::mods_config_menu {
namespace {
// Mods Config's API, bound here without ModsConfigFO4Client::initialize so that a fault in
// its DLL is caught: only getVersion, getModVersion and getMenuState are called.
using HostApiV1 = ::mods_config::api::ModsConfigFO4ApiV1;
using MenuState = ::mods_config::api::MenuState;
using GetApi = const HostApiV1*(MODS_CONFIG_FO4_CALL*)();
constexpr auto kDll = ::mods_config::api::MODS_CONFIG_FO4_DLL;
constexpr auto kGetApi = ::mods_config::api::MODS_CONFIG_FO4_GET_API;
constexpr std::uint32_t kMinimumApi = 1;

enum class Binding { Unresolved, Absent, Bound, Failed };
struct State {
 Binding binding{Binding::Unresolved};
 const HostApiV1* api{}; // Owned by Mods Config for the life of the process.
};
State& state() noexcept { static State s; return s; }

// These call another DLL's code; a fault there disables the integration instead of the game.
bool fetchApi(GetApi getApi, const HostApiV1*& api) noexcept {
 __try { api = getApi(); return true; }
 __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool readIdentity(const HostApiV1* api, std::uint32_t& version, char (&modVersion)[32]) noexcept {
 __try {
  version = api->getVersion();
  const char* text = api->getModVersion();
  std::size_t length = 0;
  for (; text && length + 1 < sizeof(modVersion) && text[length]; ++length) modVersion[length] = text[length];
  modVersion[length] = '\0';
  return true;
 }
 __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool readMenuState(const HostApiV1* api, MenuState& menuState) noexcept {
 __try { menuState = api->getMenuState(); return true; }
 __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void bind(State& s) noexcept {
 s.binding = Binding::Failed;
 const auto module = GetModuleHandleA(kDll);
 if (!module) {
  s.binding = Binding::Absent;
  log::info("Mods Config FO4 VR is not loaded");
  return;
 }
 const auto getApi = reinterpret_cast<GetApi>(GetProcAddress(module, kGetApi));
 const HostApiV1* api{};
 std::uint32_t version{};
 char modVersion[32]{};
 if (!getApi || !fetchApi(getApi, api) || !api || !readIdentity(api, version, modVersion)) {
  log::error("Mods Config FO4 VR is loaded but gave no menu-state API; RPS UI input does not yield to its menu");
  return;
 }
 if (version < kMinimumApi) {
  log::error("Mods Config FO4 VR API {} is older than {}; RPS UI input does not yield to its menu", version, kMinimumApi);
  return;
 }
 s.api = api;
 s.binding = Binding::Bound;
 log::info("Mods Config FO4 VR {} found (API {}); RPS UI input yields while its menu is open", modVersion, version);
}
}

bool isOpen() noexcept {
 auto& s = state();
 if (s.binding == Binding::Unresolved) bind(s);
 if (s.binding != Binding::Bound) return false;
 MenuState menuState{};
 if (!readMenuState(s.api, menuState)) {
  s.binding = Binding::Failed;
  s.api = nullptr;
  log::error("Mods Config FO4 VR menu-state call faulted; RPS UI input no longer yields to its menu");
  return false;
 }
 // A minimized menu takes no input.
 return menuState == MenuState::Open;
}
}
