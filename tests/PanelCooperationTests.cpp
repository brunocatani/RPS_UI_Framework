#include "PanelCooperation.h"
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

static_assert(sizeof(rpsui::sdk::ApiV1) == 152);
static_assert(offsetof(rpsui::sdk::ApiV1, isFrameworkReady) == 24);
static_assert(offsetof(rpsui::sdk::ApiV1, reserved) == 88);
static_assert(sizeof(rpsui::sdk::PanelRegistrationV1) == 248);

namespace
{
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

int main()
{
    try {
        using namespace rpsui::sdk;
        CooperativePanelRegistrationV1 registration;
        PanelAgreementV1 agreement;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::Ok, "private panel contract rejected");
        for (const auto access : {RenderAccessV1::SceneColorWrite, RenderAccessV1::SceneDepthWrite, RenderAccessV1::StereoSubmissionHook}) {
            registration.requestedAccess = RPSUI_PANEL_ACCESS | accessMask(access);
            require(rpsui::validatePanelContract(registration, agreement) == ResultV1::ResourceConflict,
                "host-owned resource must conflict");
            require(agreement.panelHandle == 0 && agreement.grantedAccess == 0 &&
                agreement.conflictingAccess == accessMask(access) && std::strcmp(agreement.conflictOwnerId, "rps.ui.framework") == 0,
                "conflict must identify host and refuse grants");
        }
        registration.requestedAccess = RPSUI_PANEL_ACCESS;
        registration.stage = RenderStageV1::StereoOutput;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::ResourceConflict, "stereo stage must conflict");
        registration.stage = RenderStageV1::PanelTexture;
        registration.requestedAccess |= accessMask(RenderAccessV1::SceneDepthRead);
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::UnsupportedStage, "private panel cannot borrow scene depth");
        registration.requestedAccess = 1ull << 63;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::InvalidArgument, "unknown access must fail closed");
        registration.requestedAccess = 0;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::InvalidArgument, "empty access must fail closed");
        registration.requestedAccess = RPSUI_PANEL_ACCESS;
        registration.apiVersion = 99;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::VersionMismatch, "wrong version accepted");
        registration.apiVersion = RPSUI_COOPERATION_VERSION;
        registration.structSize = 0;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::InvalidArgument, "undersized registration accepted");
        registration.structSize = sizeof(registration);
        registration.reserved32 = 1;
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::InvalidArgument, "reserved field accepted");
        registration.reserved32 = 0;
        registration.stage = static_cast<RenderStageV1>(99);
        require(rpsui::validatePanelContract(registration, agreement) == ResultV1::UnsupportedStage, "unknown stage accepted");

        auto first = std::make_shared<rpsui::PanelCallbackGate>();
        auto second = std::make_shared<rpsui::PanelCallbackGate>();
        const auto stale = first; // A renderer copied this before retirement.
        std::latch entered(1), release(1);
        bool admitted = false;
        std::thread rendering([&] {
            rpsui::PanelCallbackScope scope(first);
            admitted = static_cast<bool>(scope);
            entered.count_down();
            release.wait();
        });
        entered.wait();
        const bool initiallyDrained = first->close();
        const bool staleAdmitted = static_cast<bool>(rpsui::PanelCallbackScope(stale));
        const bool otherAdmitted = static_cast<bool>(rpsui::PanelCallbackScope(second));
        const bool stillBusy = !first->close();
        release.count_down();
        rendering.join();
        require(admitted && !initiallyDrained && stillBusy && !staleAdmitted && otherAdmitted,
            "retirement must close admission without waiting for a callback or affecting another panel");
        require(first->close(), "completed callback did not drain");
        require(!rpsui::PanelCallbackScope(stale), "stale snapshot entered after drain");
        require(second->close(), "idle callback must drain without a future render frame");
        require(!rpsui::PanelCallbackScope(second), "idle retired callback reopened");
        auto reentrant = std::make_shared<rpsui::PanelCallbackGate>();
        {
            rpsui::PanelCallbackScope callback(reentrant);
            require(static_cast<bool>(callback) && !reentrant->close(), "closing inside a callback must report busy, not deadlock");
        }
        require(reentrant->close(), "self-closing callback failed to drain");
        std::cout << "Cooperation resource contracts, V1 ABI and callback lifetime passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
