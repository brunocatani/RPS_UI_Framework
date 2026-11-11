#pragma once

#include <cstdint>

namespace rpsui::settings_menu::spawn_plugin_identity
{
    inline constexpr std::uint32_t kLightPluginPrefix = 0xFE000000u;
    inline constexpr std::uint32_t kRuntimeCreatedPrefix = 0xFF000000u;

    // Runtime FormIDs are the authoritative plugin identity available on every
    // loaded form. Full plugins occupy the high byte; light plugins share FE
    // and carry their 12-bit small-file index in bits 12-23.
    [[nodiscard]] constexpr std::uint32_t pluginKeyFromFormId(std::uint32_t formId) noexcept
    {
        if ((formId & 0xFF000000u) == kLightPluginPrefix) {
            return kLightPluginPrefix | ((formId & 0x00FFF000u) >> 12u);
        }
        return formId >> 24u;
    }

    [[nodiscard]] constexpr bool isLightPluginKey(std::uint32_t key) noexcept
    {
        return (key & 0xFF000000u) == kLightPluginPrefix;
    }

    [[nodiscard]] constexpr bool isRuntimeCreatedFormId(std::uint32_t formId) noexcept
    {
        return (formId & 0xFF000000u) == kRuntimeCreatedPrefix;
    }
}
