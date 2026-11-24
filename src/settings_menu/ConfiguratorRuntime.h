#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include "PanelResizePolicy.h"
#include "render/NativeRenderer.h"
namespace rpsui::settings_menu {
void onGameDataReady();
void shutdown();
bool isOpen() noexcept;
bool isOpening() noexcept;
// Increases each time the menu closes.
[[nodiscard]] std::uint64_t closedCount() noexcept;
bool takeInventoryRefreshRequest() noexcept;
void setAvailable(bool available) noexcept;
void close() noexcept;
void openAt(const devui::render::PanelPose& wheelPose);
[[nodiscard]] bool drawImGui(float x=0, float y=0,
 float width=devui::render::kPanelLayoutWidth, float height=devui::render::kPanelLayoutHeight, bool backRequested=false) noexcept;
// The keyboard panel under Config: shown on the Console tab, and on the Spawner while a search is open.
[[nodiscard]] bool drawKeyboardPanel(float x, float y, float width, float height, bool backRequested = false) noexcept;
void onFrameworkPanelFrame(float physicalWidth, panel_resize::Handle hovered, panel_resize::Handle active) noexcept;
struct ConsoleKeyEvent { bool character{ false }; std::uint32_t code{ 0 }; };
// True while the keyboard panel is up, which is also when the physical keyboard is captured.
[[nodiscard]] bool keyboardInputActive() noexcept;
// Game thread, once per input frame. Opens, moves or closes the keyboard panel, hands the
// physical keyboard's input to the line it types into, and runs the console's queued commands.
void serviceConsole(bool keyboardHeld, bool windowFocused, std::span<const ConsoleKeyEvent> keys);
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
// The desktop preview: input is ready and the Spawner holds its sample items.
void initializePreview();
void setPreviewOpen(bool open);
void drainPreviewActions();
// Shows the "Spawner" or the "Console" tab.
void previewShowTab(std::string_view tab);
// Typed keys for the Console and the Spawner's searches.
void previewKeys(std::span<const ConsoleKeyEvent> keys);
#endif
}
