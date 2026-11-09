#include "PCH.h"
#include "ModsConfigMenu.h"
#include "Logger.h"

#include <Windows.h>

namespace rpsui::mods_config_menu {
namespace {
// The leading slots of Mods Config's ModsConfigFO4ApiV1, in its declaration order. That
// interface only ever appends slots, so a later host keeps these. Only getVersion,
// getModVersion and getMenuState are called; the other two only hold their slots.
class HostApiV1 {
public:
 virtual std::uint32_t getVersion() const = 0;
 virtual const char* getModVersion() const = 0;
 virtual bool addQuickLaunch(void* client, const char* text, const char* help, const char* buttonText, const char* functionName) const = 0;
 virtual bool registerClient(const char* name, std::uint32_t version, void* client) const = 0;
 virtual std::uint32_t getMenuState() const = 0;
protected:
 ~HostApiV1() = default;
};
using GetApi = const HostApiV1*(__cdecl*)();
constexpr char kDll[] = "ModsConfigFO4VR.dll";
constexpr char kGetApi[] = "ModsConfigFO4API_GetApiV1";
constexpr std::uint32_t kMinimumApi = 1;
// MenuState is Closed = 0, Open = 1, Minimized = 2. A minimized menu takes no input.
constexpr std::uint32_t kMenuOpen = 1;

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
bool readMenuState(const HostApiV1* api, std::uint32_t& menuState) noexcept {
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
 std::uint32_t menuState{};
 if (!readMenuState(s.api, menuState)) {
  s.binding = Binding::Failed;
  s.api = nullptr;
  log::error("Mods Config FO4 VR menu-state call faulted; RPS UI input no longer yields to its menu");
  return false;
 }
 return menuState == kMenuOpen;
}
}
