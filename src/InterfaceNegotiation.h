#pragma once
#include <RPS_UI_Framework/Discovery.h>
#include <RPS_UI_Framework/Framework.h>
#include <span>

namespace rpsui::discovery
{
    // Every interface takes owner tokens from the Framework major RPS UI serves.
    template <class Table>
    constexpr api::InterfaceDescriptorV1 describe(const Table& table) noexcept
    {
        return { sizeof(api::InterfaceDescriptorV1), Table::interfaceId, Table::majorVersion, Table::minorVersion,
            sizeof(Table), api::framework::kMajor, api::framework::kMinor, 0, &table };
    }

    // Exact major, minimum minor and minimum table size. A known interface
    // asked for a major RPS UI does not serve is refused with UnsupportedMajor
    // and no table; the caller logs that refusal.
    inline api::Result query(std::span<const api::InterfaceDescriptorV1> entries,
        api::InterfaceId id, std::uint32_t major, std::uint32_t minor,
        std::uint32_t bytes, const api::InterfaceDescriptorV1** output) noexcept
    {
        if (!output) return api::Result::InvalidArgument;
        *output = nullptr;
        if (!major || !bytes) return api::Result::InvalidArgument;
        bool knownId = false;
        for (const auto& descriptor : entries) {
            if (descriptor.interfaceId != id) continue;
            knownId = true;
            if (descriptor.major != major) continue;
            if (descriptor.minor < minor) return api::Result::UnsupportedMinor;
            if (descriptor.tableByteSize < bytes) return api::Result::TableTooSmall;
            if (!descriptor.table || descriptor.size != sizeof(api::InterfaceDescriptorV1)) return api::Result::InternalError;
            *output = &descriptor;
            return api::Result::Ok;
        }
        return knownId ? api::Result::UnsupportedMajor : api::Result::UnknownInterface;
    }
}
