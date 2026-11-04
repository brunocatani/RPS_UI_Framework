#pragma once
#include "Abi.h"

// Resolve kQueryExportName from the loaded kModuleName and ask for an interface
// by ID, exact major, minimum minor and minimum table size. A major this RPS UI
// does not serve returns UnsupportedMajor and no table; RPS UI logs the refusal
// and names the calling DLL in an error box at the main menu.
// The pre-2.0 exports (RPSUI_RequestApi, RPSUI_RequestCooperationApi,
// RPSUI_RequestFocusedPanelApi, RPSUI_RequestInputApi,
// RPSUI_RequestNumberOverlayApi, RPSUI_RequestPassivePanelApi,
// RPSUI_RequestRegistryApi and RPSUI_RequestWorldDrawApi) only refuse outdated
// consumers; their names are never reused.
#pragma pack(push, 8)
namespace rpsui::api
{
    struct InterfaceDescriptorV1
    {
        std::uint32_t size{ sizeof(InterfaceDescriptorV1) };
        InterfaceId interfaceId{};
        std::uint32_t major{};
        std::uint32_t minor{};
        std::uint32_t tableByteSize{};
        // Owner tokens come from the Framework interface; a consumer checks it
        // negotiated at least this Framework version.
        std::uint32_t requiredFrameworkMajor{};
        std::uint32_t requiredFrameworkMinor{};
        std::uint32_t reserved{};
        const void* table{};
    };

    // Any thread, including before the framework is ready. The descriptor and
    // table stay valid for the process lifetime.
    using QueryInterfaceV1 = Result(RPSUI_CALL*)(InterfaceId interfaceId,
        std::uint32_t exactMajor, std::uint32_t minimumMinor,
        std::uint32_t minimumTableBytes,
        const InterfaceDescriptorV1** outDescriptor) noexcept;
    inline constexpr char kQueryExportName[] = "RPSUI_QueryInterfaceV1";
    inline constexpr wchar_t kModuleName[] = L"RPS_UI_Framework.dll";

    static_assert(sizeof(InterfaceDescriptorV1) == 40);
}
#pragma pack(pop)
