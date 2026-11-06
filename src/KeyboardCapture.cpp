#include "PCH.h"
#include "KeyboardCapture.h"
#include "KeyboardCapturePolicy.h"
#include "Logger.h"

#include <Windows.h>
#include <cstdlib>

// FO4VR registers the keyboard for Raw Input with no flags (window procedure
// 0x140D8BCA0, WM_CREATE path), so legacy WM_KEYDOWN and WM_CHAR still arrive and
// input stops when the window loses the foreground. It copies each raw make or break
// into its keyboard device's key table (0x141BBC810), and that table is the only
// source of the game's key button events. WM_CHAR is the source of its character
// events. Withholding raw presses and WM_CHAR therefore removes the keyboard from
// the game without touching ControlMap or any other engine state.
namespace rpsui::keyboard {
namespace {
// Main singleton pointer, and Main::hwnd as stored by its constructor 0x140D80660.
constexpr std::uintptr_t kMainSingletonRva = 0x5B04308, kMainWindowOffset = 0x30, kGameWindowProcRva = 0xD8BCA0;

std::atomic_bool g_captured{};
std::atomic<HWND> g_window{};
WNDPROC g_original{}; // Written once before the procedure is installed.
std::mutex g_mutex;
keyboard_policy::EventQueue g_queue;

bool readWord(std::uintptr_t source, std::uintptr_t& target) noexcept {
 __try { target = *reinterpret_cast<const std::uintptr_t*>(source); return true; }
 __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void enqueue(api::input::KeyboardEventKind kind, std::uint32_t code) noexcept {
 static std::atomic_bool logged{};
 std::scoped_lock lock(g_mutex);
 if (!g_queue.push(kind, code) && !logged.exchange(true)) log::warn("UI keyboard queue overflowed; later keys were dropped");
}
// The class is ANSI, so WM_CHAR holds a byte in the active layout's code page. The
// game converts it the same way.
std::uint32_t characterUnit(WPARAM value) noexcept {
 if (value < 0x80 || value > 0xFF) return static_cast<std::uint32_t>(value);
 char locale[8]{};
 const auto language = LOWORD(reinterpret_cast<std::uintptr_t>(GetKeyboardLayout(0)));
 const auto codePage = GetLocaleInfoA(language, LOCALE_IDEFAULTANSICODEPAGE, locale, sizeof(locale)) ? std::strtoul(locale, nullptr, 10) : CP_ACP;
 const auto byte = static_cast<char>(value);
 wchar_t unit{};
 return MultiByteToWideChar(codePage ? codePage : CP_ACP, 0, &byte, 1, &unit, 1) == 1 ? static_cast<std::uint32_t>(unit) : 0u;
}
bool swallowsRawInput(HRAWINPUT input) noexcept {
 RAWINPUT raw{};
 UINT size = sizeof(raw);
 // Larger device reports do not fit and fail here; only a keyboard record is wanted.
 return GetRawInputData(input, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
  raw.header.dwType == RIM_TYPEKEYBOARD && keyboard_policy::swallowsRawKey(raw.data.keyboard.Flags);
}
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wide, LPARAM wordLong) {
 if (g_captured.load(std::memory_order_acquire)) {
  switch (message) {
  case WM_INPUT:
   // Windows requires DefWindowProc for raw input cleanup.
   if (swallowsRawInput(reinterpret_cast<HRAWINPUT>(wordLong))) return DefWindowProcA(window, message, wide, wordLong);
   break;
  case WM_CHAR:
   if (const auto unit = characterUnit(wide); keyboard_policy::printable(unit)) enqueue(api::input::KeyboardEventKind::Character, unit);
   return 0;
  case WM_KEYDOWN:
   enqueue(api::input::KeyboardEventKind::Key, static_cast<std::uint32_t>(wide));
   return 0;
  default:
   break;
  }
 }
 return CallWindowProcA(g_original, window, message, wide, wordLong);
}
}
bool install() noexcept {
 if (g_window.load()) return true;
 std::uintptr_t main{}, value{};
 if (!readWord(REL::Offset(kMainSingletonRva).address(), main) || !main || !readWord(main + kMainWindowOffset, value)) {
  log::error("UI keyboard unavailable: the game's main object could not be read");
  return false;
 }
 const auto window = reinterpret_cast<HWND>(value);
 DWORD process{};
 if (!IsWindow(window) || !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId()) {
  log::error("UI keyboard unavailable: {:#x} is not this process's game window", value);
  return false;
 }
 const auto expected = REL::Offset(kGameWindowProcRva).address();
 const auto current = static_cast<std::uintptr_t>(GetWindowLongPtrA(window, GWLP_WNDPROC));
 g_original = reinterpret_cast<WNDPROC>(current);
 SetLastError(0);
 if (!SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProcedure)) && GetLastError()) {
  log::error("UI keyboard unavailable: subclassing failed with error {}", GetLastError());
  return false;
 }
 g_window = window;
 log::info("UI keyboard ready; previous window procedure {:#x}{}", current, current == expected ? "" : " is not the game's, another mod has chained it");
 return true;
}
bool available() noexcept { return g_window.load() != nullptr; }
void setCaptured(bool captured) noexcept {
 if (captured) {
  std::scoped_lock lock(g_mutex);
  g_queue.clear();
 }
 g_captured.store(captured, std::memory_order_release);
}
std::uint32_t drain(std::span<api::input::KeyboardEvent> output) noexcept {
 std::scoped_lock lock(g_mutex);
 return g_queue.drain(output);
}
bool focused() noexcept {
 const auto window = g_window.load();
 return window && GetForegroundWindow() == window;
}
}
