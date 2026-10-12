#pragma once
#include "RPSUIFrameworkApi.h"

namespace rpsui::sdk {
// Independent opt-in contract. V1 panel registration/render/state layouts and
// all existing panel behavior remain unchanged. Major must match exactly;
// minimumMinor must be supported. Version 1.0 provides the complete table below.
// Focused panels preserve the submitted world pose (no automatic separation),
// require FixedSize, and route interaction only while the selected visible
// pointer is inside. Only that hand's stick is captured. A UI button press is
// drained on exit until release, without capturing the stick or dispatching a
// click to a different panel. Opening/focus changes require neutral before click.
// Use ApiV1 for poses/state and CooperationApiV1 for safe retirement. Contexts
// must survive CallbackBusy. Rendering and input thread contracts are unchanged.
struct FocusedPanelApiV1 {
    std::uint32_t structSize{sizeof(FocusedPanelApiV1)};
    std::uint32_t major{1}, minor{}, apiFlavor{RPSUI_API_FLAVOR};
    ResultV1 (RPSUI_CALL* registerPanel)(std::uint64_t owner,
        const PanelRegistrationV1*, std::uint64_t* handle) noexcept{};
};
inline const FocusedPanelApiV1* RequestFocusedPanelApiV1(std::uint32_t minimumMinor = 0) noexcept {
    const auto module = GetModuleHandleW(L"RPS_UI_Framework.dll");
    const auto address = module ? GetProcAddress(module, "RPSUI_RequestFocusedPanelApi") : nullptr;
    if (!address) return nullptr;
    using Request = const FocusedPanelApiV1*(RPSUI_CALL*)(std::uint32_t, std::uint32_t) noexcept;
    const auto* api = reinterpret_cast<Request>(address)(1, minimumMinor);
    return api && api->structSize >= sizeof(*api) && api->major == 1 &&
        api->minor >= minimumMinor && api->apiFlavor == RPSUI_API_FLAVOR && api->registerPanel ? api : nullptr;
}
static_assert(std::is_standard_layout_v<FocusedPanelApiV1> && sizeof(FocusedPanelApiV1) == 24);
static_assert(offsetof(FocusedPanelApiV1, registerPanel) == 16);
}
