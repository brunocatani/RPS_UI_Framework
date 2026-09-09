#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if !defined(_WINDOWS_) && !defined(_INC_WINDOWS)
struct HINSTANCE__;
using HMODULE = HINSTANCE__*;
using LPCSTR = const char*;
using FARPROC = std::intptr_t(__stdcall*)();
extern "C" __declspec(dllimport) HMODULE __stdcall GetModuleHandleW(const wchar_t*);
extern "C" __declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE, LPCSTR);
#endif

namespace rpsui::sdk
{
#define RPSUI_CALL __cdecl

    inline constexpr std::uint32_t RPSUI_API_VERSION = 1;
    inline constexpr std::uint32_t RPSUI_API_FLAVOR = 0x31535052u;
    inline constexpr std::size_t RPSUI_MAX_CONSUMERS = 16;
    inline constexpr std::size_t RPSUI_MAX_PANELS = 8;

    enum class ResultV1 : std::uint32_t
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
    };

    enum class FeatureV1 : std::uint64_t
    {
        None = 0,
        MultipleWorldPanels = 1ull << 0,
        SceneDepthOcclusion = 1ull << 1,
        CentralPointerRouting = 1ull << 2,
        ContextualTwoAxisScroll = 1ull << 3,
        PhysicalPanelResize = 1ull << 4,
        ConsumerRenderCallbacks = 1ull << 5,
        SharedStereoComposition = 1ull << 6,
        ShapedPanels = 1ull << 7,
        ConfigNavigation = 1ull << 8,
    };

    enum class PanelFlagV1 : std::uint32_t
    {
        Transparent = 1u << 0,
        FixedSize = 1u << 1,
        CircularInput = 1u << 2,
        // Opt in to raw stick handoff and grip-back. Hold-based wheels leave this unset.
        ConfigNavigation = 1u << 3,
    };

    [[nodiscard]] constexpr bool hasPanelFlag(std::uint32_t flags, PanelFlagV1 flag) noexcept
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] constexpr bool panelContains(std::uint32_t flags, float u, float v) noexcept
    {
        if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1)) return false;
        if (!hasPanelFlag(flags, PanelFlagV1::CircularInput)) return true;
        const float x = u * 2 - 1, y = v * 2 - 1;
        return x * x + y * y <= 1;
    }

    enum class PhysicalHandV1 : std::uint32_t
    {
        None = 0,
        Left = 1,
        Right = 2,
    };

    enum class ResizeHandleV1 : std::uint32_t
    {
        None = 0,
        Left = 1,
        Right = 2,
        Top = 3,
        Bottom = 4,
        TopLeft = 5,
        TopRight = 6,
        BottomLeft = 7,
        BottomRight = 8,
    };

    struct ConsumerRegistrationV1
    {
        std::uint32_t structSize{ sizeof(ConsumerRegistrationV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        char consumerId[64]{};
        char displayName[96]{};
        std::uint64_t requestedFeatures{ 0 };
        std::uint64_t reserved[4]{};
    };

    struct ConsumerHandleV1
    {
        std::uint32_t structSize{ sizeof(ConsumerHandleV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        std::uint64_t ownerToken{ 0 };
        std::uint64_t grantedFeatures{ 0 };
        std::uint64_t reserved[2]{};
    };

    struct PanelPoseV1
    {
        float center[3]{};
        float right[3]{ 1.0f, 0.0f, 0.0f };
        float up[3]{ 0.0f, 0.0f, 1.0f };
        float front[3]{ 0.0f, -1.0f, 0.0f };
        float physicalWidth{ 112.5f };
        float physicalHeight{ 70.3125f };
    };

    struct PanelRenderFrameV1
    {
        std::uint32_t structSize{ sizeof(PanelRenderFrameV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        std::uint64_t panelHandle{ 0 };
        std::uint64_t frameSequence{ 0 };
        std::uint64_t deviceGeneration{ 0 };
        void* d3dDevice{ nullptr };
        void* d3dContext{ nullptr };
        void* renderTargetView{ nullptr };
        std::uint32_t pixelWidth{ 0 };
        std::uint32_t pixelHeight{ 0 };
        float deltaSeconds{ 0.0f };
        float pointerPixelX{ 0.0f };
        float pointerPixelY{ 0.0f };
        float scrollAxisX{ 0.0f };
        float scrollAxisY{ 0.0f };
        float physicalWidth{ 0.0f };
        float physicalHeight{ 0.0f };
        PhysicalHandV1 pointerHand{ PhysicalHandV1::None };
        ResizeHandleV1 hoveredResizeHandle{ ResizeHandleV1::None };
        ResizeHandleV1 activeResizeHandle{ ResizeHandleV1::None };
        std::uint8_t pointerValid{ 0 };
        std::uint8_t primaryDown{ 0 };
        std::uint8_t backDown{ 0 }; // Gated physical grip of the pointing hand; derive edges locally.
        std::uint8_t reservedBytes[5]{};
        std::uint64_t reserved[4]{};
    };

    using PanelRenderCallbackV1 = void (RPSUI_CALL*)(
        const PanelRenderFrameV1* frame,
        void* userData) noexcept;

    struct PanelRegistrationV1
    {
        std::uint32_t structSize{ sizeof(PanelRegistrationV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        char panelId[64]{};
        char displayName[96]{};
        std::uint32_t pixelWidth{ 1440 };
        std::uint32_t pixelHeight{ 900 };
        float defaultPhysicalWidth{ 112.5f };
        float minimumPhysicalWidth{ 72.0f };
        float maximumPhysicalWidth{ 180.0f };
        std::int32_t sortOrder{ 0 };
        // Uses the former reserved32 slot. Zero preserves rectangular opaque panels.
        union { std::uint32_t flags{ 0 }; std::uint32_t reserved32; };
        PanelRenderCallbackV1 renderCallback{ nullptr };
        void* userData{ nullptr };
        std::uint64_t reserved[4]{};
    };

    struct PanelPresentationV1
    {
        std::uint32_t structSize{ sizeof(PanelPresentationV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        std::uint64_t sequence{ 0 };
        PanelPoseV1 pose{};
        std::uint8_t open{ 0 };
        std::uint8_t reservedBytes[7]{};
        std::uint64_t reserved[3]{};
    };

    struct PanelStateV1
    {
        std::uint32_t structSize{ sizeof(PanelStateV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        std::uint64_t sequence{ 0 };
        PanelPoseV1 pose{};
        PhysicalHandV1 pointerHand{ PhysicalHandV1::None };
        ResizeHandleV1 hoveredResizeHandle{ ResizeHandleV1::None };
        ResizeHandleV1 activeResizeHandle{ ResizeHandleV1::None };
        std::uint8_t open{ 0 };
        std::uint8_t pointerValid{ 0 };
        std::uint8_t primaryDown{ 0 };
        std::uint8_t reservedBytes[5]{};
        std::uint64_t reserved[3]{};
    };

    struct ApiV1
    {
        std::uint32_t structSize{ sizeof(ApiV1) };
        std::uint32_t apiVersion{ RPSUI_API_VERSION };
        std::uint32_t apiFlavor{ RPSUI_API_FLAVOR };
        std::uint32_t reserved32{ 0 };
        std::uint64_t featureBits{ 0 };

        bool (RPSUI_CALL* isFrameworkReady)() noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* registerConsumer)(
            const ConsumerRegistrationV1* registration,
            ConsumerHandleV1* outHandle) noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* unregisterConsumer)(
            std::uint64_t ownerToken) noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* registerPanel)(
            std::uint64_t ownerToken,
            const PanelRegistrationV1* registration,
            std::uint64_t* outPanelHandle) noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* unregisterPanel)(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* submitPanelPresentation)(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle,
            const PanelPresentationV1* presentation) noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* resetPanelSize)(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle) noexcept{ nullptr };
        ResultV1 (RPSUI_CALL* getPanelState)(
            std::uint64_t ownerToken,
            std::uint64_t panelHandle,
            PanelStateV1* outState) noexcept{ nullptr };

        std::uint64_t reserved[8]{};
    };

    using RequestApiFunction = const ApiV1* (RPSUI_CALL*)(
        std::uint32_t requestedVersion) noexcept;

    [[nodiscard]] inline const ApiV1* RequestApiV1() noexcept
    {
        const auto module = GetModuleHandleW(L"RPS_UI_Framework.dll");
        if (!module) {
            return nullptr;
        }
        const auto address = GetProcAddress(module, "RPSUI_RequestApi");
        if (!address) {
            return nullptr;
        }
        const auto request = reinterpret_cast<RequestApiFunction>(address);
        const auto* api = request(RPSUI_API_VERSION);
        return api &&
                       api->structSize >= sizeof(ApiV1) &&
                       api->apiVersion == RPSUI_API_VERSION &&
                       api->apiFlavor == RPSUI_API_FLAVOR ?
            api :
            nullptr;
    }

    [[nodiscard]] constexpr std::uint64_t featureMask(FeatureV1 feature) noexcept
    {
        return static_cast<std::uint64_t>(feature);
    }

    static_assert(std::is_standard_layout_v<ConsumerRegistrationV1>);
    static_assert(std::is_standard_layout_v<PanelRegistrationV1>);
    static_assert(std::is_standard_layout_v<PanelRenderFrameV1>);
    static_assert(std::is_standard_layout_v<ApiV1>);
}
