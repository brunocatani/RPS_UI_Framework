#pragma once
#include <cstdint>

namespace rpsui::input_policy {
inline constexpr bool chordHeld(std::uint64_t left,std::uint64_t right,std::uint64_t leftChord,std::uint64_t rightChord) {
 return (leftChord || rightChord) && (left&leftChord)==leftChord && (right&rightChord)==rightChord;
}
inline constexpr std::uint64_t capturedMask(unsigned hand,std::uint64_t buttons,std::uint64_t leftChord,std::uint64_t rightChord,std::uint64_t leftPressed,std::uint64_t rightPressed) {
 return hand<2?buttons|(chordHeld(leftPressed,rightPressed,leftChord,rightChord)?(hand?rightChord:leftChord):0):0;
}
inline constexpr std::uint8_t axesForButtons(std::uint64_t buttons) {
 auto axes=static_cast<std::uint8_t>((buttons>>32)&0x1f);
 if(buttons&(1ull<<2))axes|=1u<<2;
 return axes;
}
}
