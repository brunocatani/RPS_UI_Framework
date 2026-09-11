#pragma once
#include "RPSUICooperationApi.h"
#include <cstring>
#include <memory>
#include <mutex>

namespace rpsui
{
    inline sdk::ResultV1 validatePanelContract(const sdk::CooperativePanelRegistrationV1& registration,
        sdk::PanelAgreementV1& agreement) noexcept
    {
        agreement = {};
        if (registration.structSize < sizeof(registration)) return sdk::ResultV1::InvalidArgument;
        if (registration.apiVersion != sdk::RPSUI_COOPERATION_VERSION) return sdk::ResultV1::VersionMismatch;
        constexpr auto known = sdk::RPSUI_PANEL_ACCESS | sdk::RPSUI_HOST_EXCLUSIVE_ACCESS |
            sdk::accessMask(sdk::RenderAccessV1::SceneDepthRead);
        if ((registration.requestedAccess & ~known) || registration.reserved32) return sdk::ResultV1::InvalidArgument;
        const auto conflict = registration.requestedAccess & sdk::RPSUI_HOST_EXCLUSIVE_ACCESS;
        if (conflict || registration.stage == sdk::RenderStageV1::StereoOutput) {
            agreement.conflictingAccess = conflict ? conflict : sdk::accessMask(sdk::RenderAccessV1::SceneColorWrite);
            std::memcpy(agreement.conflictOwnerId, "rps.ui.framework", sizeof("rps.ui.framework"));
            return sdk::ResultV1::ResourceConflict;
        }
        if (registration.stage != sdk::RenderStageV1::PanelTexture ||
            (registration.requestedAccess & sdk::accessMask(sdk::RenderAccessV1::SceneDepthRead)))
            return sdk::ResultV1::UnsupportedStage;
        return registration.requestedAccess == sdk::RPSUI_PANEL_ACCESS ? sdk::ResultV1::Ok : sdk::ResultV1::InvalidArgument;
    }

    // Registry/control and render threads share this lifetime gate. Its lock is
    // never held over consumer code, D3D work, or acquisition of the registry lock.
    class PanelCallbackGate final
    {
    public:
        bool enter() noexcept
        {
            std::lock_guard lock(mutex_);
            if (closing_) return false;
            ++inFlight_;
            return true;
        }
        void leave() noexcept { std::lock_guard lock(mutex_); --inFlight_; }
        bool close() noexcept
        {
            std::lock_guard lock(mutex_);
            closing_ = true;
            return inFlight_ == 0;
        }
    private:
        std::mutex mutex_;
        bool closing_{};
        std::uint32_t inFlight_{};
    };

    class PanelCallbackScope final
    {
    public:
        explicit PanelCallbackScope(std::shared_ptr<PanelCallbackGate> gate) noexcept : gate_(std::move(gate))
        { entered_ = gate_ && gate_->enter(); }
        ~PanelCallbackScope() { if (entered_) gate_->leave(); }
        explicit operator bool() const noexcept { return entered_; }
        PanelCallbackScope(const PanelCallbackScope&) = delete;
        PanelCallbackScope& operator=(const PanelCallbackScope&) = delete;
    private:
        std::shared_ptr<PanelCallbackGate> gate_;
        bool entered_{};
    };
}
