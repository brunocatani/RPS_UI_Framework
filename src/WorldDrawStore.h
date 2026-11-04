#pragma once

#include "PanelCooperation.h"
#include <RPS_UI_Framework/WorldDraw.h>

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
        api::world_draw::Fill fill{ api::world_draw::Fill::Wire };
    };

    // Reused by the renderer; vectors keep their capacity between frames.
    struct RenderBatch
    {
        std::vector<api::world_draw::Sphere> spheres;
        std::vector<api::world_draw::Line> lines;
        std::vector<api::world_draw::Text> texts;
        std::vector<DrawInstance> instances;
        std::vector<MeshRef> meshes;
        std::unordered_map<MeshKey, std::uint32_t, MeshKeyHash> meshIndex;

        void clear() noexcept;
        [[nodiscard]] bool empty() const noexcept;
    };

    class Store final
    {
    public:
        // Owner lifetime follows RPS UI consumer registration. closeOwner stops
        // all drawing and callbacks and returns whether no callback is still
        // running; removeOwner then forgets the owner.
        void addOwner(std::uint64_t owner) noexcept;
        [[nodiscard]] bool closeOwner(std::uint64_t owner) noexcept;
        void removeOwner(std::uint64_t owner) noexcept;
        [[nodiscard]] bool hasFrameCallback(std::uint64_t owner) const noexcept;

        [[nodiscard]] api::Result setFrameCallback(std::uint64_t owner,
            api::world_draw::FrameCallback callback, void* context) noexcept;
        // Render thread, before the frame counter advances to frame. Calls every
        // open owner's callback without holding the store lock, so callbacks may
        // submit.
        void runFrameCallbacks(std::uint64_t frame) noexcept;

        // frame is the renderer frame counter when the call is made.
        [[nodiscard]] api::Result submit(std::uint64_t owner,
            const api::world_draw::Publication& publication, std::uint64_t frame) noexcept;
        [[nodiscard]] api::Result clear(std::uint64_t owner) noexcept;
        [[nodiscard]] api::Result registerMesh(std::uint64_t owner, std::uint64_t meshId,
            const api::world_draw::Mesh& mesh) noexcept;
        [[nodiscard]] api::Result releaseMesh(std::uint64_t owner, std::uint64_t meshId) noexcept;

        // Fills out with every publication still leased at frame. Returns false
        // when there is nothing to draw or the copy failed.
        bool snapshot(std::uint64_t frame, RenderBatch& out) const noexcept;

    private:
        struct FrameCallback
        {
            api::world_draw::FrameCallback callback{ nullptr };
            void* context{ nullptr };
            std::shared_ptr<PanelCallbackGate> gate;
        };

        struct OwnerState
        {
            std::uint64_t owner{ 0 };
            bool closing{ false };
            FrameCallback frameCallback;
            std::uint64_t expiresAfterFrame{ 0 };
            bool published{ false };
            std::vector<api::world_draw::Sphere> spheres;
            std::vector<api::world_draw::Line> lines;
            std::vector<api::world_draw::Text> texts;
            std::vector<api::world_draw::MeshInstance> meshInstances;
            std::unordered_map<std::uint64_t, std::shared_ptr<const MeshGeometry>> meshes;
            std::uint64_t meshBytes{ 0 };
        };

        [[nodiscard]] OwnerState* findLocked(std::uint64_t owner) noexcept;

        mutable std::mutex mutex_;
        std::vector<OwnerState> owners_;
        std::vector<FrameCallback> pendingCallbacks_; // Render thread only.
        std::uint64_t nextRevision_{ 1 };
    };

    [[nodiscard]] Store& store() noexcept;

    // Renderer frame counter. Leases are counted in rendered frames.
    [[nodiscard]] std::uint64_t currentFrame() noexcept;
    std::uint64_t advanceFrame() noexcept;
}
