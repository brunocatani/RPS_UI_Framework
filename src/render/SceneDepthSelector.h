#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rpsui::render::scene_depth_selector
{
    inline constexpr std::uintptr_t kFunctionRva = 0x1D947D0;
    inline constexpr std::uintptr_t kRendererRva = 0x6239340;
    inline constexpr std::array<std::uint8_t, 5> kNativeBytes{0x0F, 0xB6, 0x41, 0x04, 0xC3};

    struct Binding
    {
        std::uintptr_t target{};
        bool trueScopes{};
        const char* stage = "selector-entry";
    };

    inline bool plausible(std::uintptr_t address) noexcept
    {
        return address >= 0x10000 && address <= 0x00007FFFFFFEFFFF;
    }

    inline bool isMainView(bool trueScopes, std::uint8_t selected) noexcept
    {
        // True Scopes' alternate pass renders the lens only. It must not
        // consume the framework's single main-view depth capture for a frame.
        // Native scopes still use alternate depth for the submitted view.
        return selected <= 1 && (!trueScopes || selected == 0);
    }

    // Accept the native reader or True Scopes' E9 detour, optionally through
    // F4SE's FF25 relay. Never follow an arbitrary chain or trust DLL presence
    // alone: the final executable address must belong to truescopes_vr.dll.
    template<class Reader, class IsTrueScopesCode>
    Binding resolve(std::uintptr_t entry, Reader read, IsTrueScopesCode isTrueScopesCode) noexcept
    {
        Binding result;
        std::array<std::uint8_t, 5> bytes{};
        if (!plausible(entry) || !read(entry, bytes.data(), bytes.size())) return result;
        if (bytes == kNativeBytes) return {entry, false, "native"};
        result.stage = "selector-detour";
        if (bytes[0] != 0xE9) return result;
        std::int32_t relative{};
        std::memcpy(&relative, bytes.data() + 1, sizeof(relative));
        auto target = static_cast<std::uintptr_t>(static_cast<std::int64_t>(entry + 5) + relative);
        if (!plausible(target)) return result;
        if (!isTrueScopesCode(target)) {
            result.stage = "selector-relay";
            std::array<std::uint8_t, 6> relay{};
            if (!read(target, relay.data(), relay.size()) || relay[0] != 0xFF || relay[1] != 0x25) return result;
            std::memcpy(&relative, relay.data() + 2, sizeof(relative));
            const auto slot = static_cast<std::uintptr_t>(static_cast<std::int64_t>(target + 6) + relative);
            if (!plausible(slot) || !read(slot, &target, sizeof(target))) return result;
        }
        result.stage = "selector-owner";
        if (!plausible(target) || !isTrueScopesCode(target)) return result;
        return {target, true, "TrueScopes"};
    }
}
