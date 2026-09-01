#pragma once

#include <cstdint>

namespace rpsui::pointer_hand_selection
{
    enum class Hand : std::uint8_t
    {
        None,
        Left,
        Right,
    };

    struct Candidate
    {
        bool valid{ false };
        bool hitsPanel{ false };
        bool primaryDown{ false };
    };

    struct State
    {
        Hand active{ Hand::None };
        bool submittedPrimaryDown{ false };
        bool pressBeganOnPanel{ false };
    };

    struct Decision
    {
        Hand hand{ Hand::None };
        bool previousPrimaryDown{ false };
        bool pressBeganOnPanel{ false };
    };

    inline void reset(State& state) noexcept
    {
        state = {};
    }

    [[nodiscard]] inline const Candidate& candidateFor(
        Hand hand,
        const Candidate& left,
        const Candidate& right) noexcept
    {
        static constexpr Candidate unavailable{};
        if (hand == Hand::Left) {
            return left;
        }
        if (hand == Hand::Right) {
            return right;
        }
        return unavailable;
    }

    [[nodiscard]] inline Hand firstValid(
        Hand preferred,
        const Candidate& left,
        const Candidate& right) noexcept
    {
        if (candidateFor(preferred, left, right).valid) {
            return preferred;
        }
        if (left.valid) {
            return Hand::Left;
        }
        if (right.valid) {
            return Hand::Right;
        }
        return Hand::None;
    }

    /*
     * One native RPS UI Framework panel owns one live pointer stream. Preserve the submitted hand
     * through button-up so a drag cannot jump controllers; otherwise favor the
     * sole pressed hand, then a ray that actually intersects the panel, then the
     * current/preferred valid hand for stable off-panel hover.
     */
    [[nodiscard]] inline Decision choose(
        const State& state,
        const Candidate& left,
        const Candidate& right,
        Hand preferred) noexcept
    {
        const auto& active = candidateFor(state.active, left, right);
        if (state.submittedPrimaryDown && active.valid) {
            return { state.active, true, state.pressBeganOnPanel };
        }

        if (left.primaryDown != right.primaryDown) {
            const Hand pressed = left.primaryDown ? Hand::Left : Hand::Right;
            if (candidateFor(pressed, left, right).valid) {
                return { pressed, false, false };
            }
        } else if (left.primaryDown && right.primaryDown) {
            const Hand selected = active.valid ? state.active : firstValid(preferred, left, right);
            return { selected, false, false };
        }

        if (active.valid && active.hitsPanel) {
            return { state.active, false, false };
        }
        if (left.hitsPanel != right.hitsPanel) {
            const Hand hit = left.hitsPanel ? Hand::Left : Hand::Right;
            if (candidateFor(hit, left, right).valid) {
                return { hit, false, false };
            }
        }
        if (left.hitsPanel && right.hitsPanel) {
            const Hand selected = active.valid ? state.active : firstValid(preferred, left, right);
            return { selected, false, false };
        }
        if (active.valid) {
            return { state.active, false, false };
        }
        return { firstValid(preferred, left, right), false, false };
    }

    [[nodiscard]] inline bool controlsPanelInteraction(
        const Decision& decision,
        const Candidate& selected) noexcept
    {
        return selected.hitsPanel ||
               (decision.previousPrimaryDown && decision.pressBeganOnPanel);
    }

    inline void commit(
        State& state,
        const Decision& decision,
        const Candidate& selected) noexcept
    {
        const bool sameHand = state.active == decision.hand;
        const bool risingPrimary = selected.primaryDown &&
                                   !(sameHand && state.submittedPrimaryDown);
        state.active = decision.hand;
        if (risingPrimary) {
            state.pressBeganOnPanel = selected.hitsPanel;
        } else if (!selected.primaryDown) {
            state.pressBeganOnPanel = false;
        }
        state.submittedPrimaryDown = selected.primaryDown;
    }
}
