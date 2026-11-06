#pragma once
#include <RPS_UI_Framework/Input.h>

#include <cstdint>
#include <span>

// Physical keyboard for text entry. The window procedure runs on whichever thread
// pumps the game window's messages; the rest runs on the input service's owner.
// Captured state is one atomic the window procedure reads, so releasing capture
// needs no engine state to be restored.
namespace rpsui::keyboard {
// Subclasses the game window for the life of the process: other mods may chain
// after this, so it never unhooks.
bool install() noexcept;
[[nodiscard]] bool available() noexcept;
// Turning capture on discards keys queued by an earlier owner.
void setCaptured(bool captured) noexcept;
[[nodiscard]] std::uint32_t drain(std::span<api::input::KeyboardEvent> output) noexcept;
[[nodiscard]] bool focused() noexcept;
}
