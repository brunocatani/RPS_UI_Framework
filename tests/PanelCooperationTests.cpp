#include "PanelCooperation.h"
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

namespace
{
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

int main()
{
    try {
        using namespace rpsui::api;
        cooperation::PanelRegistration registration;
        cooperation::PanelAgreement agreement;
        require(rpsui::validatePanelContract(registration, agreement) == Result::Ok, "private panel contract rejected");
        for (const auto access : {cooperation::RenderAccess::SceneColorWrite, cooperation::RenderAccess::SceneDepthWrite, cooperation::RenderAccess::StereoSubmissionHook}) {
            registration.requestedAccess = cooperation::kPanelAccess | cooperation::accessMask(access);
            require(rpsui::validatePanelContract(registration, agreement) == Result::ResourceConflict,
                "host-owned resource must conflict");
            require(agreement.panelHandle == 0 && agreement.grantedAccess == 0 &&
                agreement.conflictingAccess == cooperation::accessMask(access) && std::strcmp(agreement.conflictOwnerId, "rps.ui.framework") == 0,
                "conflict must identify host and refuse grants");
        }
        registration.requestedAccess = cooperation::kPanelAccess;
        registration.stage = cooperation::RenderStage::StereoOutput;
        require(rpsui::validatePanelContract(registration, agreement) == Result::ResourceConflict, "stereo stage must conflict");
        registration.stage = cooperation::RenderStage::PanelTexture;
        registration.requestedAccess |= cooperation::accessMask(cooperation::RenderAccess::SceneDepthRead);
        require(rpsui::validatePanelContract(registration, agreement) == Result::UnsupportedStage, "private panel cannot borrow scene depth");
        registration.requestedAccess = 1ull << 63;
        require(rpsui::validatePanelContract(registration, agreement) == Result::InvalidArgument, "unknown access must fail closed");
        registration.requestedAccess = 0;
        require(rpsui::validatePanelContract(registration, agreement) == Result::InvalidArgument, "empty access must fail closed");
        registration.requestedAccess = cooperation::kPanelAccess;
        registration.version = 99;
        require(rpsui::validatePanelContract(registration, agreement) == Result::VersionMismatch, "wrong version accepted");
        registration.version = kRecordVersion;
        registration.size = 0;
        require(rpsui::validatePanelContract(registration, agreement) == Result::InvalidArgument, "undersized registration accepted");
        registration.size = sizeof(registration);
        registration.reserved = 1;
        require(rpsui::validatePanelContract(registration, agreement) == Result::InvalidArgument, "reserved field accepted");
        registration.reserved = 0;
        registration.stage = static_cast<cooperation::RenderStage>(99);
        require(rpsui::validatePanelContract(registration, agreement) == Result::UnsupportedStage, "unknown stage accepted");

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
        std::cout << "Cooperation resource contracts and callback lifetime passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
