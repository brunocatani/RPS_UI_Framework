#include "PCH.h"

#include "Haptics.h"
#include "Logger.h"
#include "UiSettings.h"
#include "render/PanelImGui.h"

#include <RPS_UI_Framework/ImGuiPanelHost.h>

#include <imgui_internal.h>

#include <cfloat>
#include <map>

namespace rpsui::render::panel_imgui
{
    namespace
    {
        // Before the settings start, and in tests: the built-in theme with ImGui's own face.
        [[nodiscard]] const std::shared_ptr<const theme_store::ThemeData>& fallbackTheme() noexcept
        {
            static auto* value = new std::shared_ptr<const theme_store::ThemeData>(
                std::make_shared<theme_store::ThemeData>(theme_store::ThemeData{ .theme = theme_defaults::robco() }));
            return *value;
        }

        [[nodiscard]] std::shared_ptr<const theme_store::ThemeData> currentTheme() noexcept
        {
            auto theme = settings::current()->theme;
            return theme ? theme : fallbackTheme();
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
            // Declared first so it outlives the context: the atlas borrows its face bytes.
            std::shared_ptr<const theme_store::ThemeData> theme;
            std::unique_ptr<ImGuiContext, ContextDeleter> context;
            imgui_host::FontSet fonts{};
            std::uint64_t deviceGeneration{ 0 };
            std::uint64_t contextGeneration{ 0 };
            // A failed panel waits for the next theme or device.
            std::uint64_t failedThemeRevision{ 0 };
            std::uint64_t failedDeviceGeneration{ 0 };
            // The last Back the consumer was shown; kept across context rebuilds.
            std::uint64_t backSequence{ 0 };
            bool failed{ false };
        };

        // A press that made a control active this frame, not a press on the
        // window's empty space.
        [[nodiscard]] bool pressActivatedControl(const ImGuiContext& g) noexcept
        {
            return g.ActiveIdIsJustActivated && g.ActiveId != 0 && g.ActiveIdSource == ImGuiInputSource_Mouse &&
                !(g.ActiveIdWindow && g.ActiveId == g.ActiveIdWindow->MoveId);
        }

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
            std::uint64_t deviceGeneration, std::shared_ptr<const theme_store::ThemeData> theme)
        {
            panel.context.reset();
            panel.theme = std::move(theme);
            panel.context.reset(ImGui::CreateContext());
            if (!panel.context) throw std::runtime_error("ImGui context creation failed");
            ImGui::SetCurrentContext(panel.context.get());
            imgui_host::configureContext();
            imgui_host::applyTheme(panel.theme->theme);
            imgui_host::FaceSet faces{};
            for (std::size_t role = 0; role < faces.size(); ++role)
                if (panel.theme->faces[role]) faces[role] = *panel.theme->faces[role];
            panel.fonts = imgui_host::addThemeFonts(panel.theme->theme, faces);
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
            setup.theme = &panel.theme->theme;
            for (std::size_t role = 0; role < panel.fonts.size(); ++role) setup.fonts[role] = panel.fonts[role];
            snapshot.setupCallback(&setup, snapshot.userData);
            // The consumer's copy of ImGui changed only its own current
            // context; this DLL's is restored for the frame regardless.
            ImGui::SetCurrentContext(panel.context.get());
        }

        [[nodiscard]] PanelContext* ensureContext(ID3D11Device* device, ID3D11DeviceContext* context,
            std::uint64_t deviceGeneration, const RenderPanelSnapshot& snapshot) noexcept
        {
            auto& panel = state().panels[snapshot.panelHandle];
            auto theme = currentTheme();
            const auto themeRevision = theme->theme.revision;
            if (panel.failed) {
                if (panel.failedThemeRevision == themeRevision && panel.failedDeviceGeneration == deviceGeneration) return nullptr;
                panel.failed = false;
            }
            if (panel.context && panel.deviceGeneration == deviceGeneration && panel.theme->theme.revision == themeRevision)
                return &panel;
            try {
                createContext(panel, device, context, deviceGeneration, std::move(theme));
                runSetup(panel, snapshot, device);
                log::info("Panel {} ImGui context ready (device generation {}, context generation {}, theme '{}')",
                    snapshot.panelHandle, deviceGeneration, panel.contextGeneration, panel.theme->theme.name);
                return &panel;
            } catch (const std::exception& error) {
                log::error("Panel {} ImGui context failed: {}", snapshot.panelHandle, error.what());
            } catch (...) {
                log::error("Panel {} ImGui context failed: the consumer's setup callback threw", snapshot.panelHandle);
            }
            panel.context.reset();
            panel.failed = true;
            panel.failedThemeRevision = themeRevision;
            panel.failedDeviceGeneration = deviceGeneration;
            return nullptr;
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
        std::uint32_t pixelWidth, std::uint32_t pixelHeight, float deltaSeconds) noexcept
    {
        if (!device || !context || !target || !snapshot.drawCallback) return false;
        auto* panel = ensureContext(device, context, deviceGeneration, snapshot);
        if (!panel) return false;

        ImGui::SetCurrentContext(panel->context.get());
        try {
            imgui_host::PanelInput input{};
            input.pixelWidth = pixelWidth;
            input.pixelHeight = pixelHeight;
            input.layoutWidth = snapshot.layoutWidth;
            input.layoutHeight = snapshot.layoutHeight;
            input.deltaSeconds = deltaSeconds;
            input.pointerX = snapshot.pointerX;
            input.pointerY = snapshot.pointerY;
            input.scrollAxisX = snapshot.scrollAxisX;
            input.scrollAxisY = snapshot.scrollAxisY;
            input.pointerValid = snapshot.pointerValid;
            input.primaryDown = snapshot.primaryDown;
            const auto fed = imgui_host::feedFrame(input);
            imgui_host::beginFrame(context, target, pixelWidth, pixelHeight);

            api::framework::PanelDrawFrame frame{};
            frame.panelHandle = snapshot.panelHandle;
            frame.frameSequence = frameSequence;
            frame.deviceGeneration = panel->deviceGeneration;
            frame.contextGeneration = panel->contextGeneration;
            frame.d3dDevice = device;
            frame.imgui = imgui_host::currentBinding();
            frame.theme = &panel->theme->theme;
            for (std::size_t role = 0; role < panel->fonts.size(); ++role) frame.fonts[role] = panel->fonts[role];
            frame.pixelWidth = pixelWidth;
            frame.pixelHeight = pixelHeight;
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
            // Back is an event: shown on exactly one frame however many input
            // ticks passed between frames.
            frame.backPressed = snapshot.backSequence != panel->backSequence ? 1 : 0;
            panel->backSequence = snapshot.backSequence;
            snapshot.drawCallback(&frame, snapshot.userData);

            // The consumer drew through its own copy of ImGui, whose current
            // context is its own; this copy's is restored before rendering.
            ImGui::SetCurrentContext(panel->context.get());
            if (snapshot.primaryDown && pressActivatedControl(*panel->context))
                haptics::request(snapshot.pointerHand, haptics::kPressTick);
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
