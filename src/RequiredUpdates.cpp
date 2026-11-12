#include "PCH.h"

#include "RequiredUpdates.h"

#include "Logger.h"

#include <RPS/Runtime/CallerModule.h>
#include <RPS_UI_Framework/Framework.h>

namespace rpsui::required_updates
{
    namespace
    {
        namespace caller_module = RPS::Runtime::CallerModule;
    }

    void noteDiscoveryRefusal(const api::Result result, const api::InterfaceId id, const std::uint32_t requestedMajor,
        const void* callerAddress) noexcept
    {
        using api::Result;
        bool consumerOlder = false;
        switch (result) {
        case Result::UnsupportedMajor:
        case Result::UnknownInterface:
            consumerOlder = requestedMajor < api::framework::kMajor;
            break;
        case Result::UnsupportedMinor:
        case Result::TableTooSmall:
            break;
        default:
            return; // not a version refusal
        }
        if (!caller_module::firstFromCaller(callerAddress))
            return;
        try {
            const auto dll = caller_module::moduleNameForLog(callerAddress);
            if (consumerOlder) {
                log::error("{} asked for interface {} major {}: it was built for an older RPS UI Framework and gets no "
                           "RPS UI features until it is updated.",
                    dll, static_cast<std::uint32_t>(id), requestedMajor);
            } else {
                log::error("{} asked for interface {} major {} (result {}): it needs a newer RPS UI Framework and gets "
                           "no RPS UI features until RPS UI Framework is updated.",
                    dll, static_cast<std::uint32_t>(id), requestedMajor, static_cast<std::uint32_t>(result));
            }
        } catch (...) {}
    }

    void noteRetiredExport(const char* exportName, const void* callerAddress) noexcept
    {
        if (!caller_module::firstFromCaller(callerAddress))
            return;
        try {
            log::error("{} called {}, RPS UI Framework's retired pre-2.0 API: it was built for an older RPS UI "
                       "Framework and gets no RPS UI features until it is updated.",
                caller_module::moduleNameForLog(callerAddress), exportName ? exportName : "a retired export");
        } catch (...) {}
    }

    void noteImGuiBuildMismatch(const char* panelId, const void* callerAddress) noexcept
    {
        if (!caller_module::firstFromCaller(callerAddress))
            return;
        try {
            log::error("{} registered panel '{}' with a Dear ImGui build that differs from RPS UI Framework's: it was "
                       "built for another RPS UI Framework, and the panel stays unregistered until it is updated.",
                caller_module::moduleNameForLog(callerAddress), panelId ? panelId : "");
        } catch (...) {}
    }
}
