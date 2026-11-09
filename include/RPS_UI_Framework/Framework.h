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
        // Game units. Any positive height: the layout is scaled onto the
        // panel on each axis, so a height off the layout's shape stretches it.
        float physicalWidth{ 112.5f };
        float physicalHeight{ 70.3125f };
    };

    /*
     * RPS UI draws every panel with Dear ImGui. The host owns the panel's
     * context, its DX11 renderer, font atlas, input and frame: it feeds the
     * pointer, begins the frame, calls the consumer's draw callback and renders
     * the result. The consumer only issues ImGui calls, from its own copy of
     * Dear ImGui built as RPS_UI_Framework/ImGuiBuild.h describes.
     */

    // The consumer's Dear ImGui build (RPSUI_IMGUI_BUILD()). registerPanel
    // refuses a panel whose build differs from the host's.
    struct ImGuiBuild
    {
        std::uint32_t size{ sizeof(ImGuiBuild) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t versionNum{ 0 };
        std::uint32_t wcharSize{ 0 };
        std::uint32_t ioSize{ 0 };
        std::uint32_t styleSize{ 0 };
        std::uint32_t contextSize{ 0 };
        std::uint32_t drawListSize{ 0 };
        std::uint32_t fontAtlasSize{ 0 };
        std::uint32_t fontSize{ 0 };
        std::uint32_t textureDataSize{ 0 };
        std::uint32_t drawVertSize{ 0 };
        std::uint32_t drawIdxSize{ 0 };
        std::uint32_t reserved{ 0 };
    };

    // Dear ImGui's allocator signatures (ImGuiMemAllocFunc, ImGuiMemFreeFunc).
    using ImGuiAllocFunction = void* (*)(std::size_t size, void* userData);
    using ImGuiFreeFunction = void (*)(void* pointer, void* userData);

    // The host's context and allocator. Bind both with RPSUI_IMGUI_BIND before
    // any ImGui call; they are valid only for the callback that received them.
    struct ImGuiBinding
    {
        void* context{ nullptr };      // ImGuiContext*
        ImGuiAllocFunction alloc{ nullptr };
        ImGuiFreeFunction free{ nullptr };
        void* allocUserData{ nullptr };
    };

    // A colour: straight alpha, each channel 0 to 1, as ImGui's ImVec4.
    struct ThemeColor
    {
        float r{ 0.0f };
        float g{ 0.0f };
        float b{ 0.0f };
        float a{ 1.0f };
    };

    /*
     * The player's theme: RPS_UI.toml names it and Themes\<name>.toml holds
     * it. Before a panel's setup callback the host sets the context's ImGui
     * style from it and adds its four faces, so plain ImGui widgets follow it.
     * A consumer draws its own shapes with these roles rather than fixed
     * colours. Sizes are in layout units.
     */
    struct Theme
    {
        std::uint32_t size{ sizeof(Theme) };
        std::uint32_t version{ kRecordVersion };
        // Changes with every theme change. The host then rebuilds every
        // panel's context, so setup callbacks run again with the new theme.
        std::uint64_t revision{ 0 };
        char name[64]{};
        ThemeColor background{};   // A panel's body
        ThemeColor surface{};      // Idle controls, headers and rails
        ThemeColor hover{};        // Under the pointer
        ThemeColor pressed{};      // While pressed or dragged
        ThemeColor selected{};     // The open page, the current choice
        ThemeColor border{};       // Control outlines
        ThemeColor divider{};      // Rules and separators
        ThemeColor text{};
        ThemeColor textMuted{};    // Help, units and secondary text
        ThemeColor textDisabled{};
        ThemeColor accent{};       // Solid fills of chosen things, links, the cursor
        ThemeColor onAccent{};     // Text and marks on an accent fill
        ThemeColor focus{};        // The control the stick adjusts
        ThemeColor pending{};      // Changed and not yet saved
        ThemeColor invalid{};      // Refused values and errors
        ThemeColor success{};
        ThemeColor scrim{};        // Behind a modal
        float fontSize{ 20.0f };      // Body text
        float titleFontSize{ 27.0f };
        float smallFontSize{ 17.0f }; // Nothing smaller is drawn
        float targetHeight{ 40.0f };  // Anything pressable is at least this tall
        float padding{ 10.0f };
        float spacing{ 8.0f };
        float rounding{ 0.0f };
        float borderWidth{ 1.0f };
    };

    // The theme's faces, indexes into PanelSetup::fonts and PanelDrawFrame::fonts.
    enum class FontRole : std::uint32_t
    {
        Interface = 0, // Labels and controls; io.FontDefault
        Strong = 1,    // Headings and emphasis
        Mono = 2,      // Numbers, code and console text
        Prose = 3,     // Long reading text: dialogue, descriptions
    };
    inline constexpr std::uint32_t kFontRoleCount = 4;

    // Once per context, before its first frame: on registration, after a device
    // change, after a theme change and after the host rebuilt the context. The
    // theme's style and faces are already in place; add your own fonts, adjust
    // the style and create textures here. Everything made in the context
    // belongs to it and ends with it; contextGeneration tells the consumer when
    // that happened.
    struct PanelSetup
    {
        std::uint32_t size{ sizeof(PanelSetup) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t panelHandle{ 0 };
        std::uint64_t deviceGeneration{ 0 };
        std::uint64_t contextGeneration{ 0 };
        void* d3dDevice{ nullptr };    // ID3D11Device*, for the consumer's own textures
        ImGuiBinding imgui{};
        const Theme* theme{ nullptr }; // Valid for the callback
        void* fonts[kFontRoleCount]{}; // ImFont* by FontRole, valid for the context
    };

    /*
     * One frame, between the host's NewFrame and Render. Coordinates are in
     * layout units (the registration's layout size), the same units as
     * ImGui's DisplaySize and MousePos. pixelWidth and pixelHeight are the
     * texture's size this frame, which follows the panel's size in the world. The host has
     * already fed the pointer position, the primary button and the scroll axes
     * to ImGui and turned the software cursor on while the pointer is valid; a
     * consumer may change io.MouseDrawCursor during the frame.
     */
    struct PanelDrawFrame
    {
        std::uint32_t size{ sizeof(PanelDrawFrame) };
        std::uint32_t version{ kRecordVersion };
        std::uint64_t panelHandle{ 0 };
        std::uint64_t frameSequence{ 0 };
        std::uint64_t deviceGeneration{ 0 };
        std::uint64_t contextGeneration{ 0 };
        void* d3dDevice{ nullptr };    // ID3D11Device*, for the consumer's own textures
        ImGuiBinding imgui{};
        const Theme* theme{ nullptr }; // Valid for the callback
        void* fonts[kFontRoleCount]{}; // ImFont* by FontRole
        std::uint32_t pixelWidth{ 0 };
        std::uint32_t pixelHeight{ 0 };
        float layoutWidth{ 0.0f };
        float layoutHeight{ 0.0f };
        float deltaSeconds{ 0.0f };
        float pointerX{ 0.0f };
        float pointerY{ 0.0f };
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
        std::uint8_t reservedBytes[1]{};
    };

    using PanelSetupCallback = void(RPSUI_CALL*)(const PanelSetup* setup, void* userData) noexcept;
    using PanelDrawCallback = void(RPSUI_CALL*)(const PanelDrawFrame* frame, void* userData) noexcept;

    struct PanelRegistration
    {
        std::uint32_t size{ sizeof(PanelRegistration) };
        std::uint32_t version{ kRecordVersion };
        char panelId[64]{};
        char displayName[96]{};
        // The panel's drawing size in ImGui units (DisplaySize), and its
        // default shape. The host picks the texture's resolution from the
        // panel's size in the world and the player's sharpness setting, so
        // the layout stays the same however large the panel is shown.
        float layoutWidth{ 1440.0f };
        float layoutHeight{ 900.0f };
        float defaultPhysicalWidth{ 112.5f };
        float minimumPhysicalWidth{ 72.0f };
        float maximumPhysicalWidth{ 180.0f };
        std::int32_t sortOrder{ 0 };
        std::uint32_t flags{ 0 }; // PanelFlag bits. Zero is a rectangular opaque panel.
        std::uint32_t reserved{ 0 };
        ImGuiBuild imgui{};
        PanelSetupCallback setupCallback{ nullptr }; // Optional.
        PanelDrawCallback drawCallback{ nullptr };
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
    static_assert(std::is_standard_layout_v<ImGuiBuild> && sizeof(ImGuiBuild) == 56);
    static_assert(std::is_standard_layout_v<ImGuiBinding> && sizeof(ImGuiBinding) == 32);
    static_assert(std::is_standard_layout_v<ThemeColor> && sizeof(ThemeColor) == 16);
    static_assert(std::is_standard_layout_v<Theme> && sizeof(Theme) == 384);
    static_assert(std::is_standard_layout_v<PanelSetup> && sizeof(PanelSetup) == 112);
    static_assert(std::is_standard_layout_v<PanelDrawFrame> && sizeof(PanelDrawFrame) == 184);
    static_assert(std::is_standard_layout_v<PanelRegistration> && sizeof(PanelRegistration) == 280);
    static_assert(std::is_standard_layout_v<PanelPresentation> && sizeof(PanelPresentation) == 80);
    static_assert(std::is_standard_layout_v<PanelState> && sizeof(PanelState) == 88);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 64);
}
#pragma pack(pop)
