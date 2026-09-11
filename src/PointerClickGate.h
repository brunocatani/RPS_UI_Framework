#pragma once

#include <cstdint>

namespace rpsui::pointer_click_gate
{
    struct State
    {
        bool leaseAccepted{ false };
        bool neutralObserved{ false };
        std::uint64_t firstAcceptedFrame{ 0 };
    };

    inline void reset(State& state) noexcept
    {
        state = {};
    }

    // Capture is requested after native input has already been sampled for
    // the current frame. Forwarding therefore begins only on a
    // later frame, after physical neutral has been observed under the lease.
    [[nodiscard]] inline bool advance(
        State& state,
        std::uint64_t frameIndex,
        bool rawAvailable,
        bool primaryDown,
        bool leaseRequestAccepted) noexcept
    {
        if (!rawAvailable || !leaseRequestAccepted) {
            reset(state);
            return false;
        }

        if (!state.leaseAccepted || frameIndex < state.firstAcceptedFrame) {
            state.leaseAccepted = true;
            state.neutralObserved = false;
            state.firstAcceptedFrame = frameIndex;
        }

        const bool leaseMature = frameIndex > state.firstAcceptedFrame;
        if (leaseMature && !primaryDown) {
            state.neutralObserved = true;
        }
        return leaseMature && state.neutralObserved && primaryDown;
    }
}
