#include "PCH.h"

#include "Logger.h"
#include "render/PanelImGui.h"

#include <RPS_UI_Framework/ImGuiPanelHost.h>

#include <cfloat>
#include <fstream>
#include <map>

namespace rpsui::render::panel_imgui
{
    namespace
    {
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

        // Builds the context and its renderer. Throws on failure.
        void createContext(PanelContext& panel, ID3D11Device* device, ID3D11DeviceContext* context,
            std::uint64_t deviceGeneration)
        {
            panel.context.reset(ImGui::CreateContext());
            if (!panel.context) throw std::runtime_error("ImGui context creation failed");
            ImGui::SetCurrentContext(panel.context.get());
            imgui_host::configureContext();
            panel.defaultFont = imgui_host::addDefaultFont(defaultFaceBytes());
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
            setup.imgui = imgui_host::currentBinding();
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
            imgui_host::PanelInput input{};
            input.pixelWidth = snapshot.pixelWidth;
            input.pixelHeight = snapshot.pixelHeight;
            input.layoutWidth = snapshot.layoutWidth;
            input.layoutHeight = snapshot.layoutHeight;
            input.deltaSeconds = deltaSeconds;
            input.pointerPixelX = snapshot.pointerPixelX;
            input.pointerPixelY = snapshot.pointerPixelY;
            input.scrollAxisX = snapshot.scrollAxisX;
            input.scrollAxisY = snapshot.scrollAxisY;
            input.pointerValid = snapshot.pointerValid;
            input.primaryDown = snapshot.primaryDown;
            const auto fed = imgui_host::feedFrame(input);
            imgui_host::beginFrame(context, target, snapshot.pixelWidth, snapshot.pixelHeight);

            api::framework::PanelDrawFrame frame{};
            frame.panelHandle = snapshot.panelHandle;
            frame.frameSequence = frameSequence;
            frame.deviceGeneration = panel->deviceGeneration;
            frame.contextGeneration = panel->contextGeneration;
            frame.d3dDevice = device;
            frame.imgui = imgui_host::currentBinding();
            frame.defaultFont = panel->defaultFont;
            frame.pixelWidth = snapshot.pixelWidth;
            frame.pixelHeight = snapshot.pixelHeight;
            frame.layoutWidth = fed.layoutWidth;
            frame.layoutHeight = fed.layoutHeight;
            frame.deltaSeconds = fed.deltaSeconds;
            frame.pointerX = fed.pointerX;
            frame.pointerY = fed.pointerY;
            frame.scrollAxisX = snapshot.scrollAxisX;
            frame.scrollAxisY = snapshot.scrollAxisY;
            frame.physicalWidth = snapshot.pose.physicalWidth;
            frame.physicalHeight = snapshot.pose.physicalHeight;
            frame.pointerHand = snapshot.pointerHand;
            frame.hoveredResizeHandle = snapshot.hoveredResizeHandle;
            frame.activeResizeHandle = snapshot.activeResizeHandle;
            frame.pointerValid = fed.pointerValid ? 1 : 0;
            frame.primaryDown = snapshot.primaryDown ? 1 : 0;
            frame.backDown = snapshot.backDown ? 1 : 0;
            snapshot.drawCallback(&frame, snapshot.userData);

            // The consumer drew through its own copy of ImGui, whose current
            // context is its own; this copy's is restored before rendering.
            ImGui::SetCurrentContext(panel->context.get());
            imgui_host::endFrame();
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
