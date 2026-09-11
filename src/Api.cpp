#include "PCH.h"

#include "FrameworkRuntime.h"
#include "Logger.h"
#include "RPSUIFrameworkApi.h"
#include "RPSUICooperationApi.h"

namespace
{
    using namespace rpsui::sdk;

    bool RPSUI_CALL isFrameworkReady() noexcept
    {
        return rpsui::FrameworkRuntime::get().isReady();
    }

    ResultV1 RPSUI_CALL registerConsumer(
        const ConsumerRegistrationV1* registration,
        ConsumerHandleV1* outHandle) noexcept
    {
        if (!registration || !outHandle) {
            return ResultV1::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().registerConsumer(
            *registration,
            *outHandle);
    }

    ResultV1 RPSUI_CALL unregisterConsumer(
        std::uint64_t ownerToken) noexcept
    {
        return rpsui::FrameworkRuntime::get().unregisterConsumer(ownerToken);
    }

    ResultV1 RPSUI_CALL registerPanel(
        std::uint64_t ownerToken,
        const PanelRegistrationV1* registration,
        std::uint64_t* outPanelHandle) noexcept
    {
        if (!registration || !outPanelHandle) {
            return ResultV1::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().registerPanel(
            ownerToken,
            *registration,
            *outPanelHandle);
    }

    ResultV1 RPSUI_CALL unregisterPanel(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) noexcept
    {
        return rpsui::FrameworkRuntime::get().unregisterPanel(
            ownerToken,
            panelHandle);
    }

    ResultV1 RPSUI_CALL submitPanelPresentation(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle,
        const PanelPresentationV1* presentation) noexcept
    {
        if (!presentation) {
            return ResultV1::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().submitPanelPresentation(
            ownerToken,
            panelHandle,
            *presentation);
    }

    ResultV1 RPSUI_CALL resetPanelSize(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle) noexcept
    {
        return rpsui::FrameworkRuntime::get().resetPanelSize(
            ownerToken,
            panelHandle);
    }

    ResultV1 RPSUI_CALL getPanelState(
        std::uint64_t ownerToken,
        std::uint64_t panelHandle,
        PanelStateV1* outState) noexcept
    {
        if (!outState) {
            return ResultV1::InvalidArgument;
        }
        return rpsui::FrameworkRuntime::get().getPanelState(
            ownerToken,
            panelHandle,
            *outState);
    }

    const ApiV1 g_api{
        .featureBits =
            featureMask(FeatureV1::MultipleWorldPanels) |
            featureMask(FeatureV1::SceneDepthOcclusion) |
            featureMask(FeatureV1::CentralPointerRouting) |
            featureMask(FeatureV1::ContextualTwoAxisScroll) |
            featureMask(FeatureV1::PhysicalPanelResize) |
            featureMask(FeatureV1::ConsumerRenderCallbacks) |
            featureMask(FeatureV1::SharedStereoComposition) |
            featureMask(FeatureV1::ShapedPanels),
        .isFrameworkReady = &isFrameworkReady,
        .registerConsumer = &registerConsumer,
        .unregisterConsumer = &unregisterConsumer,
        .registerPanel = &registerPanel,
        .unregisterPanel = &unregisterPanel,
        .submitPanelPresentation = &submitPanelPresentation,
        .resetPanelSize = &resetPanelSize,
        .getPanelState = &getPanelState,
    };

    ResultV1 RPSUI_CALL registerCooperativePanel(std::uint64_t owner,
        const CooperativePanelRegistrationV1* registration, PanelAgreementV1* agreement) noexcept
    {
        if (!agreement || agreement->structSize < sizeof(PanelAgreementV1)) return ResultV1::InvalidArgument;
        if (agreement->apiVersion != RPSUI_COOPERATION_VERSION) return ResultV1::VersionMismatch;
        if (!registration) { *agreement = {}; return ResultV1::InvalidArgument; }
        return rpsui::FrameworkRuntime::get().registerCooperativePanel(owner, *registration, *agreement);
    }
    ResultV1 RPSUI_CALL getCooperationSnapshot(CooperationSnapshotV1* snapshot) noexcept
    {
        if (!snapshot || snapshot->structSize < sizeof(CooperationSnapshotV1)) return ResultV1::InvalidArgument;
        if (snapshot->apiVersion != RPSUI_COOPERATION_VERSION) return ResultV1::VersionMismatch;
        return rpsui::FrameworkRuntime::get().getCooperationSnapshot(*snapshot);
    }
    ResultV1 RPSUI_CALL unregisterPanelSafely(std::uint64_t owner, std::uint64_t panel) noexcept
    { return rpsui::FrameworkRuntime::get().unregisterPanelSafely(owner, panel); }
    ResultV1 RPSUI_CALL unregisterConsumerSafely(std::uint64_t owner) noexcept
    { return rpsui::FrameworkRuntime::get().unregisterConsumerSafely(owner); }

    const CooperationApiV1 g_cooperation{
        .registerPanel = &registerCooperativePanel,
        .getSnapshot = &getCooperationSnapshot,
        .unregisterPanelSafely = &unregisterPanelSafely,
        .unregisterConsumerSafely = &unregisterConsumerSafely,
    };
}

extern "C" DLLEXPORT const rpsui::sdk::ApiV1* RPSUI_CALL
RPSUI_RequestApi(std::uint32_t requestedVersion) noexcept
{
    return requestedVersion == rpsui::sdk::RPSUI_API_VERSION ?
        std::addressof(g_api) :
        nullptr;
}

extern "C" DLLEXPORT const rpsui::sdk::CooperationApiV1* RPSUI_CALL
RPSUI_RequestCooperationApi(std::uint32_t requestedVersion) noexcept
{
    return requestedVersion == rpsui::sdk::RPSUI_COOPERATION_VERSION ? &g_cooperation : nullptr;
}
