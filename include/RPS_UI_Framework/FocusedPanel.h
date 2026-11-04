#pragma once
#include "Framework.h"

// Focused panels preserve the submitted world pose (no automatic separation),
// require FixedSize, and route interaction only while the selected visible
// pointer is inside. Only that hand's stick is captured. A UI button press is
// drained on exit until release, without capturing the stick or dispatching a
// click to a different panel. Opening and focus changes require neutral before
// a click. Use the Framework interface for poses and state and the Cooperation
// interface for safe retirement. Contexts must survive CallbackBusy.
#pragma pack(push, 8)
namespace rpsui::api::focused_panel
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::FocusedPanel;
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
