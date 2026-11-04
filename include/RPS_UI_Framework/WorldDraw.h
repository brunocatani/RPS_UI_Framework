#pragma once
#include "Abi.h"

// World drawing for gameplay indicators and developer diagnostics. Every
// position is game units in world space. Everything is drawn into both eyes
// after UI panels, with the depth test off, so it stays visible through
// geometry. Text is anchored to a world point; there is no screen-positioned
// drawing. Uses a Framework owner token. The host copies every array during the
// call and owns all GPU resources; nothing is borrowed.
//
// A producer whose data is only final late in the frame (for example after the
// physics step) registers a frame callback and submits from it.
#pragma pack(push, 8)
namespace rpsui::api::world_draw
{
    inline constexpr InterfaceId kInterfaceId = InterfaceId::WorldDraw;
    inline constexpr std::uint32_t kMajor = 2;
    inline constexpr std::uint32_t kMinor = 0;

    // Per-owner capacities. A publication above any capacity is rejected whole.
    inline constexpr std::uint32_t kMaxSpheres = 256;
    inline constexpr std::uint32_t kMaxLines = 32768;
    inline constexpr std::uint32_t kMaxTexts = 256;
    inline constexpr std::uint32_t kMaxMeshInstances = 2048;
    inline constexpr std::uint32_t kMaxMeshes = 4096;
    inline constexpr std::uint32_t kMaxMeshVertices = 65535;
    inline constexpr std::uint32_t kMaxMeshIndices = 393216;
    inline constexpr std::uint64_t kMaxMeshBytes = 64ull * 1024 * 1024;
    inline constexpr std::uint32_t kMaxLeaseFrames = 120;
    inline constexpr std::uint32_t kTextCapacity = 128;

    enum class Fill : std::uint32_t
    {
        Solid = 0,
        Wire = 1,
    };

    struct Sphere
    {
        float center[3]{};
        float radius{ 1.0f };
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        Fill fill{ Fill::Solid };
        std::uint32_t reserved[3]{};
    };

    struct Line
    {
        float start[3]{};
        float end[3]{};
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // 5x7 block glyphs: A-Z, 0-9 and - + = . , : / ( ). Lower case draws as
    // upper case; other characters leave a blank cell. Each eye projects the
    // anchor and draws the glyphs at that point, offset and sized in eye-texture
    // pixels, kept inside the eye.
    struct Text
    {
        char text[kTextCapacity]{};
        float anchor[3]{};
        float offsetPixels[2]{ 20.0f, 0.0f };
        float pixelSize{ 3.0f };
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // transform uses the row-vector convention (position * transform), with the
    // translation in elements 12..14, as DirectX::XMFLOAT4X4 stores it.
    struct MeshInstance
    {
        std::uint64_t meshId{ 0 };
        float transform[16]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        Fill fill{ Fill::Wire };
        std::uint32_t reserved{};
    };

    // Triangle list in mesh-local game units. positions holds vertexCount xyz
    // triples; every index must be below vertexCount.
    struct Mesh
    {
        std::uint32_t size{ sizeof(Mesh) };
        std::uint32_t version{ kRecordVersion };
        const float* positions{ nullptr };
        std::uint32_t vertexCount{ 0 };
        std::uint32_t indexCount{ 0 };
        const std::uint32_t* indices{ nullptr };
    };

    // Replaces the owner's previous publication. It stays visible for
    // leaseFrames rendered frames (1..kMaxLeaseFrames) unless it is replaced or
    // cleared; republish every frame for continuous display. An empty
    // publication clears.
    struct Publication
    {
        std::uint32_t size{ sizeof(Publication) };
        std::uint32_t version{ kRecordVersion };
        std::uint32_t leaseFrames{ 2 };
        std::uint32_t sphereCount{ 0 };
        std::uint32_t lineCount{ 0 };
        std::uint32_t textCount{ 0 };
        std::uint32_t meshInstanceCount{ 0 };
        std::uint32_t reserved{ 0 };
        const Sphere* spheres{ nullptr };
        const Line* lines{ nullptr };
        const Text* texts{ nullptr };
        const MeshInstance* meshInstances{ nullptr };
    };

    // Called on the render thread once per drawn frame, before the host takes
    // its snapshot. The callback may call submit, clear, registerMesh and
    // releaseMesh for its own owner; it must not call other RPS UI functions.
    // Submitting from it with leaseFrames = 1 shows the data for exactly this
    // frame. It must stay brief: it delays the frame.
    using FrameCallback = void(RPSUI_CALL*)(void* context, std::uint64_t frame) noexcept;

    struct Api
    {
        static constexpr InterfaceId interfaceId = kInterfaceId;
        static constexpr std::uint32_t majorVersion = kMajor;
        static constexpr std::uint32_t minorVersion = kMinor;

        Result(RPSUI_CALL* submit)(std::uint64_t ownerToken, const Publication* publication) noexcept {};
        Result(RPSUI_CALL* clear)(std::uint64_t ownerToken) noexcept {};
        // Meshes belong to the owner until released or the owner unregisters.
        // Registering an existing id replaces its geometry. Instances naming an
        // unknown id are skipped.
        Result(RPSUI_CALL* registerMesh)(std::uint64_t ownerToken, std::uint64_t meshId, const Mesh* mesh) noexcept {};
        Result(RPSUI_CALL* releaseMesh)(std::uint64_t ownerToken, std::uint64_t meshId) noexcept {};
        // Replaces the owner's frame callback; null removes it. After the call
        // returns the previous callback is not started again. CallbackBusy
        // means it is still running: retry until Ok before releasing its
        // context. An owner with a callback must unregister until Ok as well.
        Result(RPSUI_CALL* setFrameCallback)(std::uint64_t ownerToken, FrameCallback callback, void* context) noexcept {};
    };

    static_assert(std::is_standard_layout_v<Sphere> && sizeof(Sphere) == 48);
    static_assert(std::is_standard_layout_v<Line> && sizeof(Line) == 40);
    static_assert(std::is_standard_layout_v<Text> && sizeof(Text) == 168);
    static_assert(std::is_standard_layout_v<MeshInstance> && sizeof(MeshInstance) == 96);
    static_assert(std::is_standard_layout_v<Mesh> && sizeof(Mesh) == 32);
    static_assert(std::is_standard_layout_v<Publication> && sizeof(Publication) == 64);
    static_assert(std::is_standard_layout_v<Api> && sizeof(Api) == 40);
}
#pragma pack(pop)
