#pragma once
#include <cstdint>

namespace rpsui::input_policy {
enum class Source { Native, Rock, Unavailable };
inline constexpr Source selectSource(bool rockRegistered,bool rockReady) {
 return !rockRegistered?Source::Native:rockReady?Source::Rock:Source::Unavailable;
}
inline constexpr bool chordHeld(std::uint64_t left,std::uint64_t right,std::uint64_t leftChord,std::uint64_t rightChord) {
 return (leftChord || rightChord) && (left&leftChord)==leftChord && (right&rightChord)==rightChord;
}
inline constexpr std::uint8_t axesForButtons(std::uint64_t buttons) {
 auto axes=static_cast<std::uint8_t>((buttons>>32)&0x1f);
 if(buttons&(1ull<<2))axes|=1u<<2;
 return axes;
}
}
