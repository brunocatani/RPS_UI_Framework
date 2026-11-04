#pragma once
#include "Abi.h"

// World panels: consumer registration, panel registration and presentation,
// and panel state. Other interfaces take the owner token issued here.
#pragma pack(push, 8)
namespace rpsui::api::framework
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::Framework;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    enum class PanelFlag : std::uint32_t
    {
        Transparent = 1u << 0,
        FixedSize = 1u << 1,
        CircularInput = 1u << 2,
        // Opt in to raw stick handoff and grip-back. Hold-based wheels leave this unset.
        ConfigNavigation = 1u << 3,
    };

    [[nodiscard]] constexpr bool hasPanelFlag(std::uint32_t flags, PanelFlag flag) noexcept
    {
        return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }

    [[nodiscard]] constexpr bool panelContains(std::uint32_t flags, float u, float v) noexcept
    {
        if (!(u >= 0 && u <= 1 && v >= 0 && v <= 1)) return false;
        if (!hasPanelFlag(flags, PanelFlag::CircularInput)) return true;
        const float x = u * 2 - 1, y = v * 2 - 1;
        return x * x + y * y <= 1;
    }

    enum class PhysicalHand : std::uint32_t
    {
        None = 0,
        Left = 1,
        Right = 2,
    };

    enum class ResizeHandle : std::uint32_t
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

    struct ConsumerRegistration
    {
        std::uint32_t size{ sizeof(ConsumerRegistration) };
        std::uint32_t version{ kRecordVersion };
        char consumerId[64]{};
        char displayName[96]{};
    };

    struct ConsumerHandle
    {
        std::uint32_t size{ sizeof(ConsumerHandle) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t ownerToken{ 0 };
    };

    struct PanelPose
    {
        float center[3]{};
        float right[3]{ 1.0f, 0.0f, 0.0f };
        float up[3]{ 0.0f, 0.0f, 1.0f };
        float front[3]{ 0.0f, -1.0f, 0.0f };
        float physicalWidth{ 112.5f };
        float physicalHeight{ 70.3125f };
    };

    struct PanelRenderFrame
    {
        std::uint32_t size{ sizeof(PanelRenderFrame) };
        std::uint32_t version{ kRecordVersion };
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
        PhysicalHand pointerHand{ PhysicalHand::None };
        ResizeHandle hoveredResizeHandle{ ResizeHandle::None };
        ResizeHandle activeResizeHandle{ ResizeHandle::None };
        std::uint8_t pointerValid{ 0 };
        std::uint8_t primaryDown{ 0 };
        std::uint8_t backDown{ 0 }; // Gated physical grip of the pointing hand; derive edges locally.
        std::uint8_t reservedBytes[5]{};
    };

    using PanelRenderCallback = void(RPSUI_CALL*)(const PanelRenderFrame* frame, void* userData) noexcept;

    struct PanelRegistration
    {
        std::uint32_t size{ sizeof(PanelRegistration) };
        std::uint32_t version{ kRecordVersion };
        char panelId[64]{};
        char displayName[96]{};
        std::uint32_t pixelWidth{ 1440 };
        std::uint32_t pixelHeight{ 900 };
        float defaultPhysicalWidth{ 112.5f };
        float minimumPhysicalWidth{ 72.0f };
        float maximumPhysicalWidth{ 180.0f };
        std::int32_t sortOrder{ 0 };
        std::uint32_t flags{ 0 }; // PanelFlag bits. Zero is a rectangular opaque panel.
        std::uint32_t reserved{ 0 };
        PanelRenderCallback renderCallback{ nullptr };
        void* userData{ nullptr };
    };

    struct PanelPresentation
    {
        std::uint32_t size{ sizeof(PanelPresentation) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t sequence{ 0 };
        PanelPose pose{};
        std::uint8_t open{ 0 };
        std::uint8_t reservedBytes[7]{};
    };

    struct PanelState
    {
        std::uint32_t size{ sizeof(PanelState) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t sequence{ 0 };
        PanelPose pose{};
        PhysicalHand pointerHand{ PhysicalHand::None };
        ResizeHandle hoveredResizeHandle{ ResizeHandle::None };
        ResizeHandle activeResizeHandle{ ResizeHandle::None };
        std::uint8_t open{ 0 };
        std::uint8_t pointerValid{ 0 };
        std::uint8_t primaryDown{ 0 };
        std::uint8_t reservedBytes[1]{};
    };

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        bool(RPSUI_CALL* isFrameworkReady)() noexcept {};
        Result(RPSUI_CALL* registerConsumer)(const ConsumerRegistration* registration, ConsumerHandle* outHandle) noexcept {};
        Result(RPSUI_CALL* unregisterConsumer)(std::uint64_t ownerToken) noexcept {};
        Result(RPSUI_CALL* registerPanel)(std::uint64_t ownerToken, const PanelRegistration* registration,
            std::uint64_t* outPanelHandle) noexcept {};
        Result(RPSUI_CALL* unregisterPanel)(std::uint64_t ownerToken, std::uint64_t panelHandle) noexcept {};
        Result(RPSUI_CALL* submitPanelPresentation)(std::uint64_t ownerToken, std::uint64_t panelHandle,
            const PanelPresentation* presentation) noexcept {};
        Result(RPSUI_CALL* resetPanelSize)(std::uint64_t ownerToken, std::uint64_t panelHandle) noexcept {};
        Result(RPSUI_CALL* getPanelState)(std::uint64_t ownerToken, std::uint64_t panelHandle, PanelState* outState) noexcept {};
    };

    static_assert(std::is_standard_layout_v<ConsumerRegistration> && sizeof(ConsumerRegistration) == 168);
    static_assert(std::is_standard_layout_v<ConsumerHandle> && sizeof(ConsumerHandle) == 16);
    static_assert(std::is_standard_layout_v<PanelPose> && sizeof(PanelPose) == 56);
    static_assert(std::is_standard_layout_v<PanelRenderFrame> && sizeof(PanelRenderFrame) == 112);
    static_assert(std::is_standard_layout_v<PanelRegistration> && sizeof(PanelRegistration) == 216);
    static_assert(std::is_standard_layout_v<PanelPresentation> && sizeof(PanelPresentation) == 80);
    static_assert(std::is_standard_layout_v<PanelState> && sizeof(PanelState) == 88);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 64);
}
#pragma pack(pop)
