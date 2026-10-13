#pragma once
#include "RPSUIFrameworkApi.h"

namespace rpsui::sdk
{
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_VERSION = 1;

    // Per-owner capacities. A publication above any capacity is rejected whole.
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_SPHERES = 256;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_LINES = 32768;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_TEXTS = 256;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_MESH_INSTANCES = 2048;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_MESHES = 4096;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_MESH_VERTICES = 65535;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_MESH_INDICES = 393216;
    inline constexpr std::uint64_t RPSUI_WORLD_DRAW_MAX_MESH_BYTES = 64ull * 1024 * 1024;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_MAX_LEASE_FRAMES = 120;
    inline constexpr std::uint32_t RPSUI_WORLD_DRAW_TEXT_CAPACITY = 128;

    // World drawing for gameplay indicators and developer diagnostics. Every
    // position is game units in world space. Everything is drawn into both eyes
    // after UI panels, with the depth test off, so it stays visible through
    // geometry. Text is anchored to a world point; there is no screen-positioned
    // drawing. Uses an existing consumer owner token. The host copies every
    // array during the call and owns all GPU resources; nothing is borrowed.
    //
    // A producer whose data is only final late in the frame (for example after
    // the physics step) registers a frame callback and submits from it.
    enum class WorldFillV1 : std::uint32_t
    {
        Solid = 0,
        Wire = 1,
    };

    struct WorldSphereV1
    {
        float center[3]{};
        float radius{ 1.0f };
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        WorldFillV1 fill{ WorldFillV1::Solid };
        std::uint32_t reserved[3]{};
    };

    struct WorldLineV1
    {
        float start[3]{};
        float end[3]{};
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // 5x7 block glyphs: A-Z, 0-9 and - + = . , : / ( ). Lower case draws as
    // upper case; other characters leave a blank cell. Each eye projects the anchor and draws the glyphs at that
    // point, offset and sized in eye-texture pixels, kept inside the eye.
    struct WorldTextV1
    {
        char text[RPSUI_WORLD_DRAW_TEXT_CAPACITY]{};
        float anchor[3]{};
        float offsetPixels[2]{ 20.0f, 0.0f };
        float pixelSize{ 3.0f };
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // transform uses the row-vector convention (position * transform), with the
    // translation in elements 12..14, as DirectX::XMFLOAT4X4 stores it.
    struct WorldMeshInstanceV1
    {
        std::uint64_t meshId{ 0 };
        float transform[16]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        WorldFillV1 fill{ WorldFillV1::Wire };
        std::uint32_t reserved{};
    };

    // Triangle list in mesh-local game units. positions holds vertexCount xyz
    // triples; every index must be below vertexCount.
    struct WorldMeshV1
    {
        std::uint32_t structSize{ sizeof(WorldMeshV1) };
        std::uint32_t apiVersion{ RPSUI_WORLD_DRAW_VERSION };
        const float* positions{ nullptr };
        std::uint32_t vertexCount{ 0 };
        std::uint32_t indexCount{ 0 };
        const std::uint32_t* indices{ nullptr };
    };

    // Replaces the owner's previous publication. It stays visible for
    // leaseFrames rendered frames (1..RPSUI_WORLD_DRAW_MAX_LEASE_FRAMES) unless
    // it is replaced or cleared; republish every frame for continuous display.
    // An empty publication clears.
    struct WorldDrawPublicationV1
    {
        std::uint32_t structSize{ sizeof(WorldDrawPublicationV1) };
        std::uint32_t apiVersion{ RPSUI_WORLD_DRAW_VERSION };
        std::uint32_t leaseFrames{ 2 };
        std::uint32_t sphereCount{ 0 };
        std::uint32_t lineCount{ 0 };
        std::uint32_t textCount{ 0 };
        std::uint32_t meshInstanceCount{ 0 };
        std::uint32_t reserved{ 0 };
        const WorldSphereV1* spheres{ nullptr };
        const WorldLineV1* lines{ nullptr };
        const WorldTextV1* texts{ nullptr };
        const WorldMeshInstanceV1* meshInstances{ nullptr };
    };

    // Called on the render thread once per drawn frame, before the host takes
    // its snapshot. The callback may call submit, clear, registerMesh and
    // releaseMesh for its own owner; it must not call other RPS UI functions.
    // Submitting from it with leaseFrames = 1 shows the data for exactly this
    // frame. It must stay brief: it delays the frame.
    using WorldDrawFrameCallbackV1 = void(RPSUI_CALL*)(void* context, std::uint64_t frame) noexcept;

    struct WorldDrawApiV1
    {
        std::uint32_t structSize{ sizeof(WorldDrawApiV1) };
        std::uint32_t apiVersion{ RPSUI_WORLD_DRAW_VERSION };
        std::uint32_t apiFlavor{ RPSUI_API_FLAVOR };
        std::uint32_t reserved32{};
        ResultV1 (RPSUI_CALL* submit)(std::uint64_t ownerToken,
            const WorldDrawPublicationV1* publication) noexcept{};
        ResultV1 (RPSUI_CALL* clear)(std::uint64_t ownerToken) noexcept{};
        // Meshes belong to the owner until released or the owner unregisters.
        // Registering an existing id replaces its geometry. Instances naming an
        // unknown id are skipped.
        ResultV1 (RPSUI_CALL* registerMesh)(std::uint64_t ownerToken,
            std::uint64_t meshId, const WorldMeshV1* mesh) noexcept{};
        ResultV1 (RPSUI_CALL* releaseMesh)(std::uint64_t ownerToken,
            std::uint64_t meshId) noexcept{};
        // Replaces the owner's frame callback; null removes it. After the call
        // returns the previous callback is not started again. CallbackBusy
        // means it is still running: retry until Ok before releasing its
        // context. An owner with a callback must unregister until Ok as well.
        ResultV1 (RPSUI_CALL* setFrameCallback)(std::uint64_t ownerToken,
            WorldDrawFrameCallbackV1 callback, void* context) noexcept{};
    };

    // Returns null when RPS UI Framework is absent or does not offer this
    // version. Consumers keep working without it; they only lose the display.
    [[nodiscard]] inline const WorldDrawApiV1* RequestWorldDrawApiV1() noexcept
    {
        const auto module = GetModuleHandleW(L"RPS_UI_Framework.dll");
        if (!module) return nullptr;
        const auto address = GetProcAddress(module, "RPSUI_RequestWorldDrawApi");
        if (!address) return nullptr;
        using Request = const WorldDrawApiV1* (RPSUI_CALL*)(std::uint32_t) noexcept;
        const auto* api = reinterpret_cast<Request>(address)(RPSUI_WORLD_DRAW_VERSION);
        return api && api->structSize >= sizeof(WorldDrawApiV1) &&
            api->apiVersion == RPSUI_WORLD_DRAW_VERSION && api->apiFlavor == RPSUI_API_FLAVOR &&
            api->submit && api->clear && api->registerMesh && api->releaseMesh && api->setFrameCallback ?
            api : nullptr;
    }

    static_assert(std::is_standard_layout_v<WorldSphereV1> && sizeof(WorldSphereV1) == 48);
    static_assert(std::is_standard_layout_v<WorldLineV1> && sizeof(WorldLineV1) == 40);
    static_assert(std::is_standard_layout_v<WorldTextV1> && sizeof(WorldTextV1) == 168);
    static_assert(std::is_standard_layout_v<WorldMeshInstanceV1> && sizeof(WorldMeshInstanceV1) == 96);
    static_assert(std::is_standard_layout_v<WorldMeshV1> && sizeof(WorldMeshV1) == 32);
    static_assert(std::is_standard_layout_v<WorldDrawPublicationV1> && sizeof(WorldDrawPublicationV1) == 64);
    static_assert(std::is_standard_layout_v<WorldDrawApiV1> && sizeof(WorldDrawApiV1) == 56);
}
