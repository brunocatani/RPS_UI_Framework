#include "WorldDrawStore.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
    using namespace rpsui::api;
    using rpsui::world_draw::RenderBatch;
    using rpsui::world_draw::Store;

    world_draw::Publication publication(const std::vector<world_draw::Sphere>& spheres, std::uint32_t lease = 2)
    {
        world_draw::Publication result{};
        result.leaseFrames = lease;
        result.sphereCount = static_cast<std::uint32_t>(spheres.size());
        result.spheres = spheres.data();
        return result;
    }

    world_draw::Sphere sphere(float x)
    {
        world_draw::Sphere result{};
        result.center[0] = x;
        result.radius = 2.0f;
        return result;
    }

    const float kTriangle[9]{ 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const std::uint32_t kTriangleIndices[3]{ 0, 1, 2 };

    world_draw::Mesh triangle()
    {
        world_draw::Mesh mesh{};
        mesh.positions = kTriangle;
        mesh.vertexCount = 3;
        mesh.indices = kTriangleIndices;
        mesh.indexCount = 3;
        return mesh;
    }
}

int main()
{
    // Published ABI of the world-draw records and table.
    static_assert(offsetof(world_draw::Publication, spheres) == 32);
    static_assert(offsetof(world_draw::Publication, meshInstances) == 56);
    static_assert(offsetof(world_draw::MeshInstance, transform) == 8);
    static_assert(offsetof(world_draw::MeshInstance, fill) == 88);
    static_assert(offsetof(world_draw::Text, anchor) == 128);
    static_assert(offsetof(world_draw::Api, submit) == 0);
    static_assert(offsetof(world_draw::Api, releaseMesh) == 24);
    static_assert(offsetof(world_draw::Api, setFrameCallback) == 32);

    Store store;
    RenderBatch batch;
    const std::vector one{ sphere(1.0f) };

    // Only registered owners draw, and removal drops everything they published.
    assert(store.submit(7, publication(one), 0) == Result::OwnerNotRegistered);
    store.addOwner(7);
    store.addOwner(9);
    assert(store.submit(7, publication(one), 10) == Result::Ok);
    assert(store.snapshot(11, batch) && batch.spheres.size() == 1);
    store.removeOwner(7);
    assert(!store.snapshot(11, batch) && batch.empty());
    assert(store.submit(7, publication(one), 10) == Result::OwnerNotRegistered);

    // Owners are isolated: one owner's clear leaves the other visible.
    store.addOwner(7);
    const std::vector two{ sphere(1.0f), sphere(2.0f) };
    assert(store.submit(7, publication(one), 10) == Result::Ok);
    assert(store.submit(9, publication(two), 10) == Result::Ok);
    assert(store.snapshot(11, batch) && batch.spheres.size() == 3);
    assert(store.clear(7) == Result::Ok);
    assert(store.snapshot(11, batch) && batch.spheres.size() == 2);

    // A lease counts rendered frames from the frame current at submission.
    assert(store.submit(9, publication(two, 3), 20) == Result::Ok);
    assert(store.snapshot(23, batch));
    assert(!store.snapshot(24, batch));
    assert(store.submit(9, publication(two, 0), 20) == Result::InvalidArgument);
    assert(store.submit(9, publication(two, world_draw::kMaxLeaseFrames + 1), 20) ==
        Result::InvalidArgument);

    // An empty publication clears; a rejected one leaves nothing half-applied.
    assert(store.submit(9, publication(two), 30) == Result::Ok);
    assert(store.submit(9, publication({}), 30) == Result::Ok);
    assert(!store.snapshot(31, batch));
    std::vector bad{ sphere(1.0f), sphere(std::numeric_limits<float>::quiet_NaN()) };
    assert(store.submit(9, publication(two), 30) == Result::Ok);
    assert(store.submit(9, publication(bad), 30) == Result::InvalidArgument);
    assert(!store.snapshot(31, batch));
    std::vector<world_draw::Sphere> tooMany(world_draw::kMaxSpheres + 1, sphere(1.0f));
    assert(store.submit(9, publication(tooMany), 30) == Result::CapacityReached);
    world_draw::Publication wrongVersion = publication(one);
    wrongVersion.version = kRecordVersion + 1;
    assert(store.submit(9, wrongVersion, 30) == Result::VersionMismatch);

    // Text is always terminated inside its capacity.
    world_draw::Text text{};
    for (auto& character : text.text) character = 'A';
    world_draw::Publication withText{};
    withText.textCount = 1;
    withText.texts = &text;
    assert(store.submit(9, withText, 40) == Result::Ok);
    assert(store.snapshot(41, batch) && batch.texts.size() == 1 &&
        batch.texts[0].text[world_draw::kTextCapacity - 1] == '\0');

    // Meshes: validated, owner-scoped, replaced by id, and skipped once released.
    auto mesh = triangle();
    assert(store.registerMesh(9, 5, mesh) == Result::Ok);
    const std::uint32_t outOfRange[3]{ 0, 1, 3 };
    auto badMesh = triangle();
    badMesh.indices = outOfRange;
    assert(store.registerMesh(9, 6, badMesh) == Result::InvalidArgument);
    badMesh = triangle();
    badMesh.indexCount = 2;
    assert(store.registerMesh(9, 6, badMesh) == Result::InvalidArgument);
    assert(store.registerMesh(4, 5, mesh) == Result::OwnerNotRegistered);

    world_draw::MeshInstance instances[3]{};
    instances[0].meshId = 5;
    instances[1].meshId = 5;
    instances[2].meshId = 77; // Unknown: skipped, not an error.
    world_draw::Publication withMeshes{};
    withMeshes.meshInstanceCount = 3;
    withMeshes.meshInstances = instances;
    assert(store.submit(9, withMeshes, 50) == Result::Ok);
    assert(store.snapshot(51, batch) && batch.instances.size() == 2 && batch.meshes.size() == 1);
    const auto firstRevision = batch.meshes[0].geometry->revision;
    assert(store.registerMesh(9, 5, mesh) == Result::Ok);
    assert(store.snapshot(51, batch) && batch.meshes[0].geometry->revision != firstRevision);
    // Owner 7 cannot use owner 9's mesh id.
    assert(store.submit(7, withMeshes, 50) == Result::Ok);
    assert(store.snapshot(51, batch) && batch.instances.size() == 2);
    assert(store.releaseMesh(9, 5) == Result::Ok);
    assert(store.releaseMesh(9, 5) == Result::InvalidArgument);
    assert(!store.snapshot(51, batch));

    // Per-owner mesh count limit.
    for (std::uint64_t id = 0; id < world_draw::kMaxMeshes; ++id) {
        assert(store.registerMesh(9, 1000 + id, mesh) == Result::Ok);
    }
    assert(store.registerMesh(9, 99999, mesh) == Result::CapacityReached);
    assert(store.registerMesh(9, 1000, mesh) == Result::Ok); // Replacing stays allowed.

    // Frame callbacks run before the snapshot and may submit for that frame only.
    struct CallbackProbe
    {
        Store* store{ nullptr };
        std::uint64_t owner{ 0 };
        std::uint64_t lastFrame{ 0 };
        int calls{ 0 };
        std::vector<world_draw::Sphere> spheres{ sphere(3.0f) };
    } probe{ &store, 11 };
    const world_draw::FrameCallback callback = [](void* context, std::uint64_t frame) noexcept {
        auto& self = *static_cast<CallbackProbe*>(context);
        ++self.calls;
        self.lastFrame = frame;
        // The API stamps submissions with the last drawn frame, frame - 1.
        (void)self.store->submit(self.owner, publication(self.spheres, 1), frame - 1);
    };
    assert(store.setFrameCallback(11, callback, &probe) == Result::OwnerNotRegistered);
    store.addOwner(11);
    assert(store.setFrameCallback(11, callback, &probe) == Result::Ok);
    assert(store.hasFrameCallback(11));
    store.runFrameCallbacks(60);
    assert(probe.calls == 1 && probe.lastFrame == 60);
    assert(store.snapshot(60, batch) && batch.spheres.size() == 1 && batch.spheres[0].center[0] == 3.0f);
    assert(!store.snapshot(61, batch)); // A one-frame lease ends after that frame.
    assert(store.setFrameCallback(11, nullptr, nullptr) == Result::Ok);
    assert(!store.hasFrameCallback(11));
    store.runFrameCallbacks(61);
    assert(probe.calls == 1);

    // Closing an owner stops its callback and drawing; removal forgets it.
    assert(store.setFrameCallback(11, callback, &probe) == Result::Ok);
    assert(store.closeOwner(11));
    store.runFrameCallbacks(70);
    assert(probe.calls == 1 && !store.snapshot(70, batch));
    assert(store.submit(11, publication(one), 70) == Result::OwnerNotRegistered);
    store.removeOwner(11);
    assert(!store.hasFrameCallback(11));
    return 0;
}
