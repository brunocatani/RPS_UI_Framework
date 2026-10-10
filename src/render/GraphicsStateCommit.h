#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace rpsui::render::graphics_state_commit
{
    inline constexpr std::array<std::uint8_t, 19> nativePrologue{
        0x88,0x54,0x24,0x10,0x88,0x4C,0x24,0x08,
        0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x50
    };

    struct Entry
    {
        bool valid{};
        std::uintptr_t detour{};
    };

    // The framework patches a CALL SITE, not this callee. APFLEX's MinHook
    // entry detour must remain in the chain, including its post-commit work.
    // MinHook writes five bytes but relocates BOTH four-byte instructions.
    // We inspect the untouched tail and never relocate or replace this entry.
    template<class Read, class Executable>
    [[nodiscard]] Entry inspect(std::uintptr_t entry, Read read, Executable executable) noexcept
    {
        std::array<std::uint8_t, nativePrologue.size()> bytes{};
        if (!read(entry, bytes.data(), bytes.size()) || !executable(entry)) return {};
        if (bytes == nativePrologue) return { true, 0 };
        if (bytes[0] != 0xE9 || !std::equal(bytes.begin() + 5, bytes.end(), nativePrologue.begin() + 5)) return {};
        std::int32_t displacement{};
        std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
        const auto target = entry + 5 + displacement;
        if ((target >= entry && target < entry + bytes.size()) || !executable(target)) return {};
        return { true, target };
    }
}
