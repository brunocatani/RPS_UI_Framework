#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>
#define RPSUI_CALL __cdecl

// RPS UI Framework's shared ABI vocabulary. Each interface header carries its
// own kMajor and kMinor; records and tables belong to their interface headers.
#pragma pack(push, 8)
namespace rpsui::api
{
    // Layout version of every size/version-tagged record. Interface versions
    // are separate and negotiated through discovery.
    inline constexpr std::uint32_t kRecordVersion = 1;

    enum class Result : std::uint32_t
    {
        Ok = 0,
        InvalidArgument = 1,
        VersionMismatch = 2,
        FrameworkNotReady = 3,
        CapacityReached = 4,
        OwnerNotRegistered = 5,
        PanelNotRegistered = 6,
        OwnershipMismatch = 7,
        InvalidPose = 8,
        CallbackFailed = 9,
        InternalError = 10,
        ResourceConflict = 11,
        UnsupportedStage = 12,
        CallbackBusy = 13,
        PanelClosing = 14,
        // The consumer's Dear ImGui build differs from the host's (framework::ImGuiBuild).
        ImGuiBuildMismatch = 15,
        // Discovery.
        UnknownInterface = 100,
        UnsupportedMajor = 101,
        UnsupportedMinor = 102,
        TableTooSmall = 103,
    };

    // Interface IDs are permanent and never reused.
    enum class InterfaceId : std::uint32_t
    {
        Framework = 1,
        Cooperation = 2,
        FocusedPanel = 3,
        Input = 4,
        NumberOverlay = 5,
        PassivePanel = 6,
        Registry = 7,
        WorldDraw = 8,
    };

    static_assert(sizeof(void*) == 8, "RPS UI Framework requires a 64-bit consumer");
    static_assert(sizeof(Result) == 4 && sizeof(InterfaceId) == 4);
}
#pragma pack(pop)
