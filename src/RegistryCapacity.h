#pragma once
#include <cstddef>

namespace rpsui
{
    // Runtime budgets are independent of the fixed Cooperation snapshot arrays.
    // Registration reserves CPU metadata only; GPU targets belong to open panels.
    inline constexpr std::size_t kConsumerCapacity = 256;
    inline constexpr std::size_t kPanelCapacity = 1024;
    inline constexpr std::size_t kInputSubscriberCapacity = 256;
}
