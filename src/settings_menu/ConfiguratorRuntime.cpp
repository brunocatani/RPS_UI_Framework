#include "PCH.h"

#include "ConfiguratorRuntime.h"

#include "ConsoleService.h"
#include "ConsoleTab.h"
#include "ExternalPages.h"
#include "SettingHelp.h"
#include "RockConfigurationClient.h"
#include "OnScreenKeyboard.h"
#include "IniSettingsStore.h"
#include "LoadedRpsMods.h"
#include "PanelResizePolicy.h"
#include "SpawnBrowser.h"
#include "render/FrameworkPanelRenderer.h"
#include "render/NativeRenderer.h"
#include <imgui_internal.h>
#include "render/ConfigUi.h"
#include <RPS_UI_Framework/BindingIcons.h>


namespace rpsui::settings_menu
{
    namespace
    {

        constexpr std::size_t kRuntimeActionQueueCapacity = 32;
        constexpr std::uint64_t kSettingTargetIdentityMask =
            (std::uint64_t{ 1 } << 60) - 1;
        constexpr std::uint64_t kInvalidRightHorizontalTarget =
            (std::numeric_limits<std::uint64_t>::max)();
        constexpr std::uint64_t kSemanticFnvOffset = 14695981039346656037ull;
        constexpr std::uint64_t kSemanticFnvPrime = 1099511628211ull;
        constexpr panel_resize::Constraints kPanelResizeConstraints{
            .aspectRatio = devui::render::kPanelAspectRatio,
            .minimumWidth = devui::render::kMinimumPanelPhysicalWidth,
            .maximumWidth = devui::render::kMaximumPanelPhysicalWidth,
        };

        enum class ConfiguratorTab : std::uint8_t
        {
            Settings,
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
            UiSelectMod,
            UiSelectRow,
            UiAdjustRow,
            UiSetBooleanRow,
            UiSetNumericRow,
            UiSetOptionRow,
            UiResetRow,
            UiReload,
            UiRefreshSettings,
            UiResetPanelLayout,
            UiOpenRockSetup,
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
            ConfiguratorTab tab{ ConfiguratorTab::Settings };
            int value{ 0 };
            std::size_t index{ 0 };
            std::uint32_t formId{ 0 };
            std::uint64_t horizontalTarget{ kInvalidRightHorizontalTarget };
            double numericValue{ 0.0 };
            bool hasPanelPose{ false };
            PanelPose panelPose{};
            std::uint64_t generation{ 0 };
        };

        struct SelectionAnchor
        {
            std::optional<std::string> id;
            std::size_t fallbackIndex{ 0 };
        };

        struct PendingNumericEdit
        {
            std::string id;
            std::size_t index{ 0 };
            std::uint64_t target{ kInvalidRightHorizontalTarget };
            double value{ 0.0 };
        };

        struct ModSettings {
            IniSettingsStore store;
            bool loaded = false;
            bool available = false;
            std::uint64_t identity = 0;
            SelectionAnchor anchor;
            std::size_t activeIndex = 0;
        };
        std::array<ModSettings, kRpsMods.size()> s_modSettings{
            ModSettings{IniSettingsStore({}, RpsMod::Rock)},
            ModSettings{IniSettingsStore({}, RpsMod::Paper)},
            ModSettings{IniSettingsStore({}, RpsMod::RockDeveloper)},
            ModSettings{IniSettingsStore({}, RpsMod::Csah)},
            ModSettings{IniSettingsStore({}, RpsMod::Diamond)},
            ModSettings{IniSettingsStore({}, RpsMod::Emitters)},
            ModSettings{IniSettingsStore({}, RpsMod::Scissors)},
            ModSettings{IniSettingsStore({}, RpsMod::ScissorsDeveloper)},
            ModSettings{IniSettingsStore({}, RpsMod::RpsUi)}
        };
        std::size_t s_modIndex = 0;
        ModSettings& activeSettings() { return s_modSettings[s_modIndex]; }
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
        std::string s_statusMessage;
        std::optional<PendingNumericEdit> s_pendingNumericEdit;

        std::atomic<ConfiguratorTab> s_activeTab{ ConfiguratorTab::Settings };
        std::atomic_uint64_t s_externalPage{ 0 };
        bool s_previewFineTuning = false; // Preview captures only.
        // A Back press offered to the open external page first (render thread only).
        bool s_externalBack = false;
        std::atomic_bool s_panelOpen = false;
        std::atomic_uint64_t s_closedCount{ 0 };
        std::atomic_bool s_settingsRefreshQueued = false;
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

        void hashByte(std::uint64_t& hash, std::uint8_t value) noexcept
        {
            hash ^= value;
            hash *= kSemanticFnvPrime;
        }

        void hashUint64(std::uint64_t& hash, std::uint64_t value) noexcept
        {
            for (unsigned shift = 0; shift < 64; shift += 8) {
                hashByte(hash, static_cast<std::uint8_t>(value >> shift));
            }
        }

        void hashString(std::uint64_t& hash, std::string_view value) noexcept
        {
            hashUint64(hash, value.size());
            for (const char character : value) {
                hashByte(hash, static_cast<std::uint8_t>(character));
            }
        }

        template <class Store>
        [[nodiscard]] std::uint64_t settingsIdentity(const Store& store, bool loaded)
        {
            std::uint64_t identity = kSemanticFnvOffset;
            hashByte(identity, loaded ? 1 : 0);
            if (!loaded) {
                return identity;
            }
            const auto& settings = store.settings();
            hashUint64(identity, settings.size());
            for (std::size_t index = 0; index < settings.size(); ++index) {
                hashString(identity, settings[index].id);
                hashByte(identity, static_cast<std::uint8_t>(settings[index].type));
                hashByte(identity, static_cast<std::uint8_t>(settings[index].control.kind));
                hashByte(identity, settings[index].control.bounded ? 1 : 0);
                hashByte(identity, store.isSlider(index) ? 1 : 0);
            }
            return identity;
        }

        [[nodiscard]] std::uint64_t selectedIdentity(
            std::uint64_t identity,
            std::size_t index) noexcept
        {
            hashUint64(identity, index);
            return identity & kSettingTargetIdentityMask;
        }

        [[nodiscard]] std::uint64_t settingTargetLocked(std::size_t index) noexcept
        {
            auto identity = activeSettings().identity;
            hashUint64(identity, s_modIndex);
            return selectedIdentity(identity, index);
        }

        template <class Store>
        [[nodiscard]] bool readStoreLocked(
            Store& store,
            bool& loaded,
            std::size_t& activeIndex,
            SelectionAnchor& anchor,
            std::uint64_t& identity,
            bool reload)
        {
            const auto& previous = store.settings();
            if (activeIndex < previous.size()) {
                anchor.id = previous[activeIndex].id;
                anchor.fallbackIndex = activeIndex;
            }
            const auto previousError = store.lastError();
            const auto previousStatus = store.lastProviderStatus();
            const bool wasLoaded = loaded;
            loaded = reload ? store.reload() : store.load();
            if (!loaded && (wasLoaded || previousError != store.lastError() || previousStatus != store.lastProviderStatus())) {
                logger::warn("Settings menu load failed: path='{}' error='{}' providerStatus={} thread={}",
                    store.path().string(), store.lastError(), static_cast<unsigned>(store.lastProviderStatus()), GetCurrentThreadId());
            } else if (loaded && !wasLoaded) {
                logger::info("Settings menu catalog loaded: path='{}' settings={} thread={}",
                    store.path().string(), store.settings().size(), GetCurrentThreadId());
            }
            const auto& settings = store.settings();
            if (!loaded || settings.empty()) {
                activeIndex = 0;
            } else {
                const auto anchored = anchor.id ? store.indexForId(*anchor.id) : std::nullopt;
                activeIndex = anchored ? *anchored :
                    (std::min)(anchor.fallbackIndex, settings.size() - 1);
                anchor.id = settings[activeIndex].id;
                anchor.fallbackIndex = activeIndex;
            }
            identity = settingsIdentity(store, loaded);
            return loaded;
        }

        [[nodiscard]] bool readActiveStoreLocked(bool reload)
        {
            s_pendingNumericEdit.reset();
            return readStoreLocked(
                activeSettings().store, activeSettings().loaded, activeSettings().activeIndex, activeSettings().anchor, activeSettings().identity, reload);
        }

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

        void updateSettingsStatusLocked()
        {
            s_statusMessage = activeSettings().loaded ?
                modInfo(static_cast<RpsMod>(s_modIndex)).applyHint :
                activeSettings().store.lastError();
        }

#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        std::array<bool, kRpsMods.size()> s_previewPresent{};
#endif

        [[nodiscard]] bool modPresentLocked(std::size_t index) noexcept
        {
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
            // F4SEVR unloads rejected plugins. Inspect loaded modules, not files in MO2.
            return configurationModLoaded(kRpsMods[index].id);
#else
            return s_previewPresent[index];
#endif
        }

        // The ROCK Dev page shows only while ROCK.toml's Developer.EnableDeveloperSettings is on.
        [[nodiscard]] bool rockDeveloperSettingsEnabledLocked()
        {
            const auto& rock = s_modSettings[static_cast<std::size_t>(RpsMod::Rock)];
            if (!rock.loaded) return false;
            for (const auto& setting : rock.store.settings())
                if (setting.id == "Developer.EnableDeveloperSettings") return setting_control::equalsIgnoreCase(setting.value, "true");
            return false;
        }

        void loadRpsSettingsLocked(bool reloadLoaded)
        {
            s_pendingNumericEdit.reset();
            for (std::size_t i = 0; i < s_modSettings.size(); ++i) {
                auto& settings = s_modSettings[i];
                settings.available = modPresentLocked(i) &&
                    (kRpsMods[i].id != RpsMod::RockDeveloper || rockDeveloperSettingsEnabledLocked());
                if (settings.available)
                    (void)readStoreLocked(settings.store, settings.loaded, settings.activeIndex,
                        settings.anchor, settings.identity, reloadLoaded && settings.loaded);
                else settings.loaded = false;
            }
            if (!activeSettings().available) {
                for (const auto mod : kRpsModDisplayOrder) {
                    const auto i=static_cast<std::size_t>(mod);
                    if (s_modSettings[i].available) { s_modIndex = i; break; }
                }
            }
            updateSettingsStatusLocked();
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
            s_settingsRefreshQueued.store(false);
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

        void openPanelLocked(const RuntimeAction& action)
        {
            struct OpeningFinished {~OpeningFinished(){s_panelOpening.store(false);}} openingFinished;
            if (s_panelOpen.load(std::memory_order_acquire)) {
                return;
            }
            if (!s_providerInputReady.load(std::memory_order_acquire)) {
                s_statusMessage = "ROCK provider is not ready for configurator input";
                return;
            }
            if (!action.hasPanelPose) {
                s_statusMessage = "Open the workshop from the wheel";
                return;
            }

            loadRpsSettingsLocked(true);
            s_pendingNumericEdit.reset();
            if (spawnTabActive() && !s_spawnBrowser.ensureIndexBuilt()) {
                s_statusMessage = s_spawnBrowser.lastResult();
            }

            s_hoveredResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            s_activeResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            if (!publishPanelToRenderer(action.panelPose)) {
                s_statusMessage = "The UI framework rejected the Config panel";
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
            s_pendingNumericEdit.reset();
            s_statusMessage = reason ? reason : "Panel closed";
            logger::info("Settings menu panel closed ({})", s_statusMessage);
        }

        void applySettingChangeLocked(const SettingChangeResult& result)
        {
            s_statusMessage = result.message.empty() ? "No change" : result.message;
            if (!result.saved && result.setting.fromRockApi) {
                logger::warn("Settings menu write failed: [{}] {} error='{}' providerStatus={} thread={}",
                    result.setting.section, result.setting.key, result.message,
                    static_cast<unsigned>(activeSettings().store.lastProviderStatus()), GetCurrentThreadId());
            }
            if (result.changed && result.saved && result.setting.id == "Developer.EnableDeveloperSettings") {
                loadRpsSettingsLocked(false);
            }
            if (result.changed && result.saved) {
                // The desktop preview writes the files of the folder it was given, like the game.
                (void)readActiveStoreLocked(true);
                s_statusMessage = activeSettings().loaded ?
                    std::format("Saved {}. {}", result.setting.key, kRpsMods[s_modIndex].applyHint) : activeSettings().store.lastError();
            }
        }

        void setActiveTabLocked(ConfiguratorTab tab)
        {
            if (s_activeTab.exchange(tab, std::memory_order_acq_rel) == tab) {
                return;
            }
            s_spawnSearchField.store(SpawnSearchField::None, std::memory_order_release);
            if (tab == ConfiguratorTab::Spawn && !s_spawnBrowser.ensureIndexBuilt()) {
                s_statusMessage = s_spawnBrowser.lastResult();
            }
        }

        void selectRowLocked(ConfiguratorTab tab, std::size_t index)
        {
            if (tab != s_activeTab.load(std::memory_order_acquire)) {
                return;
            }
            switch (tab) {
            case ConfiguratorTab::Settings:
                if (index < activeSettings().store.settings().size()) {
                    activeSettings().activeIndex = index;
                }
                break;
            case ConfiguratorTab::Spawn:
            case ConfiguratorTab::Console:
            case ConfiguratorTab::External:
                break;
            }
        }

        [[nodiscard]] bool validatesSettingTargetLocked(const RuntimeAction& action) noexcept
        {
            return action.tab == ConfiguratorTab::Settings &&
                   s_activeTab.load(std::memory_order_acquire) == ConfiguratorTab::Settings &&
                   activeSettings().available && activeSettings().loaded && action.index < activeSettings().store.settings().size() &&
                   action.horizontalTarget == settingTargetLocked(action.index);
        }

        void setBooleanRowLocked(const RuntimeAction& action)
        {
            if (!validatesSettingTargetLocked(action)) {
                return;
            }
            activeSettings().activeIndex = action.index;
            applySettingChangeLocked(activeSettings().store.setBooleanByIndex(action.index, action.value != 0));
        }

        void setNumericRowLocked(const RuntimeAction& action)
        {
            if (!validatesSettingTargetLocked(action)) {
                return;
            }
            activeSettings().activeIndex = action.index;
            applySettingChangeLocked(activeSettings().store.setNumericByIndex(action.index, action.numericValue));
        }

        void setOptionRowLocked(const RuntimeAction& action)
        {
            if (!validatesSettingTargetLocked(action) || action.value < 0) {
                return;
            }
            activeSettings().activeIndex = action.index;
            applySettingChangeLocked(activeSettings().store.setOptionByIndex(
                action.index, static_cast<std::size_t>(action.value)));
        }

        void reloadActiveLocked(ConfiguratorTab tab)
        {
            if (tab != s_activeTab.load(std::memory_order_acquire)) {
                return;
            }
            switch (tab) {
            case ConfiguratorTab::Settings:
                (void)readActiveStoreLocked(true);
                updateSettingsStatusLocked();
                break;
            case ConfiguratorTab::Spawn:
            case ConfiguratorTab::Console:
            case ConfiguratorTab::External:
                break;
            }
        }

        // ROCK's setup wizard opens in front of the player; Config closes so it is not in the way.
        void openRockSetupLocked()
        {
            const auto connection = configurationConnection(RpsMod::Rock);
            const auto status = connection.api && connection.api->openSetupWizard
                ? connection.api->openSetupWizard(connection.owner)
                : rock::api::Status::NotReady;
            if (status != rock::api::Status::Ok) {
                s_statusMessage = "ROCK could not open its setup wizard";
                logger::warn("Settings menu: ROCK setup wizard request failed ({})", static_cast<unsigned>(status));
                return;
            }
            closePanelLocked("ROCK setup wizard opened");
        }

        void resetPanelLayoutLocked()
        {
            s_hoveredResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            s_activeResizeHandle.store(panel_resize::Handle::None, std::memory_order_release);
            if (devui::render::ResetFrameworkPanelLayout()) {
                s_sessionPanelPhysicalWidth.store(
                    devui::render::kDefaultPanelPhysicalWidth,
                    std::memory_order_release);
                s_statusMessage = "Panel back where it opened, at 100%";
            } else {
                s_statusMessage = "RPS UI Framework rejected the panel reset";
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
                if (action.tab == ConfiguratorTab::External) s_externalPage.store(action.index, std::memory_order_release);
                setActiveTabLocked(action.tab);
                break;
            case RuntimeActionKind::UiSelectMod:
                if (s_activeTab.load() == ConfiguratorTab::Settings && action.index < s_modSettings.size() &&
                    s_modSettings[action.index].available && action.index != s_modIndex) {
                    s_modIndex = action.index;
                    s_pendingNumericEdit.reset();
                    invalidateQueuedRuntimeActions();
                    updateSettingsStatusLocked();
                }
                break;
            case RuntimeActionKind::UiSelectRow:
                selectRowLocked(action.tab, action.index);
                break;
            case RuntimeActionKind::UiAdjustRow:
                if (validatesSettingTargetLocked(action)) {
                    activeSettings().activeIndex = action.index;
                    applySettingChangeLocked(activeSettings().store.adjustByIndex(action.index, action.value));
                }
                break;
            case RuntimeActionKind::UiSetBooleanRow:
                setBooleanRowLocked(action);
                break;
            case RuntimeActionKind::UiSetNumericRow:
                setNumericRowLocked(action);
                break;
            case RuntimeActionKind::UiSetOptionRow:
                setOptionRowLocked(action);
                break;
            case RuntimeActionKind::UiResetRow:
                if (validatesSettingTargetLocked(action)) {
                    activeSettings().activeIndex = action.index;
                    applySettingChangeLocked(activeSettings().store.resetByIndex(action.index));
                }
                break;
            case RuntimeActionKind::UiRefreshSettings:
                s_settingsRefreshQueued.store(false);
                if (s_activeTab.load() == ConfiguratorTab::Settings) {
                    (void)readActiveStoreLocked(true);
                }
                break;
            case RuntimeActionKind::UiReload:
                reloadActiveLocked(action.tab);
                break;
            case RuntimeActionKind::UiResetPanelLayout:
                resetPanelLayoutLocked();
                break;
            case RuntimeActionKind::UiOpenRockSetup:
                openRockSetupLocked();
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
                // Back closes the search keyboard first, then the item menu, then the tab.
                if (spawnTabActive() && s_spawnSearchField.exchange(SpawnSearchField::None, std::memory_order_acq_rel) != SpawnSearchField::None) break;
                if (spawnTabActive() && s_spawnBrowser.back()) break;
                if (s_activeTab.load(std::memory_order_acquire) != ConfiguratorTab::Settings) {
                    s_pendingNumericEdit.reset();
                    s_activeTab.store(ConfiguratorTab::Settings, std::memory_order_release);
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

        [[nodiscard]] ImVec4 textColor(float alpha = 1.0f) noexcept
        {
            return devui::visual::text(alpha);
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

        // Settings (while any mod page is available), Spawner, Console, then the pages other
        // mods add (SettingsMenu::registerPage).
        struct TopBarTab
        {
            ConfiguratorTab tab{ ConfiguratorTab::Settings };
            std::uint64_t page{ 0 };
            std::string label;
        };
        constexpr float kTopBarTabGap = 10.0f;

        [[nodiscard]] std::vector<TopBarTab> topBarTabs()
        {
            std::vector<TopBarTab> tabs;
            if (std::any_of(s_modSettings.begin(), s_modSettings.end(), [](const auto& mod) { return mod.available; }))
                tabs.push_back({ ConfiguratorTab::Settings, 0, "Settings" });
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
            { ScopedFont font(devui::render::FontRole::Medium, 16); ImGui::TextColored(accentColor(), "RPS UI"); }
            ImGui::SetCursorPos({28, 43});
            { ScopedFont font(devui::render::FontRole::Heading, 30); ImGui::TextUnformatted("Settings"); }
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

        template <class Store>
        void drawSettingsRail(
            const Store& store,
            bool loaded,
            std::size_t activeIndex,
            ConfiguratorTab tab,
            const std::string& status)
        {
            devui::visual::caption(modInfo(static_cast<RpsMod>(s_modIndex)).name);
            ImGui::Dummy({0, 12});
            {
                ScopedFont font(devui::render::FontRole::Body, 17);
                if (devui::visual::button("Reload from disk", {-1, 40})) {
                    (void)queueUiAction({.kind = RuntimeActionKind::UiReload, .tab = tab});
                }
                if (static_cast<RpsMod>(s_modIndex) == RpsMod::Rock && devui::visual::button("Setup wizard", {-1, 40})) {
                    (void)queueUiAction({.kind = RuntimeActionKind::UiOpenRockSetup});
                }
            }
            ImGui::Dummy({0, 12});
            const auto& settings = store.settings();
            if (!loaded || settings.empty()) {
                ScopedFont font(devui::render::FontRole::Body, 17.0f);
                ImGui::TextColored(mutedColor(), "%s", status.c_str());
                return;
            }
            const std::string_view activeGroup =
                activeIndex < settings.size() ? settings[activeIndex].category : std::string_view{};
            std::string_view previous;
            for (std::size_t index = 0; index < settings.size(); ++index) {
                const std::string_view group = settings[index].category;
                if (index != 0 && group == previous) {
                    continue;
                }
                previous = group;
                std::size_t groupCount = 0;
                for (std::size_t cursor = index;
                     cursor < settings.size() && std::string_view(settings[cursor].category) == group;
                     ++cursor) {
                    ++groupCount;
                }
                ImGui::PushID(static_cast<int>(index));
                const std::string title(setting_help::categoryTitle(group));
                if (navigationRow(
                        group.empty() ? "General" : title.c_str(),
                        groupCount,
                        group == activeGroup)) {
                    (void)queueUiAction({
                        .kind = RuntimeActionKind::UiSelectRow,
                        .tab = tab,
                        .index = index,
                    });
                }
                ImGui::PopID();
                ImGui::Dummy({ 0.0f, 2.0f });
            }
        }

        [[nodiscard]] const char* numericFormat(const SettingRecord& setting) noexcept
        {
            if (setting.type == SettingType::Integer) {
                return "%.0f";
            }
            if (setting.control.step < 0.01) {
                return "%.3f";
            }
            if (setting.control.step < 0.1) {
                return "%.2f";
            }
            if (setting.control.step < 1.0) {
                return "%.1f";
            }
            return "%.0f";
        }

        // The hand of a ROCK button-layout row, whose options are physical controls.
        [[nodiscard]] std::optional<rpsui::binding::Hand> controllerInputHand(std::string_view section) noexcept
        {
            if (section == "ControllerInput.Left") return rpsui::binding::Hand::Left;
            if (section == "ControllerInput.Right") return rpsui::binding::Hand::Right;
            return std::nullopt;
        }

        // A physical control in a ROCK button-layout list: its icon, then its label.
        [[nodiscard]] bool bindingOption(const setting_control::Option& option, rpsui::binding::Hand hand, bool selected, int id)
        {
            constexpr float height = 40.0f;
            ImGui::PushID(id);
            const bool chosen = ImGui::Selectable("##option", selected, ImGuiSelectableFlags_None, {0.0f, height});
            ImGui::PopID();
            const ImVec2 lo = ImGui::GetItemRectMin();
            auto* draw = ImGui::GetWindowDrawList();
            const auto ink = packed(textColor(1.0f));
            rpsui::binding::drawIcon(draw, {lo.x + height * 0.5f, lo.y + height * 0.5f}, height * 0.85f,
                {.input = {hand, rpsui::binding::parseControl(option.value).value_or(rpsui::binding::Control::None)}}, ink);
            draw->AddText({lo.x + height + 8.0f, lo.y + (height - ImGui::GetFontSize()) * 0.5f}, ink, option.label.c_str());
            return chosen;
        }

        void drawSettingRow(
            const SettingRecord& setting,
            std::size_t index,
            std::size_t activeIndex,
            ConfiguratorTab tab)
        {
            ImGui::PushID(static_cast<int>(index));
            const bool active = index == activeIndex;
            constexpr float rowHeight = 66.0f;
            // Keep rows in the scrolling window. Separate child windows can
            // strand continuous wheel ownership on the parent when hand motion
            // expires ImGui's wheel lock while a row is hovered.
            const ImVec2 rowStart = ImGui::GetCursorPos();
            const ImVec2 windowPosition = ImGui::GetCursorScreenPos();
            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const auto rowCursor = [&](float x, float y) {
                ImGui::SetCursorPos({rowStart.x + x, rowStart.y + y});
            };
            ImGui::BeginGroup();
            ImGui::PushClipRect(windowPosition,
                {windowPosition.x + windowWidth, windowPosition.y + rowHeight}, true);
            const float controlsWidth = (std::min)(420.0f, windowWidth * 0.48f);
            const float controlsStart = windowWidth - controlsWidth;
            auto* draw = ImGui::GetWindowDrawList();
            if (active) draw->AddRectFilled(windowPosition,
                {windowPosition.x + windowWidth, windowPosition.y + rowHeight}, packed(accentColor(0.055f)));
            draw->AddLine({windowPosition.x + 12, windowPosition.y + rowHeight - 1},
                {windowPosition.x + windowWidth - 12, windowPosition.y + rowHeight - 1},
                packed(mutedColor(0.15f)));
            draw->PushClipRect({windowPosition.x + 12, windowPosition.y},
                {windowPosition.x + controlsStart - 14, windowPosition.y + rowHeight}, true);
            rowCursor(14, 8);
            {
                ScopedFont font(devui::render::FontRole::Medium, 25);
                if (!setting.label.empty()) ImGui::TextUnformatted(setting.label.c_str());
                else if (!setting.componentLabel.empty()) {
                    const devui::visual::ReadableLabel label(setting.key, true);
                    ImGui::Text("%s %s", label.text.data(), setting.componentLabel.c_str());
                } else {
                    const devui::visual::ReadableLabel label(setting.key, true);
                    ImGui::TextUnformatted(label.text.data());
                }
            }
            draw->PopClipRect();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
                ImGui::TextUnformatted(setting.key.c_str());
                if (!setting.description.empty()) ImGui::TextWrapped("%s", setting.description.c_str());
                if (setting.fromRockApi) {
                    ImGui::Text("Default: %s", setting.defaultValue.c_str());
                    ImGui::TextUnformatted(setting.overridden ? "Saved override" : "Using default");
                }
                ImGui::TextDisabled("%s", setting.id.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            rowCursor(14, 37);
            draw->PushClipRect({windowPosition.x + 12, windowPosition.y}, {windowPosition.x + controlsStart - 14, windowPosition.y + rowHeight}, true);
            {
                ScopedFont font(devui::render::FontRole::Body, 18);
                // The setting's own line, without its title or a group heading; the tooltip has the rest.
                const auto summary = setting_help::summary(setting.helpLines);
                if (!summary.empty()) ImGui::TextColored(mutedColor(0.85f), "%.*s", static_cast<int>(summary.size()), summary.data());
                else ImGui::TextColored(mutedColor(0.85f), "%s", setting.description.empty() ? setting.section.c_str() : setting.description.c_str());
            }

            draw->PopClipRect();
            const auto settingTarget = settingTargetLocked(index);
            // A value changed from the mod's default can go back to it.
            if (setting.fromRockApi && setting.overridden && !setting.vectorComponent && !setting.defaultValue.empty()) {
                rowCursor(controlsStart - 100.0f, 14.0f);
                ScopedFont font(devui::render::FontRole::Body, 16);
                if (devui::visual::button("Default##reset", { 86.0f, 36.0f })) {
                    (void)queueUiAction({
                        .kind = RuntimeActionKind::UiResetRow,
                        .tab = tab,
                        .index = index,
                        .horizontalTarget = settingTarget,
                    });
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to %s", setting.defaultValue.c_str());
            }
            switch (setting.control.kind) {
            case setting_control::Kind::Checkbox: {
                bool enabled = setting_control::equalsIgnoreCase(setting.value, "true") ||
                               setting.value == "1";
                rowCursor(windowWidth - 61.0f, 10.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 6.0f, 6.0f });
                if (devui::visual::checkbox("##checkbox", &enabled)) {
                    (void)queueUiAction({
                        .kind = RuntimeActionKind::UiSetBooleanRow,
                        .tab = tab,
                        .value = enabled ? 1 : 0,
                        .index = index,
                        .horizontalTarget = settingTarget,
                    });
                }
                ImGui::PopStyleVar();
                break;
            }
            case setting_control::Kind::Numeric: {
                rowCursor(controlsStart, 10.0f);
                if (devui::visual::button("-", { 36.0f, 36.0f })) {
                    (void)queueUiAction({
                        .kind = RuntimeActionKind::UiAdjustRow,
                        .tab = tab,
                        .value = -1,
                        .index = index,
                        .horizontalTarget = settingTarget,
                    });
                }
                ImGui::SameLine();
                double numericValue = setting.numericValue;
                if (s_pendingNumericEdit && s_pendingNumericEdit->id == setting.id) {
                    numericValue = s_pendingNumericEdit->value;
                }
                ImGui::SetNextItemWidth(controlsWidth - 110.0f);
                const bool changed = setting.control.bounded ?
                    ImGui::SliderScalar(
                        "##numeric",
                        ImGuiDataType_Double,
                        &numericValue,
                        &setting.control.minimum,
                        &setting.control.maximum,
                        numericFormat(setting),
                        ImGuiSliderFlags_AlwaysClamp) :
                    ImGui::DragScalar(
                        "##numeric",
                        ImGuiDataType_Double,
                        &numericValue,
                        static_cast<float>(setting.control.step),
                        nullptr,
                        nullptr,
                        numericFormat(setting));
                if (changed) {
                    numericValue = setting_control::snapNumeric(setting.control, numericValue);
                    s_pendingNumericEdit = PendingNumericEdit{
                        .id = setting.id,
                        .index = index,
                        .target = settingTarget,
                        .value = numericValue,
                    };
                }
                const bool commit = ImGui::IsItemDeactivatedAfterEdit() &&
                    s_pendingNumericEdit && s_pendingNumericEdit->id == setting.id;
                if (commit) {
                    const auto pending = *s_pendingNumericEdit;
                    if (!queueUiAction({
                            .kind = RuntimeActionKind::UiSetNumericRow,
                            .tab = tab,
                            .index = pending.index,
                            .horizontalTarget = pending.target,
                            .numericValue = pending.value,
                        })) {
                        s_statusMessage = "Numeric change was not queued; try again";
                    }
                    s_pendingNumericEdit.reset();
                }
                ImGui::SameLine();
                if (devui::visual::button("+", { 36.0f, 36.0f })) {
                    (void)queueUiAction({
                        .kind = RuntimeActionKind::UiAdjustRow,
                        .tab = tab,
                        .value = 1,
                        .index = index,
                        .horizontalTarget = settingTarget,
                    });
                }
                break;
            }
            case setting_control::Kind::Dropdown: {
                const char* preview = setting.value.c_str();
                if (setting.selectedOptionIndex &&
                    *setting.selectedOptionIndex < setting.control.options.size()) {
                    preview = setting.control.options[*setting.selectedOptionIndex].label.c_str();
                }
                // ROCK's button layout: each option is a physical control, shown with its icon.
                const auto bindingHand = controllerInputHand(setting.section);
                float comboStart = controlsStart, comboWidth = controlsWidth - 20.0f;
                if (bindingHand) {
                    constexpr float iconSize = 44.0f;
                    rpsui::binding::drawIcon(draw, {windowPosition.x + controlsStart + iconSize * 0.5f, windowPosition.y + 30.0f}, iconSize,
                        {.input = {*bindingHand, rpsui::binding::parseControl(setting.value).value_or(rpsui::binding::Control::None)}},
                        packed(textColor(1.0f)));
                    comboStart += iconSize + 10.0f;
                    comboWidth -= iconSize + 10.0f;
                }
                rowCursor(comboStart, 10.0f);
                ImGui::SetNextItemWidth(comboWidth);
                if (ImGui::BeginCombo("##dropdown", preview)) {
                    for (std::size_t optionIndex = 0;
                         optionIndex < setting.control.options.size(); ++optionIndex) {
                        const bool selected = setting.selectedOptionIndex &&
                            *setting.selectedOptionIndex == optionIndex;
                        const auto& option = setting.control.options[optionIndex];
                        if (bindingHand ? bindingOption(option, *bindingHand, selected, static_cast<int>(optionIndex)) :
                                ImGui::Selectable(option.label.c_str(), selected)) {
                            (void)queueUiAction({
                                .kind = RuntimeActionKind::UiSetOptionRow,
                                .tab = tab,
                                .value = static_cast<int>(optionIndex),
                                .index = index,
                                .horizontalTarget = settingTarget,
                            });
                        }
                        if (selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case setting_control::Kind::Text: {
                const ImVec2 valueMinimum{
                    windowPosition.x + controlsStart,
                    windowPosition.y + 16.0f
                };
                const ImVec2 valueMaximum{
                    windowPosition.x + windowWidth - 20.0f,
                    valueMinimum.y + 40.0f
                };
                draw->AddRectFilled(
                    valueMinimum,
                    valueMaximum,
                    packed(devui::visual::surface()));
                draw->AddRect(
                    valueMinimum,
                    valueMaximum,
                    packed(rpsui::settings_menu::theme::border()));
                draw->PushClipRect(
                    { valueMinimum.x + 10.0f, valueMinimum.y },
                    { valueMaximum.x - 10.0f, valueMaximum.y },
                    true);
                draw->AddText(
                    fontFor(devui::render::FontRole::Mono),
                    16.0f,
                    { valueMinimum.x + 12.0f, valueMinimum.y + 10.0f },
                    packed(textColor(0.82f)),
                    setting.value.c_str());
                draw->PopClipRect();
                break;
            }
            }

            ImGui::PopClipRect();
            ImGui::EndGroup();
            ImGui::SetCursorPos(rowStart);
            ImGui::Dummy({windowWidth, rowHeight});
            ImGui::PopID();
        }

        // A choice shows its options as cards: booleans, and short lists that are not button layouts.
        [[nodiscard]] bool isChoice(const SettingRecord& setting) noexcept
        {
            if (setting.vectorComponent) return false;
            if (setting.control.kind == setting_control::Kind::Checkbox) return true;
            return setting.control.kind == setting_control::Kind::Dropdown && !controllerInputHand(setting.section) &&
                   !setting.control.options.empty() && setting.control.options.size() <= 7;
        }

        // One option of a choice: its name and, under it, the line the mod wrote for it.
        [[nodiscard]] bool choiceCard(const char* label, std::string_view line, bool chosen, ImVec2 size)
        {
            const auto start = ImGui::GetCursorScreenPos();
            const bool pressed = ImGui::InvisibleButton("##card", size);
            const bool hovered = ImGui::IsItemHovered();
            const auto& theme = rpsui::settings_menu::theme::current();
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end{ start.x + size.x, start.y + size.y };
            const float rounding = (std::max)(theme.rounding, 6.0f);
            const auto fill = chosen ? accentColor(0.18f) :
                hovered ? rpsui::settings_menu::theme::role(theme.hover, 0.85f) : devui::visual::surface(0.75f);
            draw->AddRectFilled(start, end, packed(fill), rounding);
            draw->AddRect(start, end, packed(chosen ? accentColor() : rpsui::settings_menu::theme::border()), rounding,
                chosen ? (std::max)(2.0f, theme.borderWidth * 2.0f) : theme.borderWidth);
            draw->PushClipRect(start, end, true);
            auto* medium = fontFor(devui::render::FontRole::Medium);
            draw->AddText(medium, 23.0f, { start.x + 16.0f, start.y + 12.0f }, packed(chosen ? accentColor() : textColor(1.0f)), label);
            if (!line.empty()) {
                // Help lines continue a sentence after "label: "; a card starts it.
                std::string text(line);
                if (text[0] >= 'a' && text[0] <= 'z') text[0] = static_cast<char>(text[0] - 'a' + 'A');
                draw->AddText(fontFor(devui::render::FontRole::Body), 18.0f, { start.x + 16.0f, start.y + 44.0f },
                    packed(mutedColor(0.95f)), text.c_str(), nullptr, size.x - 32.0f);
            }
            draw->PopClipRect();
            return pressed;
        }

        void drawChoice(const SettingRecord& setting, std::size_t index, ConfiguratorTab tab)
        {
            namespace help = setting_help;
            ImGui::PushID(static_cast<int>(index));
            const std::span<const std::string> lines(setting.helpLines);
            const auto title = help::title(lines);
            {
                ScopedFont font(devui::render::FontRole::Medium, 26);
                if (!title.empty()) ImGui::TextUnformatted(title.data(), title.data() + title.size());
                else if (!setting.label.empty()) ImGui::TextUnformatted(setting.label.c_str());
                else {
                    const devui::visual::ReadableLabel label(setting.key, true);
                    ImGui::TextUnformatted(label.text.data());
                }
            }
            if (ImGui::IsItemHovered() && !setting.description.empty()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
                ImGui::TextWrapped("%s", setting.description.c_str());
                ImGui::TextDisabled("%s", setting.id.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            const bool toggle = setting.control.kind == setting_control::Kind::Checkbox;
            const auto summary = help::summary(lines);
            if (!toggle && !summary.empty()) {
                ScopedFont font(devui::render::FontRole::Body, 18);
                ImGui::PushStyleColor(ImGuiCol_Text, mutedColor(0.95f));
                ImGui::TextWrapped("%.*s", static_cast<int>(summary.size()), summary.data());
                ImGui::PopStyleColor();
            }
            ImGui::Dummy({ 0.0f, 4.0f });

            struct Card { std::string label; std::string_view line; bool chosen; int value; };
            std::vector<Card> cards;
            if (toggle) {
                const bool on = setting_control::equalsIgnoreCase(setting.value, "true") || setting.value == "1";
                cards.push_back({ "On", summary, on, 1 });
                cards.push_back({ "Off", help::offLine(lines), !on, 0 });
            } else {
                for (std::size_t option = 0; option < setting.control.options.size(); ++option) {
                    // Lists read from "N = label" carry "N — label"; a card shows the label alone.
                    const auto& choice = setting.control.options[option];
                    const std::string numbered = choice.value + " — ";
                    const std::string label = choice.label.starts_with(numbered) ? choice.label.substr(numbered.size()) : choice.label;
                    cards.push_back({ label, help::choiceLine(lines, label),
                        setting.selectedOptionIndex && *setting.selectedOptionIndex == option, static_cast<int>(option) });
                }
            }
            const bool described = std::ranges::any_of(cards, [](const Card& card) { return !card.line.empty(); });
            constexpr float gap = 12.0f;
            const float width = ImGui::GetContentRegionAvail().x - 6.0f;
            const std::size_t columns = described ? (std::min<std::size_t>)(cards.size(), 3) : (std::min<std::size_t>)(cards.size(), 4);
            const float cardWidth = (width - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
            // Every card of a choice is as tall as its longest line needs.
            float cardHeight = 56.0f;
            if (described) {
                auto* body = fontFor(devui::render::FontRole::Body);
                for (const auto& card : cards) {
                    if (card.line.empty()) continue;
                    const float text = body->CalcTextSizeA(18.0f, FLT_MAX, cardWidth - 32.0f, card.line.data(), card.line.data() + card.line.size()).y;
                    cardHeight = (std::max)(cardHeight, 44.0f + text + 16.0f);
                }
            }
            const auto target = settingTargetLocked(index);
            for (std::size_t i = 0; i < cards.size(); ++i) {
                if (i % columns) ImGui::SameLine(0.0f, gap);
                ImGui::PushID(static_cast<int>(i));
                if (choiceCard(cards[i].label.c_str(), cards[i].line, cards[i].chosen, { cardWidth, cardHeight }) && !cards[i].chosen) {
                    (void)queueUiAction({
                        .kind = toggle ? RuntimeActionKind::UiSetBooleanRow : RuntimeActionKind::UiSetOptionRow,
                        .tab = tab,
                        .value = cards[i].value,
                        .index = index,
                        .horizontalTarget = target,
                    });
                }
                ImGui::PopID();
                if (i + 1 < cards.size() && (i + 1) % columns == 0) ImGui::Dummy({ 0.0f, gap - ImGui::GetStyle().ItemSpacing.y });
            }
            ImGui::PopID();
        }

        // The fold over a page's numbers and text. True when pressed.
        [[nodiscard]] bool fineTuningHeader(std::size_t count, bool open)
        {
            ImGui::Dummy({ 0.0f, 6.0f });
            const auto start = ImGui::GetCursorScreenPos();
            const ImVec2 size{ ImGui::GetContentRegionAvail().x - 6.0f, 54.0f };
            const bool pressed = ImGui::InvisibleButton("##fine-tuning", size);
            const bool hovered = ImGui::IsItemHovered();
            const auto& theme = rpsui::settings_menu::theme::current();
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end{ start.x + size.x, start.y + size.y };
            const float rounding = (std::max)(theme.rounding, 6.0f);
            draw->AddRectFilled(start, end, packed(hovered ? rpsui::settings_menu::theme::role(theme.hover, 0.85f) : devui::visual::surface(0.6f)), rounding);
            draw->AddRect(start, end, packed(rpsui::settings_menu::theme::border()), rounding, theme.borderWidth);
            const float y = start.y + size.y * 0.5f;
            const ImVec2 mark{ start.x + 26.0f, y };
            if (open) draw->AddTriangleFilled({ mark.x - 8, y - 4 }, { mark.x + 8, y - 4 }, { mark.x, y + 6 }, packed(accentColor()));
            else draw->AddTriangleFilled({ mark.x - 4, y - 8 }, { mark.x - 4, y + 8 }, { mark.x + 6, y }, packed(accentColor()));
            draw->AddText(fontFor(devui::render::FontRole::Medium), 22.0f, { start.x + 46.0f, y - 12.0f }, packed(textColor(1.0f)), "Fine-tuning");
            const auto detail = std::format("{} value{}, for adjusting", count, count == 1 ? "" : "s");
            draw->AddText(fontFor(devui::render::FontRole::Body), 18.0f, { start.x + 190.0f, y - 10.0f }, packed(mutedColor(0.9f)), detail.c_str());
            ImGui::Dummy({ 0.0f, 6.0f });
            return pressed;
        }

        template <class Store>
        void drawSettingsWorkspace(
            const Store& store,
            bool loaded,
            std::size_t activeIndex,
            ConfiguratorTab tab,
            const std::string& status)
        {
            const auto& settings = store.settings();
            if (!loaded || settings.empty()) {
                {
                    ScopedFont heading(devui::render::FontRole::Heading, 27.0f);
                    ImGui::TextColored(accentColor(), "NO DATA");
                }
                {
                    ScopedFont body(devui::render::FontRole::Body, 17.0f);
                    ImGui::TextWrapped("%s", status.c_str());
                }
                return;
            }
            const std::string_view group =
                activeIndex < settings.size() ? settings[activeIndex].category : std::string_view{};
            const std::string groupTitle(group.empty() ? std::string_view("General") : setting_help::categoryTitle(group));
            devui::visual::heading(groupTitle.c_str(), status.c_str());
            ImGui::Dummy({ 0.0f, 6.0f });
            ImGui::BeginChild("settings-rows", { 0.0f, 0.0f }, ImGuiChildFlags_None);
            const auto groupId = ImGui::GetID(group.data(), group.data() + group.size());
            auto* storage = ImGui::GetStateStorage();
            const auto groupKey = ImGui::GetID("current-group");
            if (storage->GetInt(groupKey) != static_cast<int>(groupId)) {
                ImGui::SetScrollY(0);
                storage->SetInt(groupKey, static_cast<int>(groupId));
            }
            // Choices first, as the setup wizard shows them; numbers and text under Fine-tuning.
            std::vector<std::size_t> choices, values;
            for (std::size_t index = 0; index < settings.size(); ++index) {
                if (std::string_view(settings[index].category) != group) continue;
                (isChoice(settings[index]) ? choices : values).push_back(index);
            }
            for (const auto index : choices) {
                drawChoice(settings[index], index, tab);
                ImGui::Dummy({ 0.0f, 14.0f });
            }
            if (!values.empty()) {
                const auto foldKey = ImGui::GetID("fine-tuning");
                bool open = choices.empty() || storage->GetBool(foldKey, false) || s_previewFineTuning;
                if (!choices.empty() && fineTuningHeader(values.size(), open)) {
                    open = !open;
                    storage->SetBool(foldKey, open);
                }
                if (open) {
                    for (const auto index : values) {
                        drawSettingRow(settings[index], index, activeIndex, tab);
                        ImGui::Dummy({ 0.0f, 3.0f });
                    }
                }
            }
            ImGui::EndChild();
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
                if (!drawn) (void)queueUiAction({ .kind = RuntimeActionKind::UiSelectTab, .tab = ConfiguratorTab::Settings });
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
            const float railWidth = devui::visual::railWidth(ImGui::GetContentRegionAvail().x);
            const auto tab = s_activeTab.load(std::memory_order_acquire);
            if (tab == ConfiguratorTab::Settings) {
                ImGui::BeginChild("rps-mod-tabs", {0, 64});
                const auto visible=static_cast<float>(std::count_if(s_modSettings.begin(),s_modSettings.end(),[](const auto& settings){return settings.available;}));
                const float chipWidth=(std::min)(156.f,(ImGui::GetContentRegionAvail().x-36.f-12.f*(std::max)(0.f,visible-1.f))/(std::max)(1.f,visible));
                ImGui::SetCursorPos({18, 4});
                bool first = true;
                for (const auto mod : kRpsModDisplayOrder) {
                    const auto i=static_cast<std::size_t>(mod);
                    if (!s_modSettings[i].available) continue;
                    if (!first) ImGui::SameLine(0, 12);
                    first = false;
                    if (tabChip(kRpsMods[i].name, s_modIndex == i, {chipWidth, 52}))
                        (void)queueUiAction({.kind = RuntimeActionKind::UiSelectMod, .index = i});
                }
                if (first) ImGui::TextUnformatted("No supported RPS mods are loaded.");
                ImGui::EndChild();
                if (!activeSettings().available) return;
            }
            // Keep row/scroll/widget identities independent even for identical INI keys.
            ImGui::PushID(tab == ConfiguratorTab::Settings ? static_cast<int>(s_modIndex) : -1);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 18.0f, 24.0f });
            ImGui::PushStyleColor(ImGuiCol_ChildBg, devui::visual::surface(0.7f));
            ImGui::PushStyleColor(ImGuiCol_Border, rpsui::settings_menu::theme::border());
            ImGui::BeginChild("rail", { railWidth, 0.0f }, ImGuiChildFlags_AlwaysUseWindowPadding);
            switch (tab) {
            case ConfiguratorTab::Settings:
                drawSettingsRail(activeSettings().store, activeSettings().loaded, activeSettings().activeIndex, tab, s_statusMessage);
                break;
            case ConfiguratorTab::Spawn:
                drawSpawnRail();
                break;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::SameLine(0,0);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Border, rpsui::settings_menu::theme::border());
            ImGui::BeginChild("workspace", { 0.0f, 0.0f }, ImGuiChildFlags_AlwaysUseWindowPadding);
            switch (tab) {
            case ConfiguratorTab::Settings:
                drawSettingsWorkspace(activeSettings().store, activeSettings().loaded, activeSettings().activeIndex, tab, s_statusMessage);
                break;
            case ConfiguratorTab::Spawn:
                drawSpawnWorkspace();
                break;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            ImGui::PopID();
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
            if (s_activeTab.load() == ConfiguratorTab::Settings && !s_pendingNumericEdit &&
                activeSettings().store.needsReload() && !s_settingsRefreshQueued.exchange(true)) {
                if (!queueUiAction({.kind = RuntimeActionKind::UiRefreshSettings})) s_settingsRefreshQueued.store(false);
            }
            ScopedFont configFont(devui::render::FontRole::Body, 26);
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            if (backRequested) {
                auto& context = *ImGui::GetCurrentContext();
                if (!context.OpenPopupStack.empty()) {
                    ImGui::ClosePopupToLevel(context.OpenPopupStack.Size - 1, true);
                } else if (context.ActiveId != 0) {
                    ImGui::ClearActiveID();
                    s_pendingNumericEdit.reset();
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

    void onGameSessionReady()
    {
        std::scoped_lock lock(s_runtimeMutex);
        loadRpsSettingsLocked(false);
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
    void initializePreview(const std::filesystem::path& settingsPath)
    {
        std::array<std::filesystem::path, 2> paths{settingsPath, {}};
        initializeRpsPreview(paths, {true, settingsPath.empty()});
    }

    void initializeRpsPreview(const std::array<std::filesystem::path, 2>& paths, const std::array<bool, 2>& available,
        const rock::api::configuration::Api* rockApi, const std::filesystem::path& csahPath,
        const std::filesystem::path& csahCatalog)
    {
        std::scoped_lock lock(s_runtimeMutex);
        invalidateQueuedRuntimeActions();
        s_modIndex = 0;
        s_activeTab = ConfiguratorTab::Settings;
        for (std::size_t i=0;i<s_modSettings.size();++i)
            s_modSettings[i]=ModSettings{IniSettingsStore({},kRpsMods[i].id)};
        auto& developer = s_modSettings[static_cast<std::size_t>(RpsMod::RockDeveloper)];
        developer = ModSettings{IniSettingsStore({}, RpsMod::RockDeveloper, rockApi, {}, rockApi?1:0)};
        s_previewPresent.fill(false);
        s_previewPresent[static_cast<std::size_t>(RpsMod::RockDeveloper)] = available[0] && (paths[0].empty() || rockApi != nullptr);
        auto& csah = s_modSettings[static_cast<std::size_t>(RpsMod::Csah)];
        csah = ModSettings{IniSettingsStore(csahPath, RpsMod::Csah, nullptr, csahCatalog)};
        s_previewPresent[static_cast<std::size_t>(RpsMod::Csah)] = !csahPath.empty() && !csahCatalog.empty();
        // paths/available index the Rock and Paper pages, whose enum values are 0..1.
        for (std::size_t i = 0; i < paths.size(); ++i) {
            s_modSettings[i] = ModSettings{IniSettingsStore(paths[i], kRpsMods[i].id, i == 0 ? rockApi : nullptr, {}, rockApi?1:0)};
            s_previewPresent[i] = available[i];
        }
        s_providerInputReady.store(true);
        loadRpsSettingsLocked(false); // Read-only snapshot; preview saves are memory-only.
        s_spawnBrowser.ensureIndexBuilt(); // Compiled preview fixture, no engine calls.
    }

    void initializeFilePreview(const std::filesystem::path& folder)
    {
        std::scoped_lock lock(s_runtimeMutex);
        invalidateQueuedRuntimeActions();
        s_activeTab = ConfiguratorTab::Settings;
        s_previewPresent.fill(false);
        for (std::size_t i = 0; i < s_modSettings.size(); ++i) {
            const auto id = kRpsMods[i].id;
            const bool file = id == RpsMod::Rock || id == RpsMod::RockDeveloper || id == RpsMod::Paper || id == RpsMod::RpsUi;
            // A Mods_Config folder keeps each mod's file in the mod's own folder.
            const auto leaf = std::filesystem::path(kRpsMods[i].directory).filename();
            std::error_code error;
            auto path = file ? folder / leaf / kRpsMods[i].ini : std::filesystem::path{};
            if (file && !std::filesystem::is_regular_file(path, error)) path = folder / kRpsMods[i].ini;
            s_modSettings[i] = ModSettings{ IniSettingsStore(path, id) };
            s_modSettings[i].store.allowPreviewWrites();
            s_previewPresent[i] = file && std::filesystem::is_regular_file(path, error);
        }
        s_modIndex = static_cast<std::size_t>(RpsMod::Rock);
        s_providerInputReady.store(true);
        loadRpsSettingsLocked(false);
        s_spawnBrowser.ensureIndexBuilt();
    }

    void previewShow(std::string_view mod, std::string_view category)
    {
        std::scoped_lock lock(s_runtimeMutex);
        s_activeTab = ConfiguratorTab::Settings;
        for (std::size_t i = 0; i < s_modSettings.size(); ++i) {
            if (kRpsMods[i].name != mod || !s_modSettings[i].available) continue;
            s_modIndex = i;
            const auto& settings = s_modSettings[i].store.settings();
            for (std::size_t index = 0; index < settings.size(); ++index) {
                if (setting_help::categoryTitle(settings[index].category).find(category) != std::string_view::npos) {
                    s_modSettings[i].activeIndex = index;
                    break;
                }
            }
        }
        updateSettingsStatusLocked();
    }

    void previewShowTab(std::string_view tab)
    {
        std::scoped_lock lock(s_runtimeMutex);
        setActiveTabLocked(tab == "Spawner" ? ConfiguratorTab::Spawn : tab == "Console" ? ConfiguratorTab::Console : ConfiguratorTab::Settings);
    }

    void previewOpenFineTuning(bool open) { s_previewFineTuning = open; }

    void previewKeys(std::span<const ConsoleKeyEvent> keys) { serviceConsole(!keys.empty(), true, keys); }

    void setPreviewOpen(bool open) { s_panelOpen.store(open); }
    float previewTabCenterX(unsigned index, float width)
    {
        const auto slot = topBarSlots(width, topBarTabs())[index];
        return slot.x + slot.width * 0.5f;
    }
    void drainPreviewActions() { drainRuntimeActions(); serviceConsole(false, false, {}); }
#endif

    void shutdown()
    {
        close();
        devui::render::Shutdown();
    }
}
