#include "PCH.h"

#include "RequiredUpdates.h"

#include "Logger.h"

#include <RPS/Runtime/RequiredUpdate.h>
#include <RPS_UI_Framework/Framework.h>

#include <string>

namespace rpsui::required_updates
{
    namespace
    {
        namespace required_update = RPS::Runtime::RequiredUpdate;

        // DLL file names for the log; characters outside ASCII become '?'.
        std::string narrowForLog(const std::wstring& text)
        {
            std::string result;
            result.reserve(text.size());
            for (const wchar_t c : text)
                result.push_back(c > 0 && c < 0x80 ? static_cast<char>(c) : '?');
            return result;
        }

        struct Caller
        {
            std::wstring dll; // for the error box
            std::string dllForLog;
        };

        Caller identify(const void* callerAddress)
        {
            Caller caller{ required_update::moduleFileNameAt(callerAddress), {} };
            caller.dllForLog = caller.dll.empty() ? std::string{ "an unidentified module" } : narrowForLog(caller.dll);
            if (caller.dll.empty())
                caller.dll = L"A mod (see RPS_UI_Framework.log)";
            return caller;
        }

        void reportConsumerOlder(const Caller& caller)
        {
            required_update::report(caller.dll +
                L" was built for an older RPS UI Framework and does not work with the installed one. Update it or remove it.");
        }

        void flushLog() noexcept
        {
            try {
                if (log::instance)
                    log::instance->flush();
            } catch (...) {}
        }
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
        if (!required_update::firstFromCaller(callerAddress))
            return;
        try {
            const auto caller = identify(callerAddress);
            if (consumerOlder) {
                log::critical("{} asked for interface {} major {}: it was built for an older RPS UI Framework. The game "
                              "stops at the main menu until it is updated or removed.",
                    caller.dllForLog, static_cast<std::uint32_t>(id), requestedMajor);
                reportConsumerOlder(caller);
            } else {
                log::critical("{} asked for interface {} major {} (result {}): it needs a newer RPS UI Framework. The "
                              "game stops at the main menu until RPS UI Framework is updated.",
                    caller.dllForLog, static_cast<std::uint32_t>(id), requestedMajor, static_cast<std::uint32_t>(result));
                required_update::report(caller.dll + L" needs a newer RPS UI Framework than the installed one. Update RPS UI Framework.");
            }
        } catch (...) {}
    }

    void noteRetiredExport(const char* exportName, const void* callerAddress) noexcept
    {
        if (!required_update::firstFromCaller(callerAddress))
            return;
        try {
            const auto caller = identify(callerAddress);
            log::critical("{} called {}, RPS UI Framework's retired pre-2.0 API: it was built for an older RPS UI "
                          "Framework. The game stops at the main menu until it is updated or removed.",
                caller.dllForLog, exportName ? exportName : "a retired export");
            reportConsumerOlder(caller);
        } catch (...) {}
    }

    void watch() noexcept
    {
        if (!required_update::watchMainMenu(L"RPS UI Framework: update required", &flushLog)) {
            try {
                log::error("The UI is unavailable; version mismatches cannot stop the game at the main menu");
            } catch (...) {}
        }
    }
}
