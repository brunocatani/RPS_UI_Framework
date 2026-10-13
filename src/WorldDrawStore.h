#pragma once

#include "RPSUIWorldDrawApi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Owner-scoped world drawing state shared by the API (any thread) and the
// renderer (render thread). Publications and meshes are copied in; the renderer
// receives a bounded snapshot under the same mutex. No D3D types live here.
namespace rpsui::world_draw
{
    struct MeshGeometry
    {
        std::vector<float> positions;
        std::vector<std::uint32_t> indices;
        std::uint64_t revision{ 0 };
    };

    struct MeshKey
    {
        std::uint64_t owner{ 0 };
        std::uint64_t meshId{ 0 };
        bool operator==(const MeshKey&) const = default;
    };

    struct MeshKeyHash
    {
        [[nodiscard]] std::size_t operator()(const MeshKey& key) const noexcept
        {
            return std::hash<std::uint64_t>{}(key.owner * 0x9E3779B97F4A7C15ull ^ key.meshId);
        }
    };

    struct MeshRef
    {
        MeshKey key{};
        std::shared_ptr<const MeshGeometry> geometry;
    };

    struct DrawInstance
    {
        std::uint32_t mesh{ 0 }; // Index into RenderBatch::meshes.
        float transform[16]{};
        float color[4]{};
        sdk::WorldFillV1 fill{ sdk::WorldFillV1::Wire };
    };

    // Reused by the renderer; vectors keep their capacity between frames.
    struct RenderBatch
    {
        std::vector<sdk::WorldSphereV1> spheres;
        std::vector<sdk::WorldLineV1> lines;
        std::vector<sdk::WorldTextV1> texts;
        std::vector<DrawInstance> instances;
        std::vector<MeshRef> meshes;
        std::unordered_map<MeshKey, std::uint32_t, MeshKeyHash> meshIndex;

        void clear() noexcept;
        [[nodiscard]] bool empty() const noexcept;
    };

    class Store final
    {
    public:
        // Owner lifetime follows RPS UI consumer registration.
        void addOwner(std::uint64_t owner) noexcept;
        void removeOwner(std::uint64_t owner) noexcept;

        // frame is the renderer frame counter when the call is made.
        [[nodiscard]] sdk::ResultV1 submit(std::uint64_t owner,
            const sdk::WorldDrawPublicationV1& publication, std::uint64_t frame) noexcept;
        [[nodiscard]] sdk::ResultV1 clear(std::uint64_t owner) noexcept;
        [[nodiscard]] sdk::ResultV1 registerMesh(std::uint64_t owner, std::uint64_t meshId,
            const sdk::WorldMeshV1& mesh) noexcept;
        [[nodiscard]] sdk::ResultV1 releaseMesh(std::uint64_t owner, std::uint64_t meshId) noexcept;

        // Fills out with every publication still leased at frame. Returns false
        // when there is nothing to draw or the copy failed.
        bool snapshot(std::uint64_t frame, RenderBatch& out) const noexcept;

    private:
        struct OwnerState
        {
            std::uint64_t owner{ 0 };
            std::uint64_t expiresAfterFrame{ 0 };
            bool published{ false };
            std::vector<sdk::WorldSphereV1> spheres;
            std::vector<sdk::WorldLineV1> lines;
            std::vector<sdk::WorldTextV1> texts;
            std::vector<sdk::WorldMeshInstanceV1> meshInstances;
            std::unordered_map<std::uint64_t, std::shared_ptr<const MeshGeometry>> meshes;
            std::uint64_t meshBytes{ 0 };
        };

        [[nodiscard]] OwnerState* findLocked(std::uint64_t owner) noexcept;

        mutable std::mutex mutex_;
        std::vector<OwnerState> owners_;
        std::uint64_t nextRevision_{ 1 };
    };

    [[nodiscard]] Store& store() noexcept;

    // Renderer frame counter. Leases are counted in rendered frames.
    [[nodiscard]] std::uint64_t currentFrame() noexcept;
    std::uint64_t advanceFrame() noexcept;
}
