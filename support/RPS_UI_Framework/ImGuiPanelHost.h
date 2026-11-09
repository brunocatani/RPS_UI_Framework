#pragma once

// How RPS UI Framework runs a panel's Dear ImGui frame, for the host itself and
// for a consumer's desktop preview or test that stands in for the host.
//
// This is not part of the runtime API in include/: it includes Dear ImGui and
// its DX11 backend, so only code that compiles Dear ImGui with the shared
// configuration (RPS_UI_Framework/ImGuiBuild.h) and its DX11 backend may
// include it. In the game, RPS UI Framework is the only host; consumers never
// render panels themselves.

#include <RPS_UI_Framework/Framework.h>
#include <RPS_UI_Framework/ImGuiBuild.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include <d3d11.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace rpsui::imgui_host
{
    // The default face's size. ImGui's own default is sized for a desktop
    // monitor; a panel is read at arm's length.
    inline constexpr float kDefaultFontSize = 20.0f;
    // Scroll axes arrive in stick deflection; ImGui wants wheel units.
    inline constexpr float kWheelUnitsPerSecond = 5.5f;

    // One frame's host-side input for a panel. The pointer is in layout
    // units; the pixel size is the render target's.
    struct PanelInput
    {
        std::uint32_t pixelWidth{ 0 };
        std::uint32_t pixelHeight{ 0 };
        float layoutWidth{ 0.0f };
        float layoutHeight{ 0.0f };
        float deltaSeconds{ 1.0f / 90.0f };
        float pointerX{ 0.0f };
        float pointerY{ 0.0f };
        float scrollAxisX{ 0.0f };
        float scrollAxisY{ 0.0f };
        bool pointerValid{ false };
        bool primaryDown{ false };
        bool feedPointer{ true };   // False for passive panels.
    };

    // The input as fed to ImGui, in layout units.
    struct FedInput
    {
        float layoutWidth{ 0.0f };
        float layoutHeight{ 0.0f };
        float deltaSeconds{ 0.0f };
        float pointerX{ 0.0f };
        float pointerY{ 0.0f };
        bool pointerValid{ false };
    };

    [[nodiscard]] inline api::framework::ImGuiBinding currentBinding() noexcept
    {
        api::framework::ImGuiBinding value{};
        value.context = ImGui::GetCurrentContext();
        ImGuiMemAllocFunc alloc = nullptr;
        ImGuiMemFreeFunc free = nullptr;
        ImGui::GetAllocatorFunctions(&alloc, &free, &value.allocUserData);
        value.alloc = alloc;
        value.free = free;
        return value;
    }

    // A new context's settings. The context must be current.
    inline void configureContext() noexcept
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        // One input snapshot per rendered frame, applied together. Trickling
        // splits pointer motion and the button across frames and builds a
        // backlog while a VR hand moves.
        io.ConfigInputTrickleEventQueue = false;
        // A consumer's unbalanced Begin/End must not take the game down.
        io.ConfigErrorRecovery = true;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableDebugLog = false;
        io.ConfigErrorRecoveryEnableTooltip = false;
        // style.FontSizeBase stays unset: the first frame takes io.FontDefault's
        // size, so a consumer that installs its own default font in its setup
        // callback gets that font's size.
    }

    // The default text face from face bytes the caller keeps alive for the
    // atlas's lifetime; ImGui's built-in face when there are none.
    [[nodiscard]] inline ImFont* addDefaultFont(std::span<const char> faceBytes)
    {
        auto& io = ImGui::GetIO();
        ImFontConfig config{};
        config.SizePixels = kDefaultFontSize;
        ImFont* font = nullptr;
        if (!faceBytes.empty()) {
            config.FontDataOwnedByAtlas = false;
            font = io.Fonts->AddFontFromMemoryTTF(const_cast<char*>(faceBytes.data()),
                static_cast<int>(faceBytes.size()), kDefaultFontSize, &config);
        }
        if (!font) font = io.Fonts->AddFontDefaultVector(&config);
        io.FontDefault = font;
        return font;
    }

    // Feeds one frame's display and input to the current context. Call before
    // the renderer's NewFrame and ImGui::NewFrame.
    [[nodiscard]] inline FedInput feedFrame(const PanelInput& input) noexcept
    {
        FedInput fed{};
        const auto pixelWidth = static_cast<float>(input.pixelWidth);
        const auto pixelHeight = static_cast<float>(input.pixelHeight);
        fed.layoutWidth = input.layoutWidth > 0.0f ? input.layoutWidth : pixelWidth;
        fed.layoutHeight = input.layoutHeight > 0.0f ? input.layoutHeight : pixelHeight;
        fed.pointerValid = input.feedPointer && input.pointerValid && std::isfinite(input.pointerX) &&
            std::isfinite(input.pointerY);
        fed.pointerX = fed.pointerValid ? input.pointerX : 0.0f;
        fed.pointerY = fed.pointerValid ? input.pointerY : 0.0f;
        const bool deltaValid = std::isfinite(input.deltaSeconds) && input.deltaSeconds >= 0.0f;
        fed.deltaSeconds = std::clamp(deltaValid ? input.deltaSeconds : 1.0f / 90.0f, 1.0f / 240.0f, 0.1f);

        auto& io = ImGui::GetIO();
        io.DisplaySize = { fed.layoutWidth, fed.layoutHeight };
        io.DisplayFramebufferScale = { pixelWidth / fed.layoutWidth, pixelHeight / fed.layoutHeight };
        io.DeltaTime = fed.deltaSeconds;
        io.MouseDrawCursor = fed.pointerValid;
        io.AddMousePosEvent(fed.pointerValid ? fed.pointerX : -FLT_MAX, fed.pointerValid ? fed.pointerY : -FLT_MAX);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, fed.pointerValid && input.primaryDown);
        if (fed.pointerValid && std::isfinite(input.scrollAxisX) && std::isfinite(input.scrollAxisY) &&
            (input.scrollAxisX != 0.0f || input.scrollAxisY != 0.0f)) {
            io.AddMouseWheelEvent(input.scrollAxisX * kWheelUnitsPerSecond * io.DeltaTime,
                input.scrollAxisY * kWheelUnitsPerSecond * io.DeltaTime);
        }
        return fed;
    }

    // Binds target with a full-size viewport, then begins the frame.
    inline void beginFrame(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, std::uint32_t pixelWidth,
        std::uint32_t pixelHeight)
    {
        context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(pixelWidth);
        viewport.Height = static_cast<float>(pixelHeight);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
    }

    inline void endFrame()
    {
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    /*
     * A one-panel host for a consumer's desktop preview or test, running the
     * consumer's own setup and draw callbacks exactly as RPS UI Framework does.
     * Owns its context and DX11 renderer. Single-threaded; it leaves its
     * context current after each call.
     */
    class LocalPanelHost
    {
    public:
        LocalPanelHost(ID3D11Device* device, ID3D11DeviceContext* context, const api::framework::PanelRegistration& panel,
            std::span<const char> defaultFaceBytes) :
            device_(device), context_(context), panel_(panel)
        {
            imgui_ = ImGui::CreateContext();
            if (!imgui_) throw std::runtime_error("ImGui context creation failed");
            ImGui::SetCurrentContext(imgui_);
            configureContext();
            defaultFont_ = addDefaultFont(defaultFaceBytes);
            if (!ImGui_ImplDX11_Init(device_, context_)) {
                ImGui::DestroyContext(imgui_);
                throw std::runtime_error("ImGui DX11 renderer initialization failed");
            }
            if (panel_.setupCallback) {
                api::framework::PanelSetup setup{};
                setup.panelHandle = 1;
                setup.deviceGeneration = 1;
                setup.contextGeneration = 1;
                setup.d3dDevice = device_;
                setup.imgui = currentBinding();
                setup.defaultFont = defaultFont_;
                panel_.setupCallback(&setup, panel_.userData);
                ImGui::SetCurrentContext(imgui_);
            }
        }

        ~LocalPanelHost()
        {
            ImGui::SetCurrentContext(imgui_);
            ImGui_ImplDX11_Shutdown();
            ImGui::DestroyContext(imgui_);
        }

        LocalPanelHost(const LocalPanelHost&) = delete;
        LocalPanelHost& operator=(const LocalPanelHost&) = delete;

        // One frame into target, which the caller cleared. input.pixelWidth and
        // pixelHeight are the target's size, the layout size when left zero.
        void frame(ID3D11RenderTargetView* target, PanelInput input,
            api::framework::PhysicalHand pointerHand = api::framework::PhysicalHand::Right, bool backDown = false)
        {
            ImGui::SetCurrentContext(imgui_);
            if (!input.pixelWidth || !input.pixelHeight) {
                input.pixelWidth = static_cast<std::uint32_t>(std::ceil(panel_.layoutWidth));
                input.pixelHeight = static_cast<std::uint32_t>(std::ceil(panel_.layoutHeight));
            }
            input.layoutWidth = panel_.layoutWidth;
            input.layoutHeight = panel_.layoutHeight;
            const auto fed = feedFrame(input);
            beginFrame(context_, target, input.pixelWidth, input.pixelHeight);
            api::framework::PanelDrawFrame frame{};
            frame.panelHandle = 1;
            frame.frameSequence = ++sequence_;
            frame.deviceGeneration = 1;
            frame.contextGeneration = 1;
            frame.d3dDevice = device_;
            frame.imgui = currentBinding();
            frame.defaultFont = defaultFont_;
            frame.pixelWidth = input.pixelWidth;
            frame.pixelHeight = input.pixelHeight;
            frame.layoutWidth = fed.layoutWidth;
            frame.layoutHeight = fed.layoutHeight;
            frame.deltaSeconds = fed.deltaSeconds;
            frame.pointerX = fed.pointerX;
            frame.pointerY = fed.pointerY;
            frame.scrollAxisX = input.scrollAxisX;
            frame.scrollAxisY = input.scrollAxisY;
            frame.pointerHand = fed.pointerValid ? pointerHand : api::framework::PhysicalHand::None;
            frame.pointerValid = fed.pointerValid ? 1 : 0;
            frame.primaryDown = input.primaryDown ? 1 : 0;
            frame.backDown = fed.pointerValid && backDown ? 1 : 0;
            panel_.drawCallback(&frame, panel_.userData);
            ImGui::SetCurrentContext(imgui_);
            endFrame();
        }

        [[nodiscard]] ImGuiContext* context() const noexcept { return imgui_; }

    private:
        ID3D11Device* device_{ nullptr };
        ID3D11DeviceContext* context_{ nullptr };
        api::framework::PanelRegistration panel_{};
        ImGuiContext* imgui_{ nullptr };
        ImFont* defaultFont_{ nullptr };
        std::uint64_t sequence_{ 0 };
    };
}
