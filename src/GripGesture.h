#pragma once

// What a grip press on a panel means. A press released before the hold time
// is Back, delivered on the release so the whole press stays with the UI and
// nothing of it reaches the game after the panel closes. On a movable panel a
// press held past the hold time moves the panel until it is released, and is
// not Back.

#include <cstdint>

namespace rpsui::grip_gesture
{
    enum class Phase : std::uint8_t
    {
        Idle,
        Pressed,
        Moving,
    };

    struct State
    {
        Phase phase{ Phase::Idle };
        std::uint64_t panel{ 0 };
        double pressedAt{ 0.0 };
    };

    struct Step
    {
        // A press released on the panel it began on, before the hold time.
        std::uint64_t back{ 0 };
        // The hold time passed on a movable panel: the move starts this tick.
        std::uint64_t beginMove{ 0 };
        bool moving{ false };
        bool endMove{ false };
    };

    struct Sample
    {
        // The grip as the UI may take it: on a panel that uses the grip, with
        // the hand's capture accepted.
        bool gatedDown{ false };
        // The physical grip.
        bool rawDown{ false };
        // The panel under the pointer, 0 for none.
        std::uint64_t panel{ 0 };
        bool panelMovable{ false };
        double seconds{ 0.0 };
        double holdSeconds{ 0.45 };
    };

    [[nodiscard]] inline Step advance(State& state, const Sample& sample) noexcept
    {
        Step step{};
        switch (state.phase) {
        case Phase::Idle:
            if (sample.gatedDown && sample.panel) {
                state = { Phase::Pressed, sample.panel, sample.seconds };
            }
            break;
        case Phase::Pressed:
            if (!sample.rawDown) {
                // Released: Back, when the pointer is still on the panel it pressed.
                if (sample.panel == state.panel) step.back = state.panel;
                state = {};
            } else if (!sample.gatedDown || sample.panel != state.panel) {
                // The pointer left the panel or the UI lost the hand: no Back.
                state = {};
            } else if (sample.panelMovable && sample.seconds - state.pressedAt >= sample.holdSeconds) {
                state.phase = Phase::Moving;
                step.beginMove = state.panel;
                step.moving = true;
            }
            break;
        case Phase::Moving:
            if (sample.rawDown) {
                step.moving = true;
            } else {
                step.endMove = true;
                state = {};
            }
            break;
        }
        return step;
    }

    inline void reset(State& state) noexcept { state = {}; }
}
