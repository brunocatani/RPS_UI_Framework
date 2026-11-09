#pragma once

// A consumer built for another RPS UI API version is never left running with
// its UI silently off: discovery reports it and the game stops at the main menu
// with an error box naming the DLL to update (RPS/Runtime/RequiredUpdate.h).

#include <RPS_UI_Framework/Abi.h>

#include <cstdint>

namespace rpsui::required_updates
{
    // Any thread. Called with every discovery result; acts only on version
    // refusals. callerAddress is the discovery call's return address, which
    // lies inside the consumer's DLL.
    void noteDiscoveryRefusal(api::Result result, api::InterfaceId id, std::uint32_t requestedMajor,
        const void* callerAddress) noexcept;

    // Any thread. A call to one of the pre-2.0 exports, which only refuse.
    void noteRetiredExport(const char* exportName, const void* callerAddress) noexcept;

    // Any thread. A panel registration whose Dear ImGui build differs from the
    // host's (framework::ImGuiBuild): its panel would draw into a context its
    // code cannot read safely, so it is refused like an outdated consumer.
    void noteImGuiBuildMismatch(const char* panelId, const void* callerAddress) noexcept;

    // Once, from the F4SE GameDataReady message.
    void watch() noexcept;
}
