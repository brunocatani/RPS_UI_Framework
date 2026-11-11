#include "PCH.h"

#include "FrameworkRuntime.h"
#include "InputService.h"
#include "InterfaceNegotiation.h"
#include "Logger.h"
#include "RequiredUpdates.h"
#include "SettingsMenuHost.h"
#include "WorldDrawStore.h"
#include "settings_menu/ExternalPages.h"
#include <RPS_UI_Framework/Cooperation.h>
#include <RPS_UI_Framework/FocusedPanel.h>
#include <RPS_UI_Framework/Framework.h>
#include <RPS_UI_Framework/NumberOverlay.h>
#include <RPS_UI_Framework/PassivePanel.h>
#include <RPS_UI_Framework/Registry.h>
#include <RPS_UI_Framework/SettingsMenu.h>
#include <RPS_UI_Framework/WorldDraw.h>

#include <array>
#include <intrin.h>

namespace
{
    using namespace rpsui::api;

    bool RPSUI_CALL isFrameworkReady() noexcept
    {
        return rpsui::FrameworkRuntime::get().isReady();
    }

    Result RPSUI_CALL registerConsumer(
        const framework::ConsumerRegistration* registration,
        framework::ConsumerHandle* outHandle) noexcept
    {
        if (!registration || !outHandle) {
            return Result::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().registerConsumer(
            *registration,
            *outHandle);
    }

    Result RPSUI_CALL unregisterConsumer(
        std::uint64_t ownerToken) noexcept
    {
        const auto result = rpsui::FrameworkRuntime::get().unregisterConsumer(ownerToken);
        if (result == Result::Ok) rpsui::settings_menu::external_pages::removeOwner(ownerToken);
        return result;
    }

    // A refused Dear ImGui build stops the game like an outdated consumer.
    Result notePanelRegistration(Result result, const framework::PanelRegistration& registration,
        const void* callerAddress) noexcept
    {
        if (result == Result::ImGuiBuildMismatch) {
            char panelId[sizeof(registration.panelId) + 1]{};
            std::memcpy(panelId, registration.panelId, sizeof(registration.panelId));
            rpsui::required_updates::noteImGuiBuildMismatch(panelId, callerAddress);
        }
        return result;
    }

    Result RPSUI_CALL registerPanel(
        std::uint64_t ownerToken,
        const framework::PanelRegistration* registration,
        std::uint64_t* outPanelHandle) noexcept
    {
        if (!registration || !outPanelHandle) {
            return Result::InvalidArgument;
        }
        return notePanelRegistration(rpsui::FrameworkRuntime::get().registerPanel(
            ownerToken,
            *registration,
            *outPanelHandle), *registration, _ReturnAddress());
    }

    Result RPSUI_CALL unregisterPanel(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) noexcept
    {
        return rpsui::FrameworkRuntime::get().unregisterPanel(
            ownerToken,
            panelHandle);
    }

    Result RPSUI_CALL submitPanelPresentation(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle,
        const framework::PanelPresentation* presentation) noexcept
    {
        if (!presentation) {
            return Result::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().submitPanelPresentation(
            ownerToken,
            panelHandle,
            *presentation);
    }

    Result RPSUI_CALL resetPanelLayout(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) noexcept
    {
        return rpsui::FrameworkRuntime::get().resetPanelLayout(
            ownerToken,
            panelHandle);
    }

    Result RPSUI_CALL pulseHaptic(
        std::uint64_t ownerToken,
        framework::PhysicalHand hand,
        float strength) noexcept
    {
        return rpsui::FrameworkRuntime::get().pulseHaptic(ownerToken, hand, strength);
    }

    Result RPSUI_CALL getPanelState(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle,
        framework::PanelState* outState) noexcept
    {
        if (!outState) {
            return Result::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().getPanelState(
            ownerToken,
            panelHandle,
            *outState);
    }

    const framework::Api g_api{
        .isFrameworkReady = &isFrameworkReady,
        .registerConsumer = &registerConsumer,
        .unregisterConsumer = &unregisterConsumer,
        .registerPanel = &registerPanel,
        .unregisterPanel = &unregisterPanel,
        .submitPanelPresentation = &submitPanelPresentation,
        .resetPanelLayout = &resetPanelLayout,
        .getPanelState = &getPanelState,
        .pulseHaptic = &pulseHaptic,
    };

    Result RPSUI_CALL registerCooperativePanel(std::uint64_t owner,
        const cooperation::PanelRegistration* registration, cooperation::PanelAgreement* agreement) noexcept
    {
        if (!agreement || agreement->size < sizeof(cooperation::PanelAgreement)) return Result::InvalidArgument;
        if (agreement->version != kRecordVersion) return Result::VersionMismatch;
        if (!registration) { *agreement = {}; return Result::InvalidArgument; }
        return notePanelRegistration(rpsui::FrameworkRuntime::get().registerCooperativePanel(owner, *registration,
            *agreement), registration->panel, _ReturnAddress());
    }
    Result RPSUI_CALL getCooperationSnapshot(cooperation::Snapshot* snapshot) noexcept
    {
        if (!snapshot || snapshot->size < sizeof(cooperation::Snapshot)) return Result::InvalidArgument;
        if (snapshot->version != kRecordVersion) return Result::VersionMismatch;
        return rpsui::FrameworkRuntime::get().getCooperationSnapshot(*snapshot);
    }
    Result RPSUI_CALL unregisterPanelSafely(std::uint64_t owner, std::uint64_t panel) noexcept
    { return rpsui::FrameworkRuntime::get().unregisterPanelSafely(owner, panel); }
    Result RPSUI_CALL unregisterConsumerSafely(std::uint64_t owner) noexcept
    {
        const auto result = rpsui::FrameworkRuntime::get().unregisterConsumerSafely(owner);
        if (result == Result::Ok) rpsui::settings_menu::external_pages::removeOwner(owner);
        return result;
    }

    const cooperation::Api g_cooperation{
        .registerPanel = &registerCooperativePanel,
        .getSnapshot = &getCooperationSnapshot,
        .unregisterPanelSafely = &unregisterPanelSafely,
        .unregisterConsumerSafely = &unregisterConsumerSafely,
    };

    Result RPSUI_CALL getRegistryPage(const registry::Query* query, registry::Page* page) noexcept
    {
        if (!query || !page) return Result::InvalidArgument;
        return rpsui::FrameworkRuntime::get().getRegistryPage(*query, *page);
    }
    const registry::Api g_registry{ .getPage = &getRegistryPage };

    Result RPSUI_CALL registerNumberOverlay(std::uint64_t owner,
        const number_overlay::Registration* registration,std::uint64_t* handle) noexcept
    {
        if (!registration || !handle) return Result::InvalidArgument;
        return rpsui::FrameworkRuntime::get().registerNumberOverlay(owner,*registration,*handle);
    }
    Result RPSUI_CALL submitNumberOverlay(std::uint64_t owner,std::uint64_t handle,
        const number_overlay::Presentation* presentation) noexcept
    {
        if (!presentation) return Result::InvalidArgument;
        return rpsui::FrameworkRuntime::get().submitNumberOverlay(owner,handle,*presentation);
    }
    Result RPSUI_CALL registerPassivePanel(std::uint64_t owner,
        const framework::PanelRegistration* registration, std::uint64_t* handle) noexcept
    {
        if (!registration || !handle) return Result::InvalidArgument;
        return notePanelRegistration(rpsui::FrameworkRuntime::get().registerPassivePanel(owner, *registration, *handle),
            *registration, _ReturnAddress());
    }
    const passive_panel::Api g_passive{.registerPanel = &registerPassivePanel};
    Result RPSUI_CALL registerFocusedPanel(std::uint64_t owner,
        const framework::PanelRegistration* registration, std::uint64_t* handle) noexcept
    {
        if (!registration || !handle) return Result::InvalidArgument;
        return notePanelRegistration(rpsui::FrameworkRuntime::get().registerFocusedPanel(owner, *registration, *handle),
            *registration, _ReturnAddress());
    }
    const focused_panel::Api g_focused{.registerPanel = &registerFocusedPanel};
    Result RPSUI_CALL submitWorldDraw(std::uint64_t owner, const world_draw::Publication* publication) noexcept
    {
        if (!publication) return Result::InvalidArgument;
        return rpsui::world_draw::store().submit(owner, *publication, rpsui::world_draw::currentFrame());
    }
    Result RPSUI_CALL clearWorldDraw(std::uint64_t owner) noexcept
    { return rpsui::world_draw::store().clear(owner); }
    Result RPSUI_CALL registerWorldMesh(std::uint64_t owner, std::uint64_t meshId, const world_draw::Mesh* mesh) noexcept
    {
        if (!mesh) return Result::InvalidArgument;
        return rpsui::world_draw::store().registerMesh(owner, meshId, *mesh);
    }
    Result RPSUI_CALL releaseWorldMesh(std::uint64_t owner, std::uint64_t meshId) noexcept
    { return rpsui::world_draw::store().releaseMesh(owner, meshId); }
    Result RPSUI_CALL setWorldDrawFrameCallback(std::uint64_t owner, world_draw::FrameCallback callback,
        void* context) noexcept
    { return rpsui::world_draw::store().setFrameCallback(owner, callback, context); }
    const world_draw::Api g_worldDraw{
        .submit = &submitWorldDraw,
        .clear = &clearWorldDraw,
        .registerMesh = &registerWorldMesh,
        .releaseMesh = &releaseWorldMesh,
        .setFrameCallback = &setWorldDrawFrameCallback,
    };

    const number_overlay::Api g_numbers{
        .registerOverlay=&registerNumberOverlay,
        .submit=&submitNumberOverlay,
        .unregisterOverlay=&unregisterPanel,
    };

    [[nodiscard]] bool finite3(const float (&value)[3]) noexcept
    {
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    }

    Result RPSUI_CALL openSettingsMenu(std::uint64_t owner, const settings_menu::OpenRequest* request) noexcept
    {
        if (!request || request->size < sizeof(*request)) return Result::InvalidArgument;
        if (request->version != kRecordVersion) return Result::VersionMismatch;
        if (!rpsui::FrameworkRuntime::get().hasConsumer(owner)) return Result::OwnerNotRegistered;
        if (rpsui::settings_menu_host::isOpen()) return Result::ResourceConflict;
        rpsui::settings_menu_host::Pose pose;
        if (request->hasPose) {
            const auto& value = request->pose;
            if (!finite3(value.center) || !finite3(value.right) || !finite3(value.up) || !finite3(value.front))
                return Result::InvalidPose;
            std::copy_n(value.center, 3, pose.center);
            std::copy_n(value.right, 3, pose.right);
            std::copy_n(value.up, 3, pose.up);
            std::copy_n(value.front, 3, pose.front);
        }
        return rpsui::settings_menu_host::open(request->hasPose ? &pose : nullptr) ? Result::Ok : Result::FrameworkNotReady;
    }
    Result RPSUI_CALL closeSettingsMenu(std::uint64_t owner) noexcept
    {
        if (!rpsui::FrameworkRuntime::get().hasConsumer(owner)) return Result::OwnerNotRegistered;
        rpsui::settings_menu_host::close();
        return Result::Ok;
    }
    bool RPSUI_CALL settingsMenuOpen() noexcept { return rpsui::settings_menu_host::isOpen(); }
    Result RPSUI_CALL registerSettingsPage(std::uint64_t owner, const settings_menu::PageRegistration* registration,
        std::uint64_t* handle) noexcept
    {
        if (!registration || !handle) return Result::InvalidArgument;
        if (!rpsui::FrameworkRuntime::get().hasConsumer(owner)) return Result::OwnerNotRegistered;
        return rpsui::settings_menu::external_pages::add(owner, *registration, *handle);
    }
    Result RPSUI_CALL unregisterSettingsPage(std::uint64_t owner, std::uint64_t handle) noexcept
    { return rpsui::settings_menu::external_pages::remove(owner, handle); }
    std::uint64_t RPSUI_CALL settingsMenuClosedCount() noexcept { return rpsui::settings_menu_host::closedCount(); }
    const settings_menu::Api g_settingsMenu{
        .open = &openSettingsMenu,
        .close = &closeSettingsMenu,
        .isOpen = &settingsMenuOpen,
        .registerPage = &registerSettingsPage,
        .unregisterPage = &unregisterSettingsPage,
        .closedCount = &settingsMenuClosedCount,
    };

    // Any thread; built on first use because the input table lives with the
    // input service.
    std::span<const InterfaceDescriptorV1> interfaces() noexcept
    {
        static const std::array entries{
            rpsui::discovery::describe(g_api),
            rpsui::discovery::describe(g_cooperation),
            rpsui::discovery::describe(g_focused),
            rpsui::discovery::describe(rpsui::input::table()),
            rpsui::discovery::describe(g_numbers),
            rpsui::discovery::describe(g_passive),
            rpsui::discovery::describe(g_registry),
            rpsui::discovery::describe(g_worldDraw),
            rpsui::discovery::describe(g_settingsMenu),
        };
        return entries;
    }
}

extern "C" DLLEXPORT rpsui::api::Result RPSUI_CALL RPSUI_QueryInterfaceV1(const rpsui::api::InterfaceId id,
    const std::uint32_t exactMajor, const std::uint32_t minimumMinor, const std::uint32_t minimumTableBytes,
    const rpsui::api::InterfaceDescriptorV1** output) noexcept
{
    const auto result = rpsui::discovery::query(interfaces(), id, exactMajor, minimumMinor, minimumTableBytes, output);
    if (result != rpsui::api::Result::Ok)
        rpsui::required_updates::noteDiscoveryRefusal(result, id, exactMajor, _ReturnAddress());
    return result;
}

// Retired pre-2.0 exports. They stay exported so a mod still built against them
// is named in the required-update stop instead of failing on a missing symbol;
// each returns no table. Their names are never reused.
extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestApi(std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestCooperationApi(std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestCooperationApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestNumberOverlayApi(std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestNumberOverlayApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestWorldDrawApi(std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestWorldDrawApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestRegistryApi(std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestRegistryApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestPassivePanelApi(std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestPassivePanelApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestFocusedPanelApi(std::uint32_t, std::uint32_t) noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestFocusedPanelApi", _ReturnAddress());
    return nullptr;
}

extern "C" DLLEXPORT const void* RPSUI_CALL RPSUI_RequestInputApi() noexcept
{
    rpsui::required_updates::noteRetiredExport("RPSUI_RequestInputApi", _ReturnAddress());
    return nullptr;
}
