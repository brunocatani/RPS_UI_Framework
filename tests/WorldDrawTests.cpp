#include "WorldDrawStore.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
    using namespace rpsui::sdk;
    using rpsui::world_draw::RenderBatch;
    using rpsui::world_draw::Store;

    WorldDrawPublicationV1 publication(const std::vector<WorldSphereV1>& spheres, std::uint32_t lease = 2)
    {
        WorldDrawPublicationV1 result{};
        result.leaseFrames = lease;
        result.sphereCount = static_cast<std::uint32_t>(spheres.size());
        result.spheres = spheres.data();
        return result;
    }

    WorldSphereV1 sphere(float x)
    {
        WorldSphereV1 result{};
        result.center[0] = x;
        result.radius = 2.0f;
        return result;
    }

    const float kTriangle[9]{ 0, 0, 0, 1, 0, 0, 0, 1, 0 };
    const std::uint32_t kTriangleIndices[3]{ 0, 1, 2 };

    WorldMeshV1 triangle()
    {
        WorldMeshV1 mesh{};
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
    static_assert(offsetof(WorldDrawPublicationV1, spheres) == 32);
    static_assert(offsetof(WorldDrawPublicationV1, meshInstances) == 56);
    static_assert(offsetof(WorldMeshInstanceV1, transform) == 8);
    static_assert(offsetof(WorldMeshInstanceV1, fill) == 88);
    static_assert(offsetof(WorldTextV1, anchor) == 128);
    static_assert(offsetof(WorldDrawApiV1, submit) == 16);
    static_assert(offsetof(WorldDrawApiV1, releaseMesh) == 40);

    Store store;
    RenderBatch batch;
    const std::vector one{ sphere(1.0f) };

    // Only registered owners draw, and removal drops everything they published.
    assert(store.submit(7, publication(one), 0) == ResultV1::OwnerNotRegistered);
    store.addOwner(7);
    store.addOwner(9);
    assert(store.submit(7, publication(one), 10) == ResultV1::Ok);
    assert(store.snapshot(11, batch) && batch.spheres.size() == 1);
    store.removeOwner(7);
    assert(!store.snapshot(11, batch) && batch.empty());
    assert(store.submit(7, publication(one), 10) == ResultV1::OwnerNotRegistered);

    // Owners are isolated: one owner's clear leaves the other visible.
    store.addOwner(7);
    const std::vector two{ sphere(1.0f), sphere(2.0f) };
    assert(store.submit(7, publication(one), 10) == ResultV1::Ok);
    assert(store.submit(9, publication(two), 10) == ResultV1::Ok);
    assert(store.snapshot(11, batch) && batch.spheres.size() == 3);
    assert(store.clear(7) == ResultV1::Ok);
    assert(store.snapshot(11, batch) && batch.spheres.size() == 2);

    // A lease counts rendered frames from the frame current at submission.
    assert(store.submit(9, publication(two, 3), 20) == ResultV1::Ok);
    assert(store.snapshot(23, batch));
    assert(!store.snapshot(24, batch));
    assert(store.submit(9, publication(two, 0), 20) == ResultV1::InvalidArgument);
    assert(store.submit(9, publication(two, RPSUI_WORLD_DRAW_MAX_LEASE_FRAMES + 1), 20) ==
        ResultV1::InvalidArgument);

    // An empty publication clears; a rejected one leaves nothing half-applied.
    assert(store.submit(9, publication(two), 30) == ResultV1::Ok);
    assert(store.submit(9, publication({}), 30) == ResultV1::Ok);
    assert(!store.snapshot(31, batch));
    std::vector bad{ sphere(1.0f), sphere(std::numeric_limits<float>::quiet_NaN()) };
    assert(store.submit(9, publication(two), 30) == ResultV1::Ok);
    assert(store.submit(9, publication(bad), 30) == ResultV1::InvalidArgument);
    assert(!store.snapshot(31, batch));
    std::vector<WorldSphereV1> tooMany(RPSUI_WORLD_DRAW_MAX_SPHERES + 1, sphere(1.0f));
    assert(store.submit(9, publication(tooMany), 30) == ResultV1::CapacityReached);
    WorldDrawPublicationV1 wrongVersion = publication(one);
    wrongVersion.apiVersion = RPSUI_WORLD_DRAW_VERSION + 1;
    assert(store.submit(9, wrongVersion, 30) == ResultV1::VersionMismatch);

    // Text is always terminated inside its capacity.
    WorldTextV1 text{};
    for (auto& character : text.text) character = 'A';
    WorldDrawPublicationV1 withText{};
    withText.textCount = 1;
    withText.texts = &text;
    assert(store.submit(9, withText, 40) == ResultV1::Ok);
    assert(store.snapshot(41, batch) && batch.texts.size() == 1 &&
        batch.texts[0].text[RPSUI_WORLD_DRAW_TEXT_CAPACITY - 1] == '\0');

    // Meshes: validated, owner-scoped, replaced by id, and skipped once released.
    auto mesh = triangle();
    assert(store.registerMesh(9, 5, mesh) == ResultV1::Ok);
    const std::uint32_t outOfRange[3]{ 0, 1, 3 };
    auto badMesh = triangle();
    badMesh.indices = outOfRange;
    assert(store.registerMesh(9, 6, badMesh) == ResultV1::InvalidArgument);
    badMesh = triangle();
    badMesh.indexCount = 2;
    assert(store.registerMesh(9, 6, badMesh) == ResultV1::InvalidArgument);
    assert(store.registerMesh(4, 5, mesh) == ResultV1::OwnerNotRegistered);

    WorldMeshInstanceV1 instances[3]{};
    instances[0].meshId = 5;
    instances[1].meshId = 5;
    instances[2].meshId = 77; // Unknown: skipped, not an error.
    WorldDrawPublicationV1 withMeshes{};
    withMeshes.meshInstanceCount = 3;
    withMeshes.meshInstances = instances;
    assert(store.submit(9, withMeshes, 50) == ResultV1::Ok);
    assert(store.snapshot(51, batch) && batch.instances.size() == 2 && batch.meshes.size() == 1);
    const auto firstRevision = batch.meshes[0].geometry->revision;
    assert(store.registerMesh(9, 5, mesh) == ResultV1::Ok);
    assert(store.snapshot(51, batch) && batch.meshes[0].geometry->revision != firstRevision);
    // Owner 7 cannot use owner 9's mesh id.
    assert(store.submit(7, withMeshes, 50) == ResultV1::Ok);
    assert(store.snapshot(51, batch) && batch.instances.size() == 2);
    assert(store.releaseMesh(9, 5) == ResultV1::Ok);
    assert(store.releaseMesh(9, 5) == ResultV1::InvalidArgument);
    assert(!store.snapshot(51, batch));

    // Per-owner mesh count limit.
    for (std::uint64_t id = 0; id < RPSUI_WORLD_DRAW_MAX_MESHES; ++id) {
        assert(store.registerMesh(9, 1000 + id, mesh) == ResultV1::Ok);
    }
    assert(store.registerMesh(9, 99999, mesh) == ResultV1::CapacityReached);
    assert(store.registerMesh(9, 1000, mesh) == ResultV1::Ok); // Replacing stays allowed.
    return 0;
}
