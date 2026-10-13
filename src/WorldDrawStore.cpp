#include "WorldDrawStore.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace rpsui::world_draw
{
    namespace
    {
        using sdk::ResultV1;

        std::atomic<std::uint64_t> g_frame{ 0 };

        [[nodiscard]] bool finite(const float* values, std::size_t count) noexcept
        {
            for (std::size_t index = 0; index < count; ++index) {
                if (!std::isfinite(values[index])) return false;
            }
            return true;
        }

        [[nodiscard]] bool validFill(sdk::WorldFillV1 fill) noexcept
        {
            return fill == sdk::WorldFillV1::Solid || fill == sdk::WorldFillV1::Wire;
        }

        [[nodiscard]] bool valid(const sdk::WorldSphereV1& sphere) noexcept
        {
            return finite(sphere.center, 3) && finite(sphere.color, 4) &&
                std::isfinite(sphere.radius) && sphere.radius > 0.0f && validFill(sphere.fill);
        }

        [[nodiscard]] bool valid(const sdk::WorldLineV1& line) noexcept
        {
            return finite(line.start, 3) && finite(line.end, 3) && finite(line.color, 4);
        }

        [[nodiscard]] bool valid(const sdk::WorldTextV1& text) noexcept
        {
            return finite(text.anchor, 3) && finite(text.offsetPixels, 2) && finite(text.color, 4) &&
                std::isfinite(text.pixelSize) && text.pixelSize > 0.0f;
        }

        [[nodiscard]] bool valid(const sdk::WorldMeshInstanceV1& instance) noexcept
        {
            return finite(instance.transform, 16) && finite(instance.color, 4) && validFill(instance.fill);
        }

        template <class Record>
        [[nodiscard]] bool copyArray(std::vector<Record>& destination, const Record* source,
            std::uint32_t count, std::uint32_t capacity) noexcept
        {
            if (count > capacity || (count != 0 && !source)) return false;
            for (std::uint32_t index = 0; index < count; ++index) {
                if (!valid(source[index])) return false;
            }
            destination.assign(source, source + count);
            return true;
        }
    }

    void RenderBatch::clear() noexcept
    {
        spheres.clear();
        lines.clear();
        texts.clear();
        instances.clear();
        meshes.clear();
        meshIndex.clear();
    }

    bool RenderBatch::empty() const noexcept
    {
        return spheres.empty() && lines.empty() && texts.empty() && instances.empty();
    }

    Store::OwnerState* Store::findLocked(std::uint64_t owner) noexcept
    {
        const auto found = std::find_if(owners_.begin(), owners_.end(),
            [owner](const OwnerState& state) { return state.owner == owner; });
        return found == owners_.end() ? nullptr : &*found;
    }

    void Store::addOwner(std::uint64_t owner) noexcept
    {
        if (owner == 0) return;
        try {
            std::scoped_lock lock(mutex_);
            if (findLocked(owner)) return;
            owners_.emplace_back().owner = owner;
        } catch (...) {
            // The owner simply cannot draw; every call reports OwnerNotRegistered.
        }
    }

    void Store::removeOwner(std::uint64_t owner) noexcept
    {
        std::scoped_lock lock(mutex_);
        std::erase_if(owners_, [owner](const OwnerState& state) { return state.owner == owner; });
    }

    ResultV1 Store::submit(std::uint64_t owner, const sdk::WorldDrawPublicationV1& publication,
        std::uint64_t frame) noexcept
    {
        if (publication.structSize < sizeof(sdk::WorldDrawPublicationV1)) return ResultV1::InvalidArgument;
        if (publication.apiVersion != sdk::RPSUI_WORLD_DRAW_VERSION) return ResultV1::VersionMismatch;
        if (publication.leaseFrames == 0 || publication.leaseFrames > sdk::RPSUI_WORLD_DRAW_MAX_LEASE_FRAMES) {
            return ResultV1::InvalidArgument;
        }
        if (publication.sphereCount > sdk::RPSUI_WORLD_DRAW_MAX_SPHERES ||
            publication.lineCount > sdk::RPSUI_WORLD_DRAW_MAX_LINES ||
            publication.textCount > sdk::RPSUI_WORLD_DRAW_MAX_TEXTS ||
            publication.meshInstanceCount > sdk::RPSUI_WORLD_DRAW_MAX_MESH_INSTANCES) {
            return ResultV1::CapacityReached;
        }
        try {
            std::scoped_lock lock(mutex_);
            auto* state = findLocked(owner);
            if (!state) return ResultV1::OwnerNotRegistered;
            state->published = false;
            if (!copyArray(state->spheres, publication.spheres, publication.sphereCount,
                    sdk::RPSUI_WORLD_DRAW_MAX_SPHERES) ||
                !copyArray(state->lines, publication.lines, publication.lineCount,
                    sdk::RPSUI_WORLD_DRAW_MAX_LINES) ||
                !copyArray(state->texts, publication.texts, publication.textCount,
                    sdk::RPSUI_WORLD_DRAW_MAX_TEXTS) ||
                !copyArray(state->meshInstances, publication.meshInstances, publication.meshInstanceCount,
                    sdk::RPSUI_WORLD_DRAW_MAX_MESH_INSTANCES)) {
                state->spheres.clear();
                state->lines.clear();
                state->texts.clear();
                state->meshInstances.clear();
                return ResultV1::InvalidArgument;
            }
            for (auto& text : state->texts) text.text[sdk::RPSUI_WORLD_DRAW_TEXT_CAPACITY - 1] = '\0';
            state->expiresAfterFrame = frame + publication.leaseFrames;
            state->published = !state->spheres.empty() || !state->lines.empty() ||
                !state->texts.empty() || !state->meshInstances.empty();
            return ResultV1::Ok;
        } catch (...) {
            return ResultV1::InternalError;
        }
    }

    ResultV1 Store::clear(std::uint64_t owner) noexcept
    {
        std::scoped_lock lock(mutex_);
        auto* state = findLocked(owner);
        if (!state) return ResultV1::OwnerNotRegistered;
        state->published = false;
        state->spheres.clear();
        state->lines.clear();
        state->texts.clear();
        state->meshInstances.clear();
        return ResultV1::Ok;
    }

    ResultV1 Store::registerMesh(std::uint64_t owner, std::uint64_t meshId, const sdk::WorldMeshV1& mesh) noexcept
    {
        if (mesh.structSize < sizeof(sdk::WorldMeshV1)) return ResultV1::InvalidArgument;
        if (mesh.apiVersion != sdk::RPSUI_WORLD_DRAW_VERSION) return ResultV1::VersionMismatch;
        if (!mesh.positions || !mesh.indices || mesh.vertexCount == 0 ||
            mesh.vertexCount > sdk::RPSUI_WORLD_DRAW_MAX_MESH_VERTICES || mesh.indexCount == 0 ||
            mesh.indexCount > sdk::RPSUI_WORLD_DRAW_MAX_MESH_INDICES || (mesh.indexCount % 3) != 0 ||
            !finite(mesh.positions, static_cast<std::size_t>(mesh.vertexCount) * 3)) {
            return ResultV1::InvalidArgument;
        }
        for (std::uint32_t index = 0; index < mesh.indexCount; ++index) {
            if (mesh.indices[index] >= mesh.vertexCount) return ResultV1::InvalidArgument;
        }
        const std::uint64_t bytes = static_cast<std::uint64_t>(mesh.vertexCount) * 3 * sizeof(float) +
            static_cast<std::uint64_t>(mesh.indexCount) * sizeof(std::uint32_t);
        try {
            auto geometry = std::make_shared<MeshGeometry>();
            geometry->positions.assign(mesh.positions, mesh.positions + static_cast<std::size_t>(mesh.vertexCount) * 3);
            geometry->indices.assign(mesh.indices, mesh.indices + mesh.indexCount);

            std::scoped_lock lock(mutex_);
            auto* state = findLocked(owner);
            if (!state) return ResultV1::OwnerNotRegistered;
            std::uint64_t replacedBytes = 0;
            const auto existing = state->meshes.find(meshId);
            if (existing != state->meshes.end()) {
                replacedBytes = existing->second->positions.size() * sizeof(float) +
                    existing->second->indices.size() * sizeof(std::uint32_t);
            } else if (state->meshes.size() >= sdk::RPSUI_WORLD_DRAW_MAX_MESHES) {
                return ResultV1::CapacityReached;
            }
            if (state->meshBytes - replacedBytes + bytes > sdk::RPSUI_WORLD_DRAW_MAX_MESH_BYTES) {
                return ResultV1::CapacityReached;
            }
            geometry->revision = nextRevision_++;
            state->meshes[meshId] = std::move(geometry);
            state->meshBytes = state->meshBytes - replacedBytes + bytes;
            return ResultV1::Ok;
        } catch (...) {
            return ResultV1::InternalError;
        }
    }

    ResultV1 Store::releaseMesh(std::uint64_t owner, std::uint64_t meshId) noexcept
    {
        std::scoped_lock lock(mutex_);
        auto* state = findLocked(owner);
        if (!state) return ResultV1::OwnerNotRegistered;
        const auto found = state->meshes.find(meshId);
        if (found == state->meshes.end()) return ResultV1::InvalidArgument;
        state->meshBytes -= found->second->positions.size() * sizeof(float) +
            found->second->indices.size() * sizeof(std::uint32_t);
        state->meshes.erase(found);
        return ResultV1::Ok;
    }

    bool Store::snapshot(std::uint64_t frame, RenderBatch& out) const noexcept
    {
        out.clear();
        try {
            std::scoped_lock lock(mutex_);
            for (const auto& state : owners_) {
                if (!state.published || frame > state.expiresAfterFrame) continue;
                out.spheres.insert(out.spheres.end(), state.spheres.begin(), state.spheres.end());
                out.lines.insert(out.lines.end(), state.lines.begin(), state.lines.end());
                out.texts.insert(out.texts.end(), state.texts.begin(), state.texts.end());
                for (const auto& instance : state.meshInstances) {
                    const auto geometry = state.meshes.find(instance.meshId);
                    if (geometry == state.meshes.end()) continue;
                    const MeshKey key{ state.owner, instance.meshId };
                    auto [slot, inserted] = out.meshIndex.try_emplace(key, static_cast<std::uint32_t>(out.meshes.size()));
                    if (inserted) out.meshes.push_back({ key, geometry->second });
                    auto& draw = out.instances.emplace_back();
                    draw.mesh = slot->second;
                    std::copy_n(instance.transform, 16, draw.transform);
                    std::copy_n(instance.color, 4, draw.color);
                    draw.fill = instance.fill;
                }
            }
        } catch (...) {
            out.clear();
            return false;
        }
        return !out.empty();
    }

    Store& store() noexcept
    {
        static auto* value = new Store();
        return *value;
    }

    std::uint64_t currentFrame() noexcept
    {
        return g_frame.load(std::memory_order_acquire);
    }

    std::uint64_t advanceFrame() noexcept
    {
        return g_frame.fetch_add(1, std::memory_order_acq_rel) + 1;
    }
}
