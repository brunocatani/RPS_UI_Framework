#include "PCH.h"

#include "FrameworkRuntime.h"
#include "Logger.h"
#include "RPSUIFrameworkApi.h"

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
}

extern "C" DLLEXPORT const rpsui::sdk::ApiV1* RPSUI_CALL
RPSUI_RequestApi(std::uint32_t requestedVersion) noexcept
{
    return requestedVersion == rpsui::sdk::RPSUI_API_VERSION ?
        std::addressof(g_api) :
        nullptr;
}
