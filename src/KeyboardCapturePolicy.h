#pragma once
#include <RPS_UI_Framework/Input.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rpsui::keyboard_policy {
// Raw Input's RI_KEY_BREAK. The game copies each raw make or break into its key
// state table, so a swallowed release would leave that key held forever. Only
// presses are withheld while the keyboard is captured.
inline constexpr std::uint16_t kRawKeyBreak = 1;
[[nodiscard]] constexpr bool swallowsRawKey(std::uint16_t rawFlags) noexcept {
 return (rawFlags & kRawKeyBreak) == 0;
}
// WM_CHAR also carries Backspace, Tab, Enter, Escape and Ctrl chords as control
// characters; those arrive as key events instead.
[[nodiscard]] constexpr bool printable(std::uint32_t unit) noexcept {
 return unit >= 0x20 && unit != 0x7F;
}

// Bounded because the window thread must never allocate or wait on a slow reader.
// A full queue drops the newest event: text typed before the stall stays in order.
class EventQueue {
public:
 void clear() noexcept { head_ = count_ = 0; }
 bool push(api::input::KeyboardEventKind kind, std::uint32_t code) noexcept {
  if (count_ == events_.size()) return false;
  auto& event = events_[(head_ + count_) % events_.size()];
  event = {};
  event.kind = kind;
  event.code = code;
  ++count_;
  return true;
 }
 [[nodiscard]] std::uint32_t drain(std::span<api::input::KeyboardEvent> output) noexcept {
  std::uint32_t taken = 0;
  while (count_ && taken < output.size()) {
   output[taken++] = events_[head_];
   head_ = (head_ + 1) % events_.size();
   --count_;
  }
  return taken;
 }
private:
 std::array<api::input::KeyboardEvent, 256> events_{};
 std::size_t head_{}, count_{};
};
}
