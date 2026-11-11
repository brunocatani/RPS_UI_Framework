#pragma once
#include <RPS_UI_Framework/Client.h>

// The settings menu is a consumer of RPS UI's own published tables, served in-process by
// the discovery export of this DLL (src/Api.cpp).
extern "C" rpsui::api::Result RPSUI_CALL RPSUI_QueryInterfaceV1(rpsui::api::InterfaceId id, std::uint32_t exactMajor,
    std::uint32_t minimumMinor, std::uint32_t minimumTableBytes, const rpsui::api::InterfaceDescriptorV1** output) noexcept;

namespace rpsui::settings_menu {
[[nodiscard]] inline rpsui::api::QueryInterfaceV1 rpsUiQuery() noexcept { return &RPSUI_QueryInterfaceV1; }
}
