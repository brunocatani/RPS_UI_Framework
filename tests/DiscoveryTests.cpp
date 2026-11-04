#include "InterfaceNegotiation.h"
#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/WorldDraw.h>
#include <array>
#include <cassert>
#include <iostream>

// Discovery negotiation over RPS UI's real interface tables: exact major,
// minimum minor and minimum table size; every pre-2.0 major is refused.
namespace {
    using namespace rpsui::api;
    const framework::Api g_framework{};
    const world_draw::Api g_draw{};
    const std::array g_entries{ rpsui::discovery::describe(g_framework), rpsui::discovery::describe(g_draw) };

    Result RPSUI_CALL queryEntries(InterfaceId id, std::uint32_t major, std::uint32_t minor, std::uint32_t bytes,
        const InterfaceDescriptorV1** output) noexcept
    {
        return rpsui::discovery::query(g_entries, id, major, minor, bytes, output);
    }
    Result RPSUI_CALL wrongTable(InterfaceId, std::uint32_t, std::uint32_t, std::uint32_t,
        const InterfaceDescriptorV1** output) noexcept
    {
        *output = &g_entries[1];
        return Result::Ok;
    }
}

int main() {
    const auto query = [](InterfaceId id, std::uint32_t major, std::uint32_t minor, std::uint32_t bytes,
                           const InterfaceDescriptorV1** output) { return queryEntries(id, major, minor, bytes, output); };
    const InterfaceDescriptorV1* out{};

    assert(query(InterfaceId::Framework, 2, 0, sizeof(framework::Api), &out) == Result::Ok);
    assert(out == &g_entries[0] && out->table == &g_framework && out->tableByteSize == sizeof(framework::Api));
    assert(query(InterfaceId::WorldDraw, 2, 0, sizeof(world_draw::Api), &out) == Result::Ok);
    assert(out->requiredFrameworkMajor == framework::kMajor && out->requiredFrameworkMinor == framework::kMinor);

    // Only major 2 is served.
    for (const std::uint32_t major : { 1u, 3u }) {
        out = &g_entries[0];
        assert(query(InterfaceId::Framework, major, 0, 8, &out) == Result::UnsupportedMajor && !out);
    }

    // An older (smaller) table request is served; a newer minor or larger table is not.
    assert(query(InterfaceId::Framework, 2, 0, 8, &out) == Result::Ok && out);
    assert(query(InterfaceId::Framework, 2, 1, 8, &out) == Result::UnsupportedMinor && !out);
    assert(query(InterfaceId::Framework, 2, 0, sizeof(framework::Api) + 8, &out) == Result::TableTooSmall && !out);

    assert(query(static_cast<InterfaceId>(99), 2, 0, 8, &out) == Result::UnknownInterface && !out);
    assert(query(InterfaceId::Framework, 0, 0, 8, &out) == Result::InvalidArgument);
    assert(query(InterfaceId::Framework, 2, 0, 0, &out) == Result::InvalidArgument);
    assert(query(InterfaceId::Framework, 2, 0, 8, nullptr) == Result::InvalidArgument);

    // The consumer helper binds the matching table and rejects a wrong descriptor.
    const world_draw::Api* draw{};
    assert(acquire(&queryEntries, draw) == Result::Ok && draw == &g_draw);
    const framework::Api* wrong{};
    assert(acquire(&wrongTable, wrong) == Result::VersionMismatch && !wrong);
    assert(acquire(QueryInterfaceV1{}, wrong) == Result::InvalidArgument && !wrong);
    std::cout << "PASS RPS UI discovery: exact major, minimum minor, table size, pre-2.0 refusal\n";
}
