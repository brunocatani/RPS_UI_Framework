#include "PCH.h"

#include "Haptics.h"
#include "UiSettings.h"

namespace rpsui::haptics
{
    namespace
    {
        std::atomic_uint32_t pending[2]{};
    }

    std::uint32_t microseconds(float strength, float playerStrength) noexcept
    {
        if (!(strength > 0.0f) || !(playerStrength > 0.0f)) return 0;
        const float scaled = std::min(strength, 1.0f) * std::min(playerStrength, 1.0f);
        return static_cast<std::uint32_t>(scaled * static_cast<float>(kMaximumMicroseconds) + 0.5f);
    }

    void request(api::framework::PhysicalHand hand, float strength) noexcept
    {
        if (hand != api::framework::PhysicalHand::Left && hand != api::framework::PhysicalHand::Right) return;
        const auto length = microseconds(strength, settings::current()->hapticStrength);
        if (!length) return;
        auto& slot = pending[hand == api::framework::PhysicalHand::Left ? 0 : 1];
        auto current = slot.load(std::memory_order_relaxed);
        while (current < length && !slot.compare_exchange_weak(current, length, std::memory_order_release, std::memory_order_relaxed)) {}
    }

    std::uint32_t take(unsigned side) noexcept
    {
        return side < 2 ? pending[side].exchange(0, std::memory_order_acquire) : 0;
    }
}
