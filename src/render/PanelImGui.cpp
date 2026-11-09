#include "PCH.h"

#include "Logger.h"
#include "render/PanelImGui.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include <RPS_UI_Framework/ImGuiBuild.h>

#include <cfloat>
#include <fstream>
#include <map>

namespace rpsui::render::panel_imgui
{
    namespace
    {
        // ImGui's default style is sized for a desktop monitor; a panel is read
        // at arm's length. Consumers set their own size in their setup callback.
        constexpr float kDefaultFontSize = 20.0f;
        // Scroll axes arrive in stick deflection; ImGui wants wheel units.
        constexpr float kWheelUnitsPerSecond = 5.5f;

        // Immutable after prepareFonts(); every atlas borrows it.
        std::vector<char>& defaultFaceBytes() noexcept
        {
            static auto* bytes = new std::vector<char>();
            return *bytes;
        }

        struct ContextDeleter
        {
            void operator()(ImGuiContext* context) const noexcept
            {
                if (!context) return;
                ImGui::SetCurrentContext(context);
                if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplDX11_Shutdown();
                ImGui::DestroyContext(context);
            }
        };

        struct PanelContext
        {
            std::unique_ptr<ImGuiContext, ContextDeleter> context;
            ImFont* defaultFont{ nullptr };
            std::uint64_t deviceGeneration{ 0 };
            std::uint64_t contextGeneration{ 0 };
            bool failed{ false };
        };

        struct State
        {
            // Render mutex owned.
            std::map<std::uint64_t, PanelContext> panels;
            std::uint64_t nextContextGeneration{ 1 };
            // Any thread.
            std::mutex retiredMutex;
            std::vector<std::uint64_t> retired;
        };

        [[nodiscard]] State& state() noexcept
        {
            static auto* value = new State();
            return *value;
        }

        [[nodiscard]] api::framework::ImGuiBinding binding(ImGuiContext* context) noexcept
        {
            api::framework::ImGuiBinding value{};
            value.context = context;
            void* userData = nullptr;
            ImGuiMemAllocFunc alloc = nullptr;
            ImGuiMemFreeFunc free = nullptr;
            ImGui::GetAllocatorFunctions(&alloc, &free, &userData);
            value.alloc = alloc;
            value.free = free;
            value.allocUserData = userData;
            return value;
        }

        [[nodiscard]] ImFont* addDefaultFont(ImGuiIO& io)
        {
            ImFontConfig config{};
            config.SizePixels = kDefaultFontSize;
            auto& bytes = defaultFaceBytes();
            if (!bytes.empty()) {
                config.FontDataOwnedByAtlas = false;
                if (auto* font = io.Fonts->AddFontFromMemoryTTF(bytes.data(), static_cast<int>(bytes.size()),
                        kDefaultFontSize, &config))
                    return font;
            }
            return io.Fonts->AddFontDefaultVector(&config);
        }

        // Builds the context and its renderer. Throws on failure.
        void createContext(PanelContext& panel, ID3D11Device* device, ID3D11DeviceContext* context,
            std::uint64_t deviceGeneration)
        {
            panel.context.reset(ImGui::CreateContext());
            if (!panel.context) throw std::runtime_error("ImGui context creation failed");
            ImGui::SetCurrentContext(panel.context.get());
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
            panel.defaultFont = addDefaultFont(io);
            io.FontDefault = panel.defaultFont;
            ImGui::GetStyle().FontSizeBase = kDefaultFontSize;
            if (!ImGui_ImplDX11_Init(device, context)) throw std::runtime_error("ImGui DX11 renderer initialization failed");
            panel.deviceGeneration = deviceGeneration;
            panel.contextGeneration = state().nextContextGeneration++;
        }

        void runSetup(const PanelContext& panel, const RenderPanelSnapshot& snapshot, ID3D11Device* device)
        {
            if (!snapshot.setupCallback) return;
            api::framework::PanelSetup setup{};
            setup.panelHandle = snapshot.panelHandle;
            setup.deviceGeneration = panel.deviceGeneration;
            setup.contextGeneration = panel.contextGeneration;
            setup.d3dDevice = device;
            setup.imgui = binding(panel.context.get());
            setup.defaultFont = panel.defaultFont;
            snapshot.setupCallback(&setup, snapshot.userData);
            // The consumer's copy of ImGui changed only its own current
            // context; this DLL's is restored for the frame regardless.
            ImGui::SetCurrentContext(panel.context.get());
        }

        [[nodiscard]] PanelContext* ensureContext(ID3D11Device* device, ID3D11DeviceContext* context,
            std::uint64_t deviceGeneration, const RenderPanelSnapshot& snapshot) noexcept
        {
            auto& panel = state().panels[snapshot.panelHandle];
            if (panel.failed) return nullptr;
            if (panel.context && panel.deviceGeneration == deviceGeneration) return &panel;
            try {
                panel.context.reset();
                createContext(panel, device, context, deviceGeneration);
                runSetup(panel, snapshot, device);
                log::info("Panel {} ImGui context ready (device generation {}, context generation {})",
                    snapshot.panelHandle, deviceGeneration, panel.contextGeneration);
                return &panel;
            } catch (const std::exception& error) {
                log::error("Panel {} ImGui context failed: {}", snapshot.panelHandle, error.what());
            } catch (...) {
                log::error("Panel {} ImGui context failed: the consumer's setup callback threw", snapshot.panelHandle);
            }
            panel.context.reset();
            panel.failed = true;
            return nullptr;
        }

        [[nodiscard]] bool finiteNonNegative(float value) noexcept { return std::isfinite(value) && value >= 0.0f; }
    }

    void prepareFonts() noexcept
    {
        try {
            auto& bytes = defaultFaceBytes();
            if (!bytes.empty()) return;
            wchar_t directory[MAX_PATH]{};
            if (!GetWindowsDirectoryW(directory, MAX_PATH)) return;
            const auto path = std::filesystem::path(directory) / "Fonts" / "consola.ttf";
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input) {
                log::warn("Consolas is unavailable; panels use ImGui's built-in face");
                return;
            }
            const auto size = input.tellg();
            if (size <= 0 || size > 16 * 1024 * 1024) return;
            bytes.resize(static_cast<std::size_t>(size));
            input.seekg(0);
            if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) bytes.clear();
        } catch (...) {
            defaultFaceBytes().clear();
        }
    }

    api::framework::ImGuiBuild hostBuild() noexcept { return RPSUI_IMGUI_BUILD(); }

    bool buildMatches(const api::framework::ImGuiBuild& consumer) noexcept
    {
        const auto host = hostBuild();
        return consumer.size >= sizeof(consumer) && consumer.version == api::kRecordVersion &&
            consumer.versionNum == host.versionNum && consumer.wcharSize == host.wcharSize &&
            consumer.ioSize == host.ioSize && consumer.styleSize == host.styleSize &&
            consumer.contextSize == host.contextSize && consumer.drawListSize == host.drawListSize &&
            consumer.fontAtlasSize == host.fontAtlasSize && consumer.fontSize == host.fontSize &&
            consumer.textureDataSize == host.textureDataSize && consumer.drawVertSize == host.drawVertSize &&
            consumer.drawIdxSize == host.drawIdxSize;
    }

    bool drawPanel(ID3D11Device* device, ID3D11DeviceContext* context, std::uint64_t deviceGeneration,
        std::uint64_t frameSequence, const RenderPanelSnapshot& snapshot, ID3D11RenderTargetView* target,
        float deltaSeconds) noexcept
    {
        if (!device || !context || !target || !snapshot.drawCallback) return false;
        auto* panel = ensureContext(device, context, deviceGeneration, snapshot);
        if (!panel) return false;

        ImGui::SetCurrentContext(panel->context.get());
        try {
            const auto pixelWidth = static_cast<float>(snapshot.pixelWidth);
            const auto pixelHeight = static_cast<float>(snapshot.pixelHeight);
            const bool layout = snapshot.layoutWidth > 0.0f && snapshot.layoutHeight > 0.0f;
            const float layoutWidth = layout ? snapshot.layoutWidth : pixelWidth;
            const float layoutHeight = layout ? snapshot.layoutHeight : pixelHeight;
            const float toLayoutX = layoutWidth / pixelWidth;
            const float toLayoutY = layoutHeight / pixelHeight;
            const bool pointerValid = snapshot.pointerValid && std::isfinite(snapshot.pointerPixelX) &&
                std::isfinite(snapshot.pointerPixelY);
            const float pointerX = snapshot.pointerPixelX * toLayoutX;
            const float pointerY = snapshot.pointerPixelY * toLayoutY;

            auto& io = ImGui::GetIO();
            io.DisplaySize = { layoutWidth, layoutHeight };
            io.DisplayFramebufferScale = { pixelWidth / layoutWidth, pixelHeight / layoutHeight };
            io.DeltaTime = std::clamp(finiteNonNegative(deltaSeconds) ? deltaSeconds : 1.0f / 90.0f, 1.0f / 240.0f, 0.1f);
            io.MouseDrawCursor = pointerValid;
            io.AddMousePosEvent(pointerValid ? pointerX : -FLT_MAX, pointerValid ? pointerY : -FLT_MAX);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, pointerValid && snapshot.primaryDown);
            if (pointerValid && std::isfinite(snapshot.scrollAxisX) && std::isfinite(snapshot.scrollAxisY) &&
                (snapshot.scrollAxisX != 0.0f || snapshot.scrollAxisY != 0.0f)) {
                io.AddMouseWheelEvent(snapshot.scrollAxisX * kWheelUnitsPerSecond * io.DeltaTime,
                    snapshot.scrollAxisY * kWheelUnitsPerSecond * io.DeltaTime);
            }

            context->OMSetRenderTargets(1, &target, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = pixelWidth;
            viewport.Height = pixelHeight;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);

            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();

            api::framework::PanelDrawFrame frame{};
            frame.panelHandle = snapshot.panelHandle;
            frame.frameSequence = frameSequence;
            frame.deviceGeneration = panel->deviceGeneration;
            frame.contextGeneration = panel->contextGeneration;
            frame.d3dDevice = device;
            frame.imgui = binding(panel->context.get());
            frame.defaultFont = panel->defaultFont;
            frame.pixelWidth = snapshot.pixelWidth;
            frame.pixelHeight = snapshot.pixelHeight;
            frame.layoutWidth = layoutWidth;
            frame.layoutHeight = layoutHeight;
            frame.deltaSeconds = io.DeltaTime;
            frame.pointerX = pointerValid ? pointerX : 0.0f;
            frame.pointerY = pointerValid ? pointerY : 0.0f;
            frame.scrollAxisX = snapshot.scrollAxisX;
            frame.scrollAxisY = snapshot.scrollAxisY;
            frame.physicalWidth = snapshot.pose.physicalWidth;
            frame.physicalHeight = snapshot.pose.physicalHeight;
            frame.pointerHand = snapshot.pointerHand;
            frame.hoveredResizeHandle = snapshot.hoveredResizeHandle;
            frame.activeResizeHandle = snapshot.activeResizeHandle;
            frame.pointerValid = pointerValid ? 1 : 0;
            frame.primaryDown = snapshot.primaryDown ? 1 : 0;
            frame.backDown = snapshot.backDown ? 1 : 0;
            snapshot.drawCallback(&frame, snapshot.userData);

            // The consumer drew through its own copy of ImGui; this copy's
            // current context is unchanged, but set it again for clarity of
            // ownership before rendering.
            ImGui::SetCurrentContext(panel->context.get());
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            return true;
        } catch (...) {
            log::error("Panel {} draw callback threw across its boundary; its context is rebuilt next frame",
                snapshot.panelHandle);
            // The frame may be half open; a fresh context is the only clean state.
            panel->context.reset();
            return false;
        }
    }

    void retire(std::uint64_t panelHandle) noexcept
    {
        try {
            auto& value = state();
            std::scoped_lock lock(value.retiredMutex);
            value.retired.push_back(panelHandle);
        } catch (...) {
            log::error("Panel {} ImGui context could not be queued for release", panelHandle);
        }
    }

    void collectRetired() noexcept
    {
        auto& value = state();
        std::vector<std::uint64_t> retired;
        {
            std::scoped_lock lock(value.retiredMutex);
            retired.swap(value.retired);
        }
        for (const auto handle : retired) value.panels.erase(handle);
    }

    void releaseAll() noexcept
    {
        collectRetired();
        state().panels.clear();
    }
}
