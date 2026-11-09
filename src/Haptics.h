#pragma once

// Controller pulses the UI asks for: the host's tick when a press activates a
// control, and pulses consumers request through the Framework API.
//
// Thread model: request() runs on any thread (render callbacks, consumer
// threads) and only folds the pulse into a per-hand atomic. The input tick on
// the game thread takes the pending pulse with take() and sends it through
// OpenVR, so nothing outside that tick touches the VR runtime.

#include <RPS_UI_Framework/Framework.h>

#include <cstdint>

namespace rpsui::haptics
{
    // A tick when a press activates a control, before the player's strength.
    inline constexpr float kPressTick = 0.5f;
    // OpenVR's longest single pulse.
    inline constexpr std::uint32_t kMaximumMicroseconds = 3999;

    // Pulse length for a request at the player's strength; 0 when either is 0.
    [[nodiscard]] std::uint32_t microseconds(float strength, float playerStrength) noexcept;

    // Any thread. Strength 0 to 1; the strongest request before the next input tick wins.
    void request(api::framework::PhysicalHand hand, float strength) noexcept;

    // Input tick. The hand's pending pulse in microseconds, or 0; clears it.
    [[nodiscard]] std::uint32_t take(unsigned side) noexcept;
}
