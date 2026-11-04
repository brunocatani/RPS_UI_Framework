#pragma once
#include <RPS_UI_Framework/Cooperation.h>
#include <cstring>
#include <memory>
#include <mutex>

namespace rpsui
{
    inline api::Result validatePanelContract(const api::cooperation::PanelRegistration& registration,
        api::cooperation::PanelAgreement& agreement) noexcept
    {
        agreement = {};
        if (registration.size < sizeof(registration)) return api::Result::InvalidArgument;
        if (registration.version != api::kRecordVersion) return api::Result::VersionMismatch;
        constexpr auto known = api::cooperation::kPanelAccess | api::cooperation::kHostExclusiveAccess |
            api::cooperation::accessMask(api::cooperation::RenderAccess::SceneDepthRead);
        if ((registration.requestedAccess & ~known) || registration.reserved) return api::Result::InvalidArgument;
        const auto conflict = registration.requestedAccess & api::cooperation::kHostExclusiveAccess;
        if (conflict || registration.stage == api::cooperation::RenderStage::StereoOutput) {
            agreement.conflictingAccess = conflict ? conflict : api::cooperation::accessMask(api::cooperation::RenderAccess::SceneColorWrite);
            std::memcpy(agreement.conflictOwnerId, "rps.ui.framework", sizeof("rps.ui.framework"));
            return api::Result::ResourceConflict;
        }
        if (registration.stage != api::cooperation::RenderStage::PanelTexture ||
            (registration.requestedAccess & api::cooperation::accessMask(api::cooperation::RenderAccess::SceneDepthRead)))
            return api::Result::UnsupportedStage;
        return registration.requestedAccess == api::cooperation::kPanelAccess ? api::Result::Ok : api::Result::InvalidArgument;
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
