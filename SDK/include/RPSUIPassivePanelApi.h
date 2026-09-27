#pragma once
#include "RPSUIFrameworkApi.h"

namespace rpsui::sdk
{
    // Independently negotiated visual-only panels. Existing panel, pointer and
    // cooperation V1 contracts are unchanged. The host never hit-tests these
    // panels, captures input for them, resizes them or separates their poses.
    // Require Transparent|FixedSize and a render callback. Submit poses through
    // ApiV1; retire callbacks through CooperationApiV1's safe unregister calls.
    struct PassivePanelApiV1
    {
        std::uint32_t structSize{sizeof(PassivePanelApiV1)}, apiVersion{1};
        std::uint32_t apiFlavor{RPSUI_API_FLAVOR}, reserved32{};
        ResultV1 (RPSUI_CALL* registerPanel)(std::uint64_t owner,
            const PanelRegistrationV1*, std::uint64_t* handle) noexcept{};
    };
    inline const PassivePanelApiV1* RequestPassivePanelApiV1() noexcept
    {
        const auto module = GetModuleHandleW(L"RPS_UI_Framework.dll");
        if (!module) return nullptr;
        const auto address = GetProcAddress(module, "RPSUI_RequestPassivePanelApi");
        if (!address) return nullptr;
        using Request = const PassivePanelApiV1* (RPSUI_CALL*)(std::uint32_t) noexcept;
        const auto* api = reinterpret_cast<Request>(address)(1);
        return api && api->structSize >= sizeof(*api) && api->apiVersion == 1 &&
            api->apiFlavor == RPSUI_API_FLAVOR && api->registerPanel ? api : nullptr;
    }
    static_assert(sizeof(PassivePanelApiV1) == 24 && offsetof(PassivePanelApiV1, registerPanel) == 16);
}
