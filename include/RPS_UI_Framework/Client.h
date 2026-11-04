#pragma once
#include "Discovery.h"
#include "Framework.h"

namespace rpsui::api
{
    // Consumer-side helper only; it is not part of the DLL ABI. Pass the
    // kQueryExportName export resolved from the loaded kModuleName, for example
    // reinterpret_cast<QueryInterfaceV1>(GetProcAddress(module, kQueryExportName)).
    // Requests exactly the versions and table size this header set declares, so
    // every refusal is a real version mismatch. Any thread.
    template <class Table>
    [[nodiscard]] Result acquire(QueryInterfaceV1 query, const Table*& output) noexcept
    {
        output = nullptr;
        if (!query) return Result::InvalidArgument;
        const InterfaceDescriptorV1* descriptor{};
        const auto result = query(Table::interfaceId, Table::majorVersion, Table::minorVersion, sizeof(Table), &descriptor);
        if (result != Result::Ok) return result;
        if (!descriptor || descriptor->size != sizeof(InterfaceDescriptorV1) ||
            descriptor->interfaceId != Table::interfaceId || descriptor->major != Table::majorVersion ||
            descriptor->minor < Table::minorVersion || descriptor->tableByteSize < sizeof(Table) ||
            descriptor->requiredFrameworkMajor != framework::kMajor ||
            descriptor->requiredFrameworkMinor > framework::kMinor || !descriptor->table) return Result::VersionMismatch;
        output = static_cast<const Table*>(descriptor->table);
        return Result::Ok;
    }
}
