#include "PCH.h"

#include "ConfiguratorRuntime.h"

#include "ConsoleService.h"
#include "ConsoleTab.h"
#include "ExternalPages.h"
#include "OnScreenKeyboard.h"
#include "PanelResizePolicy.h"
#include "SpawnBrowser.h"
#include "render/FrameworkPanelRenderer.h"
#include "render/NativeRenderer.h"
#include <imgui_internal.h>
#include "render/ConfigUi.h"


namespace rpsui::settings_menu
{
    namespace
    {

        constexpr std::size_t kRuntimeActionQueueCapacity = 32;

        enum class ConfiguratorTab : std::uint8_t
        {
            Spawn,
            Console,
            External, // A page another mod added; s_externalPage names it
        };

        enum class RuntimeActionKind : std::uint8_t
        {
            NoOp,
            OpenPanel,
            ClosePanel,
            UiClose,
            UiBack,
            UiSelectTab,
            UiResetPanelLayout,
            UiSpawnSelectPlugin,
            UiSpawnSelectCategory,
            UiSpawnOpenItem,
            UiSpawnActivateAction,
            UiSpawnBack,
            UiSpawnSearch,
            UiSpawnClearSearch,
        };

        // Which Spawner search the keyboard panel types into; None hides the panel on the Spawner.
        enum class SpawnSearchField : std::uint8_t
        {
            None,
            Sources,
            Items,
        };

        struct PanelPose
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT3 right{};
            DirectX::XMFLOAT3 up{};
            DirectX::XMFLOAT3 front{};
            float physicalWidth{ devui::render::kDefaultPanelPhysicalWidth };
            float physicalHeight{ devui::render::kDefaultPanelPhysicalHeight };
        };

        struct RuntimeAction
        {
            RuntimeActionKind kind{ RuntimeActionKind::NoOp };
            ConfiguratorTab tab{ ConfiguratorTab::Spawn };
            int value{ 0 };
            std::size_t index{ 0 };
            std::uint32_t formId{ 0 };
            bool hasPanelPose{ false };
            PanelPose panelPose{};
            std::uint64_t generation{ 0 };
        };

        // A tab of the top bar: page names the external page of an External tab.
        struct TabTarget
        {
            ConfiguratorTab tab{ ConfiguratorTab::Spawn };
            std::uint64_t page{ 0 };
        };

        SpawnBrowser s_spawnBrowser;
        ConsoleTab s_consoleTab;
        std::atomic_bool s_consoleKeyboardHeld = false;
        std::atomic_bool s_consoleWindowFocused = false;
        std::atomic_uint32_t s_consoleSelectedReference{ 0 };
        std::atomic_bool s_consoleCommandsPending = false;
        std::uint32_t s_consoleSelectionAge = 0; // Game thread only.
        bool s_consoleWasActive = false;         // Game thread only.
        // Lines and keyboard state are guarded by s_runtimeMutex; the field is read lock-free
        // to decide whether the keyboard panel is up.
        OnScreenKeyboard s_keyboard;
        ConsoleLine s_spawnSourceSearch;
        ConsoleLine s_spawnItemSearch;
        float s_spawnSourceScroll = 0.0f;
        float s_spawnItemScroll = 0.0f;
        std::atomic<SpawnSearchField> s_spawnSearchField{ SpawnSearchField::None };
        std::mutex s_runtimeMutex;

        std::atomic<ConfiguratorTab> s_activeTab{ ConfiguratorTab::Spawn };
        std::atomic_uint64_t s_externalPage{ 0 };
        // Guarded by s_runtimeMutex: the menu has opened once, so it reopens on its last tab.
        bool s_openedBefore = false;
        // A Back press offered to the open external page first (render thread only).
        bool s_externalBack = false;
        std::atomic_bool s_panelOpen = false;
        std::atomic_uint64_t s_closedCount{ 0 };
        std::atomic_bool s_panelOpening = false;
        std::atomic<float> s_sessionPanelPhysicalWidth{
            devui::render::kDefaultPanelPhysicalWidth
        };
        std::atomic_bool s_providerInputReady = false;
        std::atomic_bool s_inventoryRefreshRequested = false;

        std::atomic<panel_resize::Handle> s_hoveredResizeHandle{
            panel_resize::Handle::None
        };
        std::atomic<panel_resize::Handle> s_activeResizeHandle{
            panel_resize::Handle::None
        };

        std::mutex s_actionMutex;
        std::array<RuntimeAction, kRuntimeActionQueueCapacity> s_actionQueue{};
        std::size_t s_actionReadIndex = 0;
        std::size_t s_actionWriteIndex = 0;
        std::size_t s_actionCount = 0;
        std::atomic_bool s_actionDrainScheduled = false;
        std::atomic_uint64_t s_actionGeneration{ 1 };
        std::atomic_bool s_actionGenerationExhausted = false;
        std::atomic_uint64_t s_actionQueueFullDrops{ 0 };

        void drainRuntimeActions();

        [[nodiscard]] bool spawnTabActive() noexcept
        {
            return s_activeTab.load(std::memory_order_acquire) == ConfiguratorTab::Spawn;
        }

        [[nodiscard]] ConsoleLine* spawnSearchLine(SpawnSearchField field) noexcept
        {
            switch (field) {
            case SpawnSearchField::Sources: return &s_spawnSourceSearch;
            case SpawnSearchField::Items: return &s_spawnItemSearch;
            case SpawnSearchField::None: break;
            }
            return nullptr;
        }

        // The line the keyboard panel types into, or none while the panel is down.
        [[nodiscard]] ConsoleLine* keyboardLineLocked() noexcept
        {
            switch (s_activeTab.load(std::memory_order_acquire)) {
            case ConfiguratorTab::Console: return &s_consoleTab.line();
            case ConfiguratorTab::Spawn: return spawnSearchLine(s_spawnSearchField.load(std::memory_order_acquire));
            default: return nullptr;
            }
        }

        void syncSpawnSearchLocked()
        {
            s_spawnBrowser.setPluginSearch(s_spawnSourceSearch.text());
            s_spawnBrowser.setItemSearch(s_spawnItemSearch.text());
        }

        void invalidateQueuedRuntimeActions()
        {
            std::scoped_lock lock(s_actionMutex);
            const auto generation = s_actionGeneration.load(std::memory_order_relaxed);
            if (generation == (std::numeric_limits<std::uint64_t>::max)()) {
                if (!s_actionGenerationExhausted.exchange(true, std::memory_order_relaxed)) {
                    logger::critical("Settings menu runtime action generation exhausted; input disabled");
                }
            } else {
                s_actionGeneration.store(generation + 1, std::memory_order_release);
            }
            s_actionReadIndex = 0;
            s_actionWriteIndex = 0;
            s_actionCount = 0;
        }

        bool publishPanelToRenderer(const PanelPose& pose) noexcept
        {
            const devui::render::PanelPose nativePose{
                .center = { pose.position.x, pose.position.y, pose.position.z },
                .right = { pose.right.x, pose.right.y, pose.right.z },
                .up = { pose.up.x, pose.up.y, pose.up.z },
                .front = { pose.front.x, pose.front.y, pose.front.z },
                .physicalWidth = pose.physicalWidth,
                .physicalHeight = pose.physicalHeight,
            };
            return devui::render::SetPanelOpen(true, &nativePose);
        }

        void setActiveTabLocked(ConfiguratorTab tab);

        // The menu's first tab: the first page another mod added (RobCo PALM's Wheel items),
        // else the Spawner.
        [[nodiscard]] TabTarget homeTab()
        {
            const auto pages = external_pages::tabs();
            return pages.empty() ? TabTarget{ ConfiguratorTab::Spawn, 0 } : TabTarget{ ConfiguratorTab::External, pages.front().handle };
        }

        [[nodiscard]] bool showingTab(const TabTarget& target) noexcept
        {
            return s_activeTab.load(std::memory_order_acquire) == target.tab &&
                   (target.tab != ConfiguratorTab::External || s_externalPage.load(std::memory_order_acquire) == target.page);
        }

        void showTabLocked(const TabTarget& target)
        {
            if (target.tab == ConfiguratorTab::External) s_externalPage.store(target.page, std::memory_order_release);
            setActiveTabLocked(target.tab);
        }

        void openPanelLocked(const RuntimeAction& action)
        {
            struct OpeningFinished {~OpeningFinished(){s_panelOpening.store(false);}} openingFinished;
            if (s_panelOpen.load(std::memory_order_acquire) || !s_providerInputReady.load(std::memory_order_acquire) ||
                !action.hasPanelPose) {
                return;
            }

            // It opens on its home tab the first time, then on the tab it was closed on.
            if (!std::exchange(s_openedBefore, true)) showTabLocked(homeTab());
            if (spawnTabActive()) (void)s_spawnBrowser.ensureIndexBuilt();

            s_hoveredResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            s_activeResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            if (!publishPanelToRenderer(action.panelPose)) {
                logger::warn("Settings menu not opened: RPS UI Framework refused its panel");
                return;
            }
            s_panelOpen.store(true, std::memory_order_release);
            logger::info(
                "Settings menu opened at {:.2f},{:.2f},{:.2f}",
                action.panelPose.position.x,
                action.panelPose.position.y,
                action.panelPose.position.z);
        }

        void closePanelLocked(const char* reason)
        {
            if (!s_panelOpen.exchange(false, std::memory_order_acq_rel)) {
                return;
            }
            s_closedCount.fetch_add(1, std::memory_order_acq_rel);
            devui::render::SetPanelOpen(false);
            s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
            s_hoveredResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            s_activeResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            invalidateQueuedRuntimeActions();
            logger::info("Settings menu panel closed ({})", reason);
        }

        void setActiveTabLocked(ConfiguratorTab tab)
        {
            if (s_activeTab.exchange(tab, std::memory_order_acq_rel) == tab) {
                return;
            }
            s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
            // A Spawner that cannot build its index shows why in its workspace.
            if (tab == ConfiguratorTab::Spawn) (void)s_spawnBrowser.ensureIndexBuilt();
        }

        void resetPanelLayoutLocked()
        {
            s_hoveredResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            s_activeResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            if (devui::render::ResetFrameworkPanelLayout()) {
                s_sessionPanelPhysicalWidth.store(
                    devui::render::kDefaultPanelPhysicalWidth,
                    std::memory_order_release);
            } else {
                logger::warn("Settings menu panel reset refused by RPS UI Framework");
            }
        }

        void applyRuntimeActionLocked(const RuntimeAction& action)
        {
            if (s_actionGenerationExhausted.load(std::memory_order_acquire) ||
                action.generation != s_actionGeneration.load(std::memory_order_acquire)) {
                return;
            }
            const bool lifecycle = action.kind == RuntimeActionKind::NoOp ||
                                   action.kind == RuntimeActionKind::OpenPanel ||
                                   action.kind == RuntimeActionKind::ClosePanel;
            if (!lifecycle && !s_panelOpen.load(std::memory_order_acquire)) {
                return;
            }

            switch (action.kind) {
            case RuntimeActionKind::NoOp:
                break;
            case RuntimeActionKind::OpenPanel:
                openPanelLocked(action);
                break;
            case RuntimeActionKind::ClosePanel:
                closePanelLocked("Closed because gameplay input is unavailable");
                break;
            case RuntimeActionKind::UiClose:
                closePanelLocked("Closed by panel button");
                break;
            case RuntimeActionKind::UiSelectTab:
                showTabLocked({ action.tab, action.index });
                break;
            case RuntimeActionKind::UiResetPanelLayout:
                resetPanelLayoutLocked();
                break;
            case RuntimeActionKind::UiSpawnSelectPlugin:
                if (spawnTabActive()) {
                    (void)s_spawnBrowser.selectPlugin(action.index);
                }
                break;
            case RuntimeActionKind::UiSpawnSelectCategory:
                if (spawnTabActive()) {
                    (void)s_spawnBrowser.selectCategory(action.index);
                }
                break;
            case RuntimeActionKind::UiSpawnOpenItem:
                if (spawnTabActive() && s_spawnBrowser.openItem(action.formId)) {
                    s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
                }
                break;
            case RuntimeActionKind::UiSpawnActivateAction:
                if (spawnTabActive()) {
                    (void)s_spawnBrowser.activateMenuAction(action.index, action.formId);
                    s_inventoryRefreshRequested.store(true, std::memory_order_release);
                }
                break;
            case RuntimeActionKind::UiSpawnBack:
                if (spawnTabActive()) {
                    if (action.value != 0) {
                        s_spawnBrowser.returnToBrowse();
                    } else {
                        (void)s_spawnBrowser.back();
                    }
                }
                break;
            case RuntimeActionKind::UiSpawnSearch:
                if (spawnTabActive() && action.index <= static_cast<std::size_t>(SpawnSearchField::Items)) {
                    const auto field = static_cast<SpawnSearchField>(action.index);
                    s_spawnSearchField.store(s_spawnSearchField.load(std::memory_order_acquire) == field ? SpawnSearchField::None : field,
                        std::memory_order_release);
                }
                break;
            case RuntimeActionKind::UiSpawnClearSearch:
                if (auto* line = spawnSearchLine(static_cast<SpawnSearchField>(action.index)); spawnTabActive() && line) {
                    line->clear();
                    syncSpawnSearchLocked();
                }
                break;
            case RuntimeActionKind::UiBack:
                // Back closes the search keyboard first, then the item menu, then goes to the
                // home tab, and on the home tab closes the menu.
                if (spawnTabActive() && s_spawnSearchField.exchange(SpawnSearchField::None, std::memory_order_acq_rel) != SpawnSearchField::None) break;
                if (spawnTabActive() && s_spawnBrowser.back()) break;
                if (const auto home = homeTab(); !showingTab(home)) {
                    showTabLocked(home);
                } else {
                    closePanelLocked("Closed by Config back navigation");
                }
                break;
            }
        }

        [[nodiscard]] bool popRuntimeAction(RuntimeAction& outAction)
        {
            std::scoped_lock lock(s_actionMutex);
            if (s_actionCount == 0) {
                return false;
            }
            outAction = s_actionQueue[s_actionReadIndex];
            s_actionReadIndex = (s_actionReadIndex + 1) % s_actionQueue.size();
            --s_actionCount;
            return true;
        }

        void scheduleRuntimeActionDrain()
        {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
            return; // Preview explicitly drains after drawing, never through F4SE.
#else
            bool expected = false;
            if (!s_actionDrainScheduled.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                return;
            }
            const auto* tasks = F4SE::GetTaskInterface();
            if (!tasks) {
                s_actionDrainScheduled.store(false, std::memory_order_release);
                return;
            }
            tasks->AddTask([]() { drainRuntimeActions(); });
#endif
        }

        [[nodiscard]] bool enqueueRuntimeAction(
            RuntimeAction action,
            std::uint64_t producerGeneration)
        {
            if (s_actionGenerationExhausted.load(std::memory_order_acquire)) {
                return false;
            }
            {
                std::scoped_lock lock(s_actionMutex);
                if (producerGeneration != s_actionGeneration.load(std::memory_order_acquire) ||
                    s_actionCount >= s_actionQueue.size()) {
                    if (s_actionCount >= s_actionQueue.size()) {
                        s_actionQueueFullDrops.fetch_add(1, std::memory_order_relaxed);
                    }
                    return false;
                }
                action.generation = producerGeneration;
                s_actionQueue[s_actionWriteIndex] = action;
                s_actionWriteIndex = (s_actionWriteIndex + 1) % s_actionQueue.size();
                ++s_actionCount;
            }
            scheduleRuntimeActionDrain();
            return true;
        }

        void drainRuntimeActions()
        {
            std::array<RuntimeAction, kRuntimeActionQueueCapacity> batch{};
            std::size_t batchCount = 0;
            RuntimeAction action{};
            while (batchCount < batch.size() && popRuntimeAction(action)) {
                batch[batchCount++] = action;
            }
            if (batchCount > 0) {
                std::scoped_lock lock(s_runtimeMutex);
                for (std::size_t index = 0; index < batchCount; ++index) {
                    applyRuntimeActionLocked(batch[index]);
                }
            }
            s_actionDrainScheduled.store(false, std::memory_order_release);
            {
                std::scoped_lock lock(s_actionMutex);
                if (s_actionCount == 0) {
                    return;
                }
            }
            scheduleRuntimeActionDrain();
        }

        [[nodiscard]] bool queueUiAction(RuntimeAction action)
        {
            return enqueueRuntimeAction(
                action, s_actionGeneration.load(std::memory_order_acquire));
        }

        [[nodiscard]] ImVec4 accentColor(float alpha = 1.0f) noexcept
        {
            return devui::visual::accent(alpha);
        }

        [[nodiscard]] ImVec4 mutedColor(float alpha = 1.0f) noexcept
        {
            return devui::visual::muted(alpha);
        }

        [[nodiscard]] ImU32 packed(const ImVec4& color) noexcept
        {
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        class ScopedFont final
        {
        public:
            explicit ScopedFont(devui::render::FontRole role, float size = 0.0f) noexcept
            {
                if (auto* font = devui::render::GetFont(role)) {
                    ImGui::PushFont(font, size > 0.0f ? size : devui::render::FontSize(role));
                    pushed_ = true;
                }
            }

            ~ScopedFont() noexcept
            {
                if (pushed_) {
                    ImGui::PopFont();
                }
            }

            ScopedFont(const ScopedFont&) = delete;
            ScopedFont& operator=(const ScopedFont&) = delete;

        private:
            bool pushed_{ false };
        };

        [[nodiscard]] ImFont* fontFor(devui::render::FontRole role) noexcept
        {
            if (auto* font = devui::render::GetFont(role)) {
                return font;
            }
            return ImGui::GetFont();
        }

        [[nodiscard]] bool tabChip(const char* label, bool active, ImVec2 size)
        {
            ImGui::PushID(label);
            const ImVec2 minimum = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("tab", size);
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
            auto* draw = ImGui::GetWindowDrawList();
            devui::visual::chosenBox(draw, minimum, maximum, active, hovered);

            auto* font = fontFor(devui::render::FontRole::Medium);
            const float textWidth = font->CalcTextSizeA(26, FLT_MAX, 0, label).x;
            const float fontSize = textWidth > size.x - 24 ? 26 * (size.x - 24) / textWidth : 26;
            draw->AddText(font, fontSize,
                {minimum.x + 12, minimum.y + 16},
                packed(devui::visual::inkFor(active, hovered)), label);
            ImGui::PopID();
            return clicked;
        }

        [[nodiscard]] bool navigationRow(const char* label, std::size_t count, bool active, float = 48)
        {
            std::array<char, 24> badge{};
            std::snprintf(badge.data(), badge.size(), "%zu", count);
            return devui::visual::navigation(label, active, badge.data());
        }

        // Spawner, Console, then the pages other mods add (SettingsMenu::registerPage).
        struct TopBarTab
        {
            ConfiguratorTab tab{ ConfiguratorTab::Spawn };
            std::uint64_t page{ 0 };
            std::string label;
        };
        constexpr float kTopBarTabGap = 10.0f;

        [[nodiscard]] std::vector<TopBarTab> topBarTabs()
        {
            std::vector<TopBarTab> tabs;
            tabs.push_back({ ConfiguratorTab::Spawn, 0, "Spawner" });
            tabs.push_back({ ConfiguratorTab::Console, 0, "Console" });
            for (auto& page : external_pages::tabs())
                tabs.push_back({ ConfiguratorTab::External, page.handle, std::move(page.title) });
            return tabs;
        }

        struct TopBarSlot
        {
            float x{ 0.0f };
            float width{ 0.0f };
        };

        // Tabs share the row by label width, so every label keeps its full size. Spare
        // room goes equally to all of them; a row too narrow scales them down together.
        [[nodiscard]] std::vector<TopBarSlot> topBarSlots(float windowWidth, const std::vector<TopBarTab>& tabs)
        {
            constexpr float kFontSize = 26.0f;
            constexpr float kLabelPadding = 24.0f;
            constexpr float kMaximumSpare = 40.0f;
            auto* font = fontFor(devui::render::FontRole::Medium);
            const float start = devui::visual::railWidth(windowWidth) + 14.0f;
            const float available = windowWidth - 342.0f - start;
            std::vector<float> natural(tabs.size());
            float total = 0.0f;
            for (std::size_t i = 0; i < tabs.size(); ++i) {
                natural[i] = font->CalcTextSizeA(kFontSize, FLT_MAX, 0.0f, tabs[i].label.c_str()).x + kLabelPadding;
                total += natural[i];
            }
            const float count = static_cast<float>(tabs.size());
            const float room = available - kTopBarTabGap * (std::max)(0.0f, count - 1.0f);
            const float scale = total > room ? room / total : 1.0f;
            const float spare = total > room ? 0.0f : (std::min)(kMaximumSpare, (room - total) / (std::max)(1.0f, count));
            std::vector<TopBarSlot> slots(tabs.size());
            float x = start;
            for (std::size_t i = 0; i < tabs.size(); ++i) {
                slots[i] = { x, natural[i] * scale + spare };
                x += slots[i].width + kTopBarTabGap;
            }
            return slots;
        }
        void drawTopBar()
        {
            const auto active = s_activeTab.load(std::memory_order_acquire);
            const auto p = ImGui::GetWindowPos();
            const float width = ImGui::GetWindowWidth();
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddLine({p.x, p.y + 94}, {p.x + width, p.y + 94}, packed(mutedColor(0.24f)));
            ImGui::SetCursorPos({28, 15});
            { ScopedFont font(devui::render::FontRole::Medium, 16); ImGui::TextColored(accentColor(), "ROCK Family"); }
            ImGui::SetCursorPos({28, 43});
            { ScopedFont font(devui::render::FontRole::Heading, 30); ImGui::TextUnformatted("Configurator"); }
            const auto tabs = topBarTabs();
            const auto slots = topBarSlots(width, tabs);
            const auto page = s_externalPage.load(std::memory_order_acquire);
            for (std::size_t i = 0; i < tabs.size(); ++i) {
                ImGui::SetCursorPos({slots[i].x, 28});
                ImGui::PushID(static_cast<int>(i));
                const bool chosen = tabs[i].tab == active && (tabs[i].tab != ConfiguratorTab::External || tabs[i].page == page);
                if (tabChip(tabs[i].label.c_str(), chosen, {slots[i].width, 62}))
                    (void)queueUiAction({ .kind = RuntimeActionKind::UiSelectTab, .tab = tabs[i].tab, .index = tabs[i].page });
                ImGui::PopID();
            }
            ImGui::SetCursorPos({width - 322, 20});
            { ScopedFont font(devui::render::FontRole::Medium, 24);
              if (devui::visual::button("< Back", {100, 54})) (void)queueUiAction({.kind = RuntimeActionKind::UiBack}); }
            ImGui::SameLine(0, 10);
            const float percent = s_sessionPanelPhysicalWidth.load(std::memory_order_acquire) / devui::render::kDefaultPanelPhysicalWidth * 100;
            std::array<char, 32> sizeLabel{};
            // The panel's size, and a reset of its size and place (grip held moves it).
            std::snprintf(sizeLabel.data(), sizeLabel.size(), "RESET %.0f%%", percent);
            { ScopedFont font(devui::render::FontRole::Medium, 18);
              if (devui::visual::button(sizeLabel.data(), {126, 54})) (void)queueUiAction({.kind = RuntimeActionKind::UiResetPanelLayout}); }
            ImGui::SameLine(0, 10);
            if (devui::visual::button("X##close", {52, 54})) (void)queueUiAction({.kind = RuntimeActionKind::UiClose});
        }

        // A search line. Clicking it opens the keyboard panel under Config, or closes it again;
        // the X beside it clears the search.
        void drawSpawnSearch(const char* id, const char* placeholder, SpawnSearchField field, float& scroll)
        {
            constexpr float kHeight = 44.0f;
            const auto* line = spawnSearchLine(field);
            const bool hasText = !line->text().empty();
            const bool editing = s_spawnSearchField.load(std::memory_order_acquire) == field;
            const float width = ImGui::GetContentRegionAvail().x - (hasText ? kHeight + 6.0f : 0.0f);
            ImGui::PushID(id);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("field", { width, kHeight })) {
                (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnSearch, .tab = ConfiguratorTab::Spawn, .index = static_cast<std::size_t>(field)});
            }
            ImGui::SetCursorScreenPos(start);
            drawLineField(*line, width, kHeight, 22.0f, placeholder, scroll, editing);
            if (hasText) {
                ImGui::SameLine(0, 6);
                ScopedFont font(devui::render::FontRole::Medium, 20);
                if (devui::visual::button("X", { kHeight, kHeight })) {
                    (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnClearSearch, .tab = ConfiguratorTab::Spawn, .index = static_cast<std::size_t>(field)});
                }
            }
            ImGui::PopID();
        }

        void drawSpawnRail()
        {
            devui::visual::caption("ITEM SOURCES");
            ImGui::Dummy({0, 12});
            if (!s_spawnBrowser.indexBuilt()) {
                ImGui::TextWrapped("%s", s_spawnBrowser.lastResult().c_str()); return;
            }
            drawSpawnSearch("source-search", "Search mods", SpawnSearchField::Sources, s_spawnSourceScroll);
            ImGui::Dummy({0, 8});
            // The search stays in view while the source list scrolls under it.
            ImGui::BeginChild("spawn-sources", {0, 0});
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(s_spawnBrowser.visiblePluginCount()));
            while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto index = s_spawnBrowser.visiblePluginAt(static_cast<std::size_t>(row));
                const auto* plugin = s_spawnBrowser.pluginAt(index);
                if (!plugin) continue;
                ImGui::PushID(static_cast<int>(index));
                if (navigationRow(plugin->name.c_str(), plugin->items.size(), index == s_spawnBrowser.pluginCursor()))
                    (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnSelectPlugin, .tab = ConfiguratorTab::Spawn, .index = index});
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        void drawSpawnBrowse()
        {
            const auto* plugin = s_spawnBrowser.pluginAt(s_spawnBrowser.pluginCursor());
            devui::visual::heading("Spawner", plugin ? plugin->name.c_str() : "No item source");
            const float categoryWidth = (ImGui::GetContentRegionAvail().x - 7 * 6) / 8;
            for (std::size_t i = 0; i < s_spawnBrowser.categoryCount(); ++i) {
                if (i) ImGui::SameLine(0, 6);
                ImGui::PushID(static_cast<int>(i));
                const bool active = i == s_spawnBrowser.categoryCursor();
                { ScopedFont font(devui::render::FontRole::Medium, 18);
                  if (devui::visual::button(s_spawnBrowser.categoryName(i).data(), {categoryWidth, 36},active))
                    (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnSelectCategory, .tab = ConfiguratorTab::Spawn, .index = i}); }
                ImGui::PopID();
            }
            ImGui::Dummy({0, 10});
            drawSpawnSearch("item-search", "Search items", SpawnSearchField::Items, s_spawnItemScroll);
            ImGui::Dummy({0, 10});
            constexpr auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH;
            if (ImGui::BeginTable("spawn-items", 3, flags, {0,0})) {
                ImGui::TableSetupColumn("ITEM", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthFixed, 130);
                ImGui::TableSetupColumn("FORM ID", ImGuiTableColumnFlags_WidthFixed, 120);
                devui::visual::tableHeader("ITEM", "TYPE", "FORM ID");
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(s_spawnBrowser.filteredItemCount()));
                while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto* item = s_spawnBrowser.filteredItemAt(i);
                    if (!item) continue;
                    ImGui::PushID(i);
                    ImGui::TableNextRow(0, 46); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(item->name.c_str(), i == s_spawnBrowser.itemCursor(), ImGuiSelectableFlags_SpanAllColumns, {0,30})) {
                        (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnOpenItem, .tab = ConfiguratorTab::Spawn, .formId = item->formId});
                    }
                    ImGui::TableSetColumnIndex(1);
                    { ScopedFont font(devui::render::FontRole::Body, 17); ImGui::TextColored(mutedColor(), "%s", s_spawnBrowser.categoryName(item->category).data()); }
                    ImGui::TableSetColumnIndex(2);
                    { ScopedFont font(devui::render::FontRole::Mono, 16); ImGui::TextColored(mutedColor(), "%08X", item->formId); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }

        void drawSpawnMenu()
        {
            const auto* item = s_spawnBrowser.currentItem();
            ImGui::BeginChild("spawn-item-head", {0,104}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
            devui::visual::heading(item ? item->name.c_str() : "Selected item", "Add this item to your inventory.");
            { ScopedFont font(devui::render::FontRole::Mono, 15);
              ImGui::TextColored(mutedColor(), "%08X  /  %s", s_spawnBrowser.menuFormId(), item ? s_spawnBrowser.categoryName(item->category).data() : ""); }
            ImGui::EndChild();
            ImGui::Dummy({0, 18});
            devui::visual::caption("QUANTITY & ACTIONS");
            ImGui::Dummy({0, 12});
            const float width = (std::min)(320.0f, (ImGui::GetContentRegionAvail().x - 16) / 2);
            for (std::size_t i = 0; i < s_spawnBrowser.menuActionCount(); ++i) {
                const auto* action = s_spawnBrowser.menuActionAt(i);
                if (!action) continue;
                if (i % 2) ImGui::SameLine(0, 16);
                ImGui::PushID(static_cast<int>(i));
                { ScopedFont font(devui::render::FontRole::Medium, 28);
                  if (devui::visual::button(action->label.c_str(), {width,76},i==0)) {
                    (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnActivateAction, .tab = ConfiguratorTab::Spawn, .index = i, .formId = s_spawnBrowser.menuFormId()});
                  } }
                ImGui::PopID();
                if (i % 2) ImGui::Dummy({0, 8});
            }
            ImGui::Dummy({0, 20});
            { ScopedFont font(devui::render::FontRole::Body, 16);
              ImGui::TextColored(mutedColor(), "%s", s_spawnBrowser.lastResult().c_str()); }
            ImGui::Dummy({0, 16});
            if (devui::visual::button("< Item list", {160,44})) {
                (void)queueUiAction({.kind = RuntimeActionKind::UiSpawnBack, .tab = ConfiguratorTab::Spawn});
            }
        }

        void drawSpawnWorkspace()
        {
            if (!s_spawnBrowser.indexBuilt()) {
                ImGui::TextWrapped("%s", s_spawnBrowser.lastResult().c_str());
                return;
            }
            if (s_spawnBrowser.view() == SpawnView::ItemMenu) {
                drawSpawnMenu();
            } else {
                drawSpawnBrowse();
            }
        }

        void drawCurrentTabBody()
        {
            if (s_activeTab.load(std::memory_order_acquire) == ConfiguratorTab::External) {
                const bool back = std::exchange(s_externalBack, false);
                bool usedBack = false;
                const auto available = ImGui::GetContentRegionAvail();
                ImGui::BeginChild("external-page", available, ImGuiChildFlags_None);
                const bool drawn = external_pages::draw(s_externalPage.load(std::memory_order_acquire), available.x, available.y, back, usedBack);
                ImGui::EndChild();
                if (!drawn) {
                    // Its mod removed the page: the menu goes to its home tab.
                    const auto home = homeTab();
                    (void)queueUiAction({ .kind = RuntimeActionKind::UiSelectTab, .tab = home.tab, .index = home.page });
                }
                else if (back && !usedBack) (void)queueUiAction({ .kind = RuntimeActionKind::UiBack });
                return;
            }
            if(s_activeTab.load(std::memory_order_acquire)==ConfiguratorTab::Console) {
                ConsoleStatus status;
                status.keyboardHeld = s_consoleKeyboardHeld.load(std::memory_order_acquire);
                status.windowFocused = s_consoleWindowFocused.load(std::memory_order_acquire);
                status.outputCaptured = console::captureInstalled();
                if (const auto reference = s_consoleSelectedReference.load(std::memory_order_acquire)) status.selectedReference = reference;
                s_consoleTab.draw(status);
                if (s_consoleTab.hasCommands() || s_consoleTab.hasReloadRequest()) s_consoleCommandsPending.store(true, std::memory_order_release);
                return;
            }
            // The Spawner: its item sources on the rail, the items beside them.
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 18.0f, 24.0f });
            ImGui::PushStyleColor(ImGuiCol_ChildBg, devui::visual::surface(0.7f));
            ImGui::PushStyleColor(ImGuiCol_Border, rpsui::settings_menu::theme::border());
            ImGui::BeginChild("rail", { devui::visual::railWidth(ImGui::GetContentRegionAvail().x), 0.0f }, ImGuiChildFlags_AlwaysUseWindowPadding);
            drawSpawnRail();
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0,0);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Border, rpsui::settings_menu::theme::border());
            ImGui::BeginChild("workspace", { 0.0f, 0.0f }, ImGuiChildFlags_AlwaysUseWindowPadding);
            drawSpawnWorkspace();
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }

        void drawResizeChrome()
        {
            const auto hovered = s_hoveredResizeHandle.load(std::memory_order_acquire);
            const auto active = s_activeResizeHandle.load(std::memory_order_acquire);
            const auto effective = active != panel_resize::Handle::None ? active : hovered;
            switch (effective) {
            case panel_resize::Handle::Left:
            case panel_resize::Handle::Right:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            case panel_resize::Handle::Top:
            case panel_resize::Handle::Bottom:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                break;
            case panel_resize::Handle::TopLeft:
            case panel_resize::Handle::BottomRight:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                break;
            case panel_resize::Handle::TopRight:
            case panel_resize::Handle::BottomLeft:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                break;
            case panel_resize::Handle::None:
                break;
            }

            const ImVec2 minimum = ImGui::GetWindowPos();
            const ImVec2 maximum{
                minimum.x + ImGui::GetWindowWidth(),
                minimum.y + ImGui::GetWindowHeight()
            };
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRect(
                { minimum.x + 3.0f, minimum.y + 3.0f },
                { maximum.x - 3.0f, maximum.y - 3.0f },
                packed(active != panel_resize::Handle::None ? accentColor(0.72f) : accentColor(0.24f)),
                0.0f,
                active != panel_resize::Handle::None ? 3.0f : 1.0f);

            const auto handleColor = [&](panel_resize::Handle handle) {
                if (handle == active) {
                    return packed(accentColor());
                }
                if (handle == hovered) {
                    return packed(accentColor(0.92f));
                }
                return packed(accentColor(0.10f));
            };
            const auto thickness = [&](panel_resize::Handle handle) {
                return handle == active ? 5.0f : handle == hovered ? 4.0f : 2.0f;
            };
            constexpr float inset = 7.0f;
            constexpr float corner = 38.0f;
            constexpr float edge = 52.0f;
            const float left = minimum.x + inset;
            const float right = maximum.x - inset;
            const float top = minimum.y + inset;
            const float bottom = maximum.y - inset;
            const float middleX = (left + right) * 0.5f;
            const float middleY = (top + bottom) * 0.5f;

            const auto line = [&](ImVec2 from, ImVec2 to, panel_resize::Handle handle) {
                draw->AddLine(from, to, handleColor(handle), thickness(handle));
            };
            line({ left, top }, { left + corner, top }, panel_resize::Handle::TopLeft);
            line({ left, top }, { left, top + corner }, panel_resize::Handle::TopLeft);
            line({ right - corner, top }, { right, top }, panel_resize::Handle::TopRight);
            line({ right, top }, { right, top + corner }, panel_resize::Handle::TopRight);
            line({ left, bottom }, { left + corner, bottom }, panel_resize::Handle::BottomLeft);
            line({ left, bottom - corner }, { left, bottom }, panel_resize::Handle::BottomLeft);
            line({ right - corner, bottom }, { right, bottom }, panel_resize::Handle::BottomRight);
            line({ right, bottom - corner }, { right, bottom }, panel_resize::Handle::BottomRight);
            line({ left, middleY - edge * 0.5f }, { left, middleY + edge * 0.5f }, panel_resize::Handle::Left);
            line({ right, middleY - edge * 0.5f }, { right, middleY + edge * 0.5f }, panel_resize::Handle::Right);
            line({ middleX - edge * 0.5f, top }, { middleX + edge * 0.5f, top }, panel_resize::Handle::Top);
            line({ middleX - edge * 0.5f, bottom }, { middleX + edge * 0.5f, bottom }, panel_resize::Handle::Bottom);
        }

        // END_PRIVATE_IMPLEMENTATION
    }

    bool drawImGui(float x, float y, float width, float height, bool backRequested) noexcept
    {
        std::unique_lock lock(s_runtimeMutex, std::try_to_lock);
        if (!lock.owns_lock() || !s_panelOpen.load(std::memory_order_acquire)) {
            return false;
        }

        try {
            ScopedFont configFont(devui::render::FontRole::Body, 26);
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            if (backRequested) {
                auto& context = *ImGui::GetCurrentContext();
                if (!context.OpenPopupStack.empty()) {
                    ImGui::ClosePopupToLevel(context.OpenPopupStack.Size - 1, true);
                } else if (context.ActiveId != 0) {
                    ImGui::ClearActiveID();
                } else if (s_activeTab.load(std::memory_order_acquire) == ConfiguratorTab::External) {
                    s_externalBack = true;
                } else {
                    (void)queueUiAction({ .kind = RuntimeActionKind::UiBack });
                }
            }
            ImGui::SetNextWindowSize(
                ImVec2(width, height),
                ImGuiCond_Always);
            constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            if (!ImGui::Begin("Settings##root", nullptr, flags)) {
                ImGui::End();
                ImGui::PopStyleVar();
                return true;
            }

            const ImVec2 windowPosition = ImGui::GetWindowPos();
            const float windowWidth = ImGui::GetWindowWidth();
            const float windowHeight = ImGui::GetWindowHeight();
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(windowPosition,
                { windowPosition.x + windowWidth, windowPosition.y + windowHeight },packed(devui::visual::surface()));
            drawTopBar();
            constexpr float bodyTop = 94.0f;
            ImGui::SetCursorPos({ 0.0f, bodyTop });
            ImGui::BeginChild(
                "body",
                { windowWidth, windowHeight - bodyTop },
                ImGuiChildFlags_None);
            drawCurrentTabBody();
            ImGui::EndChild();
            drawResizeChrome();
            ImGui::End();
            ImGui::PopStyleVar();
            return true;
        } catch (...) {
            static std::atomic_bool logged = false;
            if (!logged.exchange(true, std::memory_order_relaxed)) {
                logger::error("Settings menu contained an exception while building the ImGui frame");
            }
            return false;
        }
    }

    bool drawKeyboardPanel(float x, float y, float width, float height, bool backRequested) noexcept
    {
        std::unique_lock lock(s_runtimeMutex, std::try_to_lock);
        if (!lock.owns_lock() || !s_panelOpen.load(std::memory_order_acquire)) {
            return false;
        }
        try {
            auto* line = keyboardLineLocked();
            if (!line) {
                return false;
            }
            if (backRequested) {
                (void)queueUiAction({ .kind = RuntimeActionKind::UiBack });
            }
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            if (ImGui::Begin("Keyboard##root", nullptr, flags)) {
                const ImVec2 minimum = ImGui::GetWindowPos();
                const ImVec2 maximum{ minimum.x + width, minimum.y + height };
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(minimum, maximum, packed(devui::visual::surface()));
                draw->AddRect({ minimum.x + 3.0f, minimum.y + 3.0f }, { maximum.x - 3.0f, maximum.y - 3.0f }, packed(accentColor(0.24f)));
                if (s_keyboard.draw(*line)) {
                    if (line == &s_consoleTab.line()) {
                        s_consoleTab.submit();
                    } else {
                        s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
                    }
                }
                syncSpawnSearchLocked();
                if (s_consoleTab.hasCommands()) s_consoleCommandsPending.store(true, std::memory_order_release);
            }
            ImGui::End();
            ImGui::PopStyleVar();
            return true;
        } catch (...) {
            static std::atomic_bool logged = false;
            if (!logged.exchange(true, std::memory_order_relaxed)) {
                logger::error("Settings menu contained an exception while building the keyboard frame");
            }
            return false;
        }
    }

    void onFrameworkPanelFrame(
        float physicalWidth,
        panel_resize::Handle hovered,
        panel_resize::Handle active) noexcept
    {
        if (std::isfinite(physicalWidth)) {
            s_sessionPanelPhysicalWidth.store(
                std::clamp(
                    physicalWidth,
                    devui::render::kMinimumPanelPhysicalWidth,
                    devui::render::kMaximumPanelPhysicalWidth),
                std::memory_order_release);
        }
        s_hoveredResizeHandle.store(hovered, std::memory_order_release);
        s_activeResizeHandle.store(active, std::memory_order_release);
    }

    bool keyboardInputActive() noexcept
    {
        if (!s_panelOpen.load(std::memory_order_acquire)) {
            return false;
        }
        switch (s_activeTab.load(std::memory_order_acquire)) {
        case ConfiguratorTab::Console: return true;
        case ConfiguratorTab::Spawn: return s_spawnSearchField.load(std::memory_order_acquire) != SpawnSearchField::None;
        default: return false;
        }
    }

    void serviceConsole(bool keyboardHeld, bool windowFocused, std::span<const ConsoleKeyEvent> keys)
    {
        constexpr std::uint32_t kSelectionRefreshFrames = 30;
        s_consoleKeyboardHeld.store(keyboardHeld, std::memory_order_release);
        s_consoleWindowFocused.store(windowFocused, std::memory_order_release);
        devui::render::SyncKeyboardPanel(keyboardInputActive());
        const bool active = s_panelOpen.load(std::memory_order_acquire) &&
                            s_activeTab.load(std::memory_order_acquire) == ConfiguratorTab::Console;
        if (active && ++s_consoleSelectionAge >= kSelectionRefreshFrames) {
            s_consoleSelectionAge = 0;
            s_consoleSelectedReference.store(console::selectedReference().value_or(0), std::memory_order_release);
        }
        // Shortcuts reload on each visit to the tab and on Reload, never per frame, so an
        // edited ConsoleCommands.ini applies without restarting and costs no polling.
        const bool entered = active && !s_consoleWasActive;
        s_consoleWasActive = active;
        const bool pending = s_consoleCommandsPending.exchange(false, std::memory_order_acq_rel);
        if (keys.empty() && !pending && !entered) {
            return;
        }
        std::vector<std::string> commands;
        bool reloadShortcuts = entered;
        {
            std::scoped_lock lock(s_runtimeMutex);
            auto* line = keyboardLineLocked();
            for (const auto& key : keys) {
                if (line == &s_consoleTab.line()) {
                    if (key.character) {
                        s_consoleTab.onCharacter(key.code);
                    } else {
                        s_consoleTab.onKey(key.code);
                    }
                } else if (line) {
                    if (key.character) {
                        typeCharacter(*line, key.code);
                    } else if (key.code == VK_RETURN) {
                        s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
                    } else {
                        (void)applyEditKey(*line, key.code);
                    }
                }
            }
            syncSpawnSearchLocked();
            commands = s_consoleTab.takeCommands();
            reloadShortcuts = s_consoleTab.takeReloadRequest() || reloadShortcuts;
        }
        if (reloadShortcuts) {
            // File I/O outside the mutex so rendering never waits on the disk.
            auto file = console::loadShortcuts();
            std::scoped_lock lock(s_runtimeMutex);
            s_consoleTab.setShortcuts(std::move(file.shortcuts), std::move(file.problem));
        }
        // Not under the mutex: the engine prints the echo through the capture hook, and
        // rendering should not wait on it. A closed panel leaves no stale command to run.
        if (s_panelOpen.load(std::memory_order_acquire)) {
            for (const auto& command : commands) {
                console::run(command);
            }
        }
    }

    void onGameDataReady()
    {
        {
            std::scoped_lock lock(s_runtimeMutex);
            if (!s_spawnBrowser.ensureIndexBuilt()) {
                logger::warn(
                    "Spawner index not ready at game-data-ready ({}); it will retry on first use",
                    s_spawnBrowser.lastResult());
            }
        }

    }

    bool isOpen() noexcept { return s_panelOpen.load(std::memory_order_acquire); }
    bool isOpening() noexcept { return s_panelOpening.load(std::memory_order_acquire); }
    std::uint64_t closedCount() noexcept { return s_closedCount.load(std::memory_order_acquire); }
    bool takeInventoryRefreshRequest() noexcept { return s_inventoryRefreshRequested.exchange(false); }

    void setAvailable(bool available) noexcept
    {
        const bool wasAvailable = s_providerInputReady.exchange(available, std::memory_order_acq_rel);
        if (!available && wasAvailable) close();
    }

    void close() noexcept
    {
        s_panelOpening.store(false);
        if (s_panelOpen.exchange(false, std::memory_order_acq_rel)) s_closedCount.fetch_add(1, std::memory_order_acq_rel);
        s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
        devui::render::SetPanelOpen(false);
        invalidateQueuedRuntimeActions();
    }

    void openAt(const devui::render::PanelPose& wheelPose)
    {
        if (isOpen() || !s_providerInputReady.load(std::memory_order_acquire) || s_panelOpening.exchange(true)) return;
        const float width = s_sessionPanelPhysicalWidth.load(std::memory_order_acquire);
        PanelPose pose;
        pose.position = wheelPose.center;
        pose.right = {wheelPose.right.x, wheelPose.right.y, wheelPose.right.z};
        pose.up = {wheelPose.up.x, wheelPose.up.y, wheelPose.up.z};
        pose.front = {wheelPose.front.x, wheelPose.front.y, wheelPose.front.z};
        pose.physicalWidth = width;
        pose.physicalHeight = width / devui::render::kPanelAspectRatio;
        if (!queueUiAction({.kind = RuntimeActionKind::OpenPanel, .hasPanelPose = true, .panelPose = pose}))s_panelOpening.store(false);
    }

#ifdef RPSUI_SETTINGS_MENU_PREVIEW
    void initializePreview()
    {
        std::scoped_lock lock(s_runtimeMutex);
        invalidateQueuedRuntimeActions();
        s_providerInputReady.store(true);
        (void)s_spawnBrowser.ensureIndexBuilt();
    }

    void previewShowTab(std::string_view tab)
    {
        std::scoped_lock lock(s_runtimeMutex);
        setActiveTabLocked(tab == "Console" ? ConfiguratorTab::Console : ConfiguratorTab::Spawn);
    }

    void previewKeys(std::span<const ConsoleKeyEvent> keys) { serviceConsole(!keys.empty(), true, keys); }

    void setPreviewOpen(bool open) { s_panelOpen.store(open); }
    void drainPreviewActions() { drainRuntimeActions(); serviceConsole(false, false, {}); }
#endif

    void shutdown()
    {
        close();
        devui::render::Shutdown();
    }
}
