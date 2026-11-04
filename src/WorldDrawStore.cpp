#include "WorldDrawStore.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace rpsui::world_draw
{
    namespace
    {
        using api::Result;

        std::atomic<std::uint64_t> g_frame{ 0 };

        [[nodiscard]] bool finite(const float* values, std::size_t count) noexcept
        {
            for (std::size_t index = 0; index < count; ++index) {
                if (!std::isfinite(values[index])) return false;
            }
            return true;
        }

        [[nodiscard]] bool validFill(api::world_draw::Fill fill) noexcept
        {
            return fill == api::world_draw::Fill::Solid || fill == api::world_draw::Fill::Wire;
        }

        [[nodiscard]] bool valid(const api::world_draw::Sphere& sphere) noexcept
        {
            return finite(sphere.center, 3) && finite(sphere.color, 4) &&
                std::isfinite(sphere.radius) && sphere.radius > 0.0f && validFill(sphere.fill);
        }

        [[nodiscard]] bool valid(const api::world_draw::Line& line) noexcept
        {
            return finite(line.start, 3) && finite(line.end, 3) && finite(line.color, 4);
        }

        [[nodiscard]] bool valid(const api::world_draw::Text& text) noexcept
        {
            return finite(text.anchor, 3) && finite(text.offsetPixels, 2) && finite(text.color, 4) &&
                std::isfinite(text.pixelSize) && text.pixelSize > 0.0f;
        }

        [[nodiscard]] bool valid(const api::world_draw::MeshInstance& instance) noexcept
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

    // Returns open owners only; a closing owner accepts no further calls.
    Store::OwnerState* Store::findLocked(std::uint64_t owner) noexcept
    {
        const auto found = std::find_if(owners_.begin(), owners_.end(),
            [owner](const OwnerState& state) { return state.owner == owner; });
        return found == owners_.end() || found->closing ? nullptr : &*found;
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

    bool Store::closeOwner(std::uint64_t owner) noexcept
    {
        std::scoped_lock lock(mutex_);
        const auto found = std::find_if(owners_.begin(), owners_.end(),
            [owner](const OwnerState& state) { return state.owner == owner; });
        if (found == owners_.end()) return true;
        found->closing = true;
        found->published = false;
        found->spheres.clear();
        found->lines.clear();
        found->texts.clear();
        found->meshInstances.clear();
        found->meshes.clear();
        found->meshBytes = 0;
        return !found->frameCallback.gate || found->frameCallback.gate->close();
    }

    bool Store::hasFrameCallback(std::uint64_t owner) const noexcept
    {
        std::scoped_lock lock(mutex_);
        return std::any_of(owners_.begin(), owners_.end(), [owner](const OwnerState& state) {
            return state.owner == owner && state.frameCallback.gate;
        });
    }

    Result Store::setFrameCallback(std::uint64_t owner, api::world_draw::FrameCallback callback,
        void* context) noexcept
    {
        try {
            auto gate = callback ? std::make_shared<PanelCallbackGate>() : nullptr;
            std::scoped_lock lock(mutex_);
            auto* state = findLocked(owner);
            if (!state) return Result::OwnerNotRegistered;
            const auto previous = std::move(state->frameCallback.gate);
            state->frameCallback = { callback, callback ? context : nullptr, std::move(gate) };
            return !previous || previous->close() ? Result::Ok : Result::CallbackBusy;
        } catch (...) {
            return Result::InternalError;
        }
    }

    void Store::runFrameCallbacks(std::uint64_t frame) noexcept
    {
        try {
            {
                std::scoped_lock lock(mutex_);
                pendingCallbacks_.clear();
                for (const auto& state : owners_) {
                    if (!state.closing && state.frameCallback.gate) pendingCallbacks_.push_back(state.frameCallback);
                }
            }
            for (const auto& entry : pendingCallbacks_) {
                PanelCallbackScope scope(entry.gate);
                if (scope) entry.callback(entry.context, frame);
            }
        } catch (...) {
            // Allocation failure only: this frame's callbacks are skipped.
        }
        pendingCallbacks_.clear();
    }

    void Store::removeOwner(std::uint64_t owner) noexcept
    {
        std::scoped_lock lock(mutex_);
        std::erase_if(owners_, [owner](const OwnerState& state) { return state.owner == owner; });
    }

    Result Store::submit(std::uint64_t owner, const api::world_draw::Publication& publication,
        std::uint64_t frame) noexcept
    {
        if (publication.size < sizeof(api::world_draw::Publication)) return Result::InvalidArgument;
        if (publication.version != api::kRecordVersion) return Result::VersionMismatch;
        if (publication.leaseFrames == 0 || publication.leaseFrames > api::world_draw::kMaxLeaseFrames) {
            return Result::InvalidArgument;
        }
        if (publication.sphereCount > api::world_draw::kMaxSpheres ||
            publication.lineCount > api::world_draw::kMaxLines ||
            publication.textCount > api::world_draw::kMaxTexts ||
            publication.meshInstanceCount > api::world_draw::kMaxMeshInstances) {
            return Result::CapacityReached;
        }
        try {
            std::scoped_lock lock(mutex_);
            auto* state = findLocked(owner);
            if (!state) return Result::OwnerNotRegistered;
            state->published = false;
            if (!copyArray(state->spheres, publication.spheres, publication.sphereCount,
                    api::world_draw::kMaxSpheres) ||
                !copyArray(state->lines, publication.lines, publication.lineCount,
                    api::world_draw::kMaxLines) ||
                !copyArray(state->texts, publication.texts, publication.textCount,
                    api::world_draw::kMaxTexts) ||
                !copyArray(state->meshInstances, publication.meshInstances, publication.meshInstanceCount,
                    api::world_draw::kMaxMeshInstances)) {
                state->spheres.clear();
                state->lines.clear();
                state->texts.clear();
                state->meshInstances.clear();
                return Result::InvalidArgument;
            }
            for (auto& text : state->texts) text.text[api::world_draw::kTextCapacity - 1] = '\0';
            state->expiresAfterFrame = frame + publication.leaseFrames;
            state->published = !state->spheres.empty() || !state->lines.empty() ||
                !state->texts.empty() || !state->meshInstances.empty();
            return Result::Ok;
        } catch (...) {
            return Result::InternalError;
        }
    }

    Result Store::clear(std::uint64_t owner) noexcept
    {
        std::scoped_lock lock(mutex_);
        auto* state = findLocked(owner);
        if (!state) return Result::OwnerNotRegistered;
        state->published = false;
        state->spheres.clear();
        state->lines.clear();
        state->texts.clear();
        state->meshInstances.clear();
        return Result::Ok;
    }

    Result Store::registerMesh(std::uint64_t owner, std::uint64_t meshId, const api::world_draw::Mesh& mesh) noexcept
    {
        if (mesh.size < sizeof(api::world_draw::Mesh)) return Result::InvalidArgument;
        if (mesh.version != api::kRecordVersion) return Result::VersionMismatch;
        if (!mesh.positions || !mesh.indices || mesh.vertexCount == 0 ||
            mesh.vertexCount > api::world_draw::kMaxMeshVertices || mesh.indexCount == 0 ||
            mesh.indexCount > api::world_draw::kMaxMeshIndices || (mesh.indexCount % 3) != 0 ||
            !finite(mesh.positions, static_cast<std::size_t>(mesh.vertexCount) * 3)) {
            return Result::InvalidArgument;
        }
        for (std::uint32_t index = 0; index < mesh.indexCount; ++index) {
            if (mesh.indices[index] >= mesh.vertexCount) return Result::InvalidArgument;
        }
        const std::uint64_t bytes = static_cast<std::uint64_t>(mesh.vertexCount) * 3 * sizeof(float) +
            static_cast<std::uint64_t>(mesh.indexCount) * sizeof(std::uint32_t);
        try {
            auto geometry = std::make_shared<MeshGeometry>();
            geometry->positions.assign(mesh.positions, mesh.positions + static_cast<std::size_t>(mesh.vertexCount) * 3);
            geometry->indices.assign(mesh.indices, mesh.indices + mesh.indexCount);

            std::scoped_lock lock(mutex_);
            auto* state = findLocked(owner);
            if (!state) return Result::OwnerNotRegistered;
            std::uint64_t replacedBytes = 0;
            const auto existing = state->meshes.find(meshId);
            if (existing != state->meshes.end()) {
                replacedBytes = existing->second->positions.size() * sizeof(float) +
                    existing->second->indices.size() * sizeof(std::uint32_t);
            } else if (state->meshes.size() >= api::world_draw::kMaxMeshes) {
                return Result::CapacityReached;
            }
            if (state->meshBytes - replacedBytes + bytes > api::world_draw::kMaxMeshBytes) {
                return Result::CapacityReached;
            }
            geometry->revision = nextRevision_++;
            state->meshes[meshId] = std::move(geometry);
            state->meshBytes = state->meshBytes - replacedBytes + bytes;
            return Result::Ok;
        } catch (...) {
            return Result::InternalError;
        }
    }

    Result Store::releaseMesh(std::uint64_t owner, std::uint64_t meshId) noexcept
    {
        std::scoped_lock lock(mutex_);
        auto* state = findLocked(owner);
        if (!state) return Result::OwnerNotRegistered;
        const auto found = state->meshes.find(meshId);
        if (found == state->meshes.end()) return Result::InvalidArgument;
        state->meshBytes -= found->second->positions.size() * sizeof(float) +
            found->second->indices.size() * sizeof(std::uint32_t);
        state->meshes.erase(found);
        return Result::Ok;
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
