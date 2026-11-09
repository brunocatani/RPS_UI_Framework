#pragma once
#include "Framework.h"

// Visual-only panels. The host never hit-tests these panels, captures input for
// them, resizes them or separates their poses. Require Transparent|FixedSize and
// a draw callback. Submit poses through the Framework interface; retire
// callbacks through the Cooperation interface's safe unregister calls.
#pragma pack(push, 8)
namespace rpsui::api::passive_panel
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::PassivePanel;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        Result(RPSUI_CALL* registerPanel)(std::uint64_t ownerToken, const framework::PanelRegistration* registration,
            std::uint64_t* outPanelHandle) noexcept {};
    };

    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 8);
}
#pragma pack(pop)
