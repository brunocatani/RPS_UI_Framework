#include "PCH.h"

#include "Logger.h"
#include "WorldDrawStore.h"
#include "render/StereoProjection.h"
#include "render/WorldDrawRenderer.h"

#include <d3dcompiler.h>

namespace rpsui::render::WorldDrawRenderer
{
    namespace
    {
        // Shaders, vertex layouts, states and budgets are ROCK's world overlay
        // renderer's, unchanged, so moved drawings keep their appearance. Each
        // instance or vertex is drawn twice; SV_InstanceID & 1 selects the eye,
        // and the clip distance confines it to that half of the stereo texture.
        constexpr char kInstancedVertex[] = R"(
struct VS_INPUT {
    float3 vPos : POS;
    float4 modelRow0 : IROW0;
    float4 modelRow1 : IROW1;
    float4 modelRow2 : IROW2;
    float4 modelRow3 : IROW3;
    float4 color : ICOLOR0;
    uint instanceId : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
    float clipDistance : SV_ClipDistance0;
    float cullDistance : SV_CullDistance0;
};

cbuffer Camera : register(b0) {
    column_major float4x4 matProjView[2];
    float4 posAdjust[2];
};

VS_OUTPUT main(VS_INPUT input) {
    const float4 eyeClipEdge[2] = { { -1, 0, 0, 1 }, { 1, 0, 0, 1 } };
    const float eyeOffsetScale[2] = { -0.5, 0.5 };
    const uint eyeIndex = input.instanceId & 1;
    row_major float4x4 matModel = float4x4(input.modelRow0, input.modelRow1, input.modelRow2, input.modelRow3);

    float4 pos = float4(input.vPos.xyz, 1.0f);
    pos = mul(pos, matModel);
    pos.xyz -= posAdjust[eyeIndex].xyz;
    pos = mul(matProjView[eyeIndex], pos);

    VS_OUTPUT output;
    output.vColor = input.color;
    output.clipDistance = dot(pos, eyeClipEdge[eyeIndex]);
    output.cullDistance = output.clipDistance;
    output.vPos = pos;
    output.vPos.x *= 0.5;
    output.vPos.x += eyeOffsetScale[eyeIndex] * output.vPos.w;
    return output;
}
)";

        constexpr char kStereoColorVertex[] = R"(
struct VS_INPUT {
    float3 vPos : POS;
    float4 color : COLOR0;
    uint instanceId : SV_InstanceID;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
    float clipDistance : SV_ClipDistance0;
    float cullDistance : SV_CullDistance0;
};

cbuffer Camera : register(b0) {
    column_major float4x4 matProjView[2];
    float4 posAdjust[2];
};

VS_OUTPUT main(VS_INPUT input) {
    const float4 eyeClipEdge[2] = { { -1, 0, 0, 1 }, { 1, 0, 0, 1 } };
    const float eyeOffsetScale[2] = { -0.5, 0.5 };
    const uint eyeIndex = input.instanceId & 1;

    float4 pos = float4(input.vPos.xyz, 1.0f);
    pos.xyz -= posAdjust[eyeIndex].xyz;
    pos = mul(matProjView[eyeIndex], pos);

    VS_OUTPUT output;
    output.vColor = input.color;
    output.clipDistance = dot(pos, eyeClipEdge[eyeIndex]);
    output.cullDistance = output.clipDistance;
    output.vPos = pos;
    output.vPos.x *= 0.5;
    output.vPos.x += eyeOffsetScale[eyeIndex] * output.vPos.w;
    return output;
}
)";

        // Text glyphs arrive already projected per eye, in clip space.
        constexpr char kProjectedTextVertex[] = R"(
struct VS_INPUT {
    float3 vPos : POS;
    float4 color : COLOR0;
};

struct VS_OUTPUT {
    float4 vPos : SV_POSITION;
    float4 vColor : COLOR0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.vPos = float4(input.vPos.xy, 0.0f, 1.0f);
    output.vColor = input.color;
    return output;
}
)";

        constexpr char kPixel[] = R"(
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PS_INPUT input) : SV_Target {
    return input.color;
}
)";

        constexpr UINT kInstanceCapacity = 2048;
        constexpr UINT kLineVertexCapacity = 65536;
        constexpr UINT kTextVertexCapacity = 131072;
        constexpr UINT kMeshUploadsPerFrame = 64;
        constexpr std::uint64_t kMeshIdleFrames = 600;

        struct Position
        {
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
        };

        struct ColoredVertex
        {
            float x{ 0.0f };
            float y{ 0.0f };
            float z{ 0.0f };
            float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        struct alignas(16) InstanceData
        {
            DirectX::XMFLOAT4X4 model{};
            float color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        };
        static_assert(sizeof(InstanceData) == 80);

        struct alignas(16) CameraData
        {
            DirectX::XMFLOAT4X4 projView[2]{};
            DirectX::XMFLOAT4 originAdjust[2]{};
        };

        struct GpuMesh
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> vertices;
            Microsoft::WRL::ComPtr<ID3D11Buffer> indices;
            UINT indexCount{ 0 };
            std::uint64_t revision{ 0 };
            std::uint64_t lastUsedFrame{ 0 };
        };

        struct Resources
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            Microsoft::WRL::ComPtr<ID3D11VertexShader> instancedVertex;
            Microsoft::WRL::ComPtr<ID3D11VertexShader> stereoColorVertex;
            Microsoft::WRL::ComPtr<ID3D11VertexShader> projectedTextVertex;
            Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
            Microsoft::WRL::ComPtr<ID3D11InputLayout> instancedLayout;
            Microsoft::WRL::ComPtr<ID3D11InputLayout> coloredLayout;
            Microsoft::WRL::ComPtr<ID3D11Buffer> camera;
            Microsoft::WRL::ComPtr<ID3D11Buffer> instances;
            Microsoft::WRL::ComPtr<ID3D11Buffer> lineVertices;
            Microsoft::WRL::ComPtr<ID3D11Buffer> textVertices;
            Microsoft::WRL::ComPtr<ID3D11RasterizerState> wire;
            Microsoft::WRL::ComPtr<ID3D11RasterizerState> solid;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthOff;
            Microsoft::WRL::ComPtr<ID3D11BlendState> blend;
            GpuMesh unitSphere;
            std::unordered_map<world_draw::MeshKey, GpuMesh, world_draw::MeshKeyHash> meshes;
            std::vector<const GpuMesh*> frameMeshes;
            std::vector<std::uint32_t> order;
            std::vector<InstanceData> instanceScratch;
            std::vector<ColoredVertex> vertexScratch;
            bool failed{ false };
        };

        [[nodiscard]] Resources& resources() noexcept
        {
            static auto* value = new Resources();
            return *value;
        }

        std::atomic_bool g_uploadFailureReported{ false };
        std::atomic_bool g_drawFailureReported{ false };

        void reportOnce(std::atomic_bool& flag, const char* message) noexcept
        {
            if (!flag.exchange(true, std::memory_order_relaxed)) {
                log::warn("RPS UI world drawing: {}", message);
            }
        }

        [[nodiscard]] bool compile(const char* source, std::size_t length, const char* name,
            const char* target, Microsoft::WRL::ComPtr<ID3DBlob>& blob) noexcept
        {
            Microsoft::WRL::ComPtr<ID3DBlob> errors;
            const HRESULT result = D3DCompile(source, length, name, nullptr, nullptr, "main", target,
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR, 0,
                blob.GetAddressOf(), errors.GetAddressOf());
            if (FAILED(result)) {
                log::error("RPS UI world drawing shader {} failed to compile: {}", name,
                    errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no diagnostics");
                return false;
            }
            return true;
        }

        [[nodiscard]] bool createMesh(ID3D11Device* device, const float* positions, UINT vertexCount,
            const std::uint32_t* indices, UINT indexCount, GpuMesh& out) noexcept
        {
            D3D11_BUFFER_DESC vertexDescription{};
            vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
            vertexDescription.ByteWidth = vertexCount * sizeof(Position);
            vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            const D3D11_SUBRESOURCE_DATA vertexData{ positions, 0, 0 };
            D3D11_BUFFER_DESC indexDescription{};
            indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
            indexDescription.ByteWidth = indexCount * sizeof(std::uint32_t);
            indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
            const D3D11_SUBRESOURCE_DATA indexData{ indices, 0, 0 };
            GpuMesh mesh{};
            if (FAILED(device->CreateBuffer(&vertexDescription, &vertexData, mesh.vertices.GetAddressOf())) ||
                FAILED(device->CreateBuffer(&indexDescription, &indexData, mesh.indices.GetAddressOf()))) {
                return false;
            }
            mesh.indexCount = indexCount;
            out = std::move(mesh);
            return true;
        }

        // ROCK's canonical unit sphere: 12 rings by 12 segments.
        [[nodiscard]] bool createUnitSphere(ID3D11Device* device, GpuMesh& out) noexcept
        {
            constexpr int segments = 12;
            constexpr int rings = 12;
            constexpr float pi = 3.14159265358979323846f;
            std::vector<float> positions;
            std::vector<std::uint32_t> indices;
            try {
                for (int ring = 0; ring <= rings; ++ring) {
                    const float theta = ring / static_cast<float>(rings) * pi;
                    for (int segment = 0; segment <= segments; ++segment) {
                        const float phi = segment / static_cast<float>(segments) * pi * 2.0f;
                        positions.push_back(std::cos(phi) * std::sin(theta));
                        positions.push_back(std::cos(theta));
                        positions.push_back(std::sin(phi) * std::sin(theta));
                    }
                }
                for (int ring = 0; ring < rings; ++ring) {
                    for (int segment = 0; segment < segments; ++segment) {
                        const auto index = static_cast<std::uint32_t>(ring * (segments + 1) + segment);
                        indices.insert(indices.end(), { index, index + segments + 1, index + 1, index + 1,
                            index + segments + 1, index + segments + 2 });
                    }
                }
            } catch (...) {
                return false;
            }
            return createMesh(device, positions.data(), static_cast<UINT>(positions.size() / 3), indices.data(),
                static_cast<UINT>(indices.size()), out);
        }

        [[nodiscard]] bool createDynamicBuffer(ID3D11Device* device, UINT bytes, UINT bind,
            Microsoft::WRL::ComPtr<ID3D11Buffer>& out) noexcept
        {
            D3D11_BUFFER_DESC description{};
            description.Usage = D3D11_USAGE_DYNAMIC;
            description.ByteWidth = bytes;
            description.BindFlags = bind;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            return SUCCEEDED(device->CreateBuffer(&description, nullptr, out.GetAddressOf()));
        }

        [[nodiscard]] bool initialize(Resources& state, ID3D11Device* device) noexcept
        {
            if (state.device.Get() == device) return !state.failed;
            state = {};
            state.device = device;
            state.failed = true;

            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            if (!compile(kInstancedVertex, sizeof(kInstancedVertex) - 1, "RPSUIWorldInstancedVS", "vs_5_0", blob) ||
                FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                    state.instancedVertex.GetAddressOf()))) {
                return false;
            }
            const D3D11_INPUT_ELEMENT_DESC instancedLayout[] = {
                { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "IROW", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 2 },
                { "IROW", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 2 },
                { "IROW", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 2 },
                { "IROW", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 2 },
                { "ICOLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 64, D3D11_INPUT_PER_INSTANCE_DATA, 2 },
            };
            if (FAILED(device->CreateInputLayout(instancedLayout, static_cast<UINT>(std::size(instancedLayout)),
                    blob->GetBufferPointer(), blob->GetBufferSize(), state.instancedLayout.GetAddressOf()))) {
                return false;
            }

            blob.Reset();
            if (!compile(kStereoColorVertex, sizeof(kStereoColorVertex) - 1, "RPSUIWorldColorVS", "vs_5_0", blob) ||
                FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                    state.stereoColorVertex.GetAddressOf()))) {
                return false;
            }
            const D3D11_INPUT_ELEMENT_DESC coloredLayout[] = {
                { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            if (FAILED(device->CreateInputLayout(coloredLayout, static_cast<UINT>(std::size(coloredLayout)),
                    blob->GetBufferPointer(), blob->GetBufferSize(), state.coloredLayout.GetAddressOf()))) {
                return false;
            }

            blob.Reset();
            if (!compile(kProjectedTextVertex, sizeof(kProjectedTextVertex) - 1, "RPSUIWorldTextVS", "vs_5_0", blob) ||
                FAILED(device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                    state.projectedTextVertex.GetAddressOf()))) {
                return false;
            }

            blob.Reset();
            if (!compile(kPixel, sizeof(kPixel) - 1, "RPSUIWorldPS", "ps_5_0", blob) ||
                FAILED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                    state.pixel.GetAddressOf()))) {
                return false;
            }

            if (!createDynamicBuffer(device, sizeof(CameraData), D3D11_BIND_CONSTANT_BUFFER, state.camera) ||
                !createDynamicBuffer(device, sizeof(InstanceData) * kInstanceCapacity, D3D11_BIND_VERTEX_BUFFER,
                    state.instances) ||
                !createDynamicBuffer(device, sizeof(ColoredVertex) * kLineVertexCapacity, D3D11_BIND_VERTEX_BUFFER,
                    state.lineVertices) ||
                !createDynamicBuffer(device, sizeof(ColoredVertex) * kTextVertexCapacity, D3D11_BIND_VERTEX_BUFFER,
                    state.textVertices)) {
                return false;
            }

            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode = D3D11_FILL_WIREFRAME;
            raster.CullMode = D3D11_CULL_NONE;
            raster.FrontCounterClockwise = TRUE;
            raster.DepthClipEnable = TRUE;
            if (FAILED(device->CreateRasterizerState(&raster, state.wire.GetAddressOf()))) return false;
            raster.FillMode = D3D11_FILL_SOLID;
            if (FAILED(device->CreateRasterizerState(&raster, state.solid.GetAddressOf()))) return false;

            D3D11_DEPTH_STENCIL_DESC depth{};
            depth.DepthEnable = FALSE;
            depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (FAILED(device->CreateDepthStencilState(&depth, state.depthOff.GetAddressOf()))) return false;

            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].BlendEnable = TRUE;
            blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(device->CreateBlendState(&blend, state.blend.GetAddressOf()))) return false;

            if (!createUnitSphere(device, state.unitSphere)) return false;

            state.failed = false;
            log::info("RPS UI world drawing initialized");
            return true;
        }

        [[nodiscard]] bool uploadCamera(Resources& state, ID3D11DeviceContext* context,
            const StereoProjection::Snapshot& projection) noexcept
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(state.camera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.pData) {
                reportOnce(g_uploadFailureReported, "camera buffer map failed; frame skipped");
                return false;
            }
            auto* camera = static_cast<CameraData*>(mapped.pData);
            camera->projView[0] = projection.composite[0];
            camera->projView[1] = projection.composite[1];
            camera->originAdjust[0] = projection.origin[0];
            camera->originAdjust[1] = projection.origin[1];
            context->Unmap(state.camera.Get(), 0);
            ID3D11Buffer* buffer = state.camera.Get();
            context->VSSetConstantBuffers(0, 1, &buffer);
            return true;
        }

        // Draws instances of one mesh with the instanced shader, in chunks that
        // fit the instance buffer.
        void drawMeshInstances(Resources& state, ID3D11DeviceContext* context, const GpuMesh& mesh,
            std::span<const InstanceData> instances, ID3D11RasterizerState* raster) noexcept
        {
            if (instances.empty() || !mesh.vertices || !mesh.indices || mesh.indexCount == 0) return;
            context->IASetInputLayout(state.instancedLayout.Get());
            context->VSSetShader(state.instancedVertex.Get(), nullptr, 0);
            context->PSSetShader(state.pixel.Get(), nullptr, 0);
            context->RSSetState(raster);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->IASetIndexBuffer(mesh.indices.Get(), DXGI_FORMAT_R32_UINT, 0);
            for (std::size_t start = 0; start < instances.size(); start += kInstanceCapacity) {
                const auto count = static_cast<UINT>((std::min)(instances.size() - start,
                    static_cast<std::size_t>(kInstanceCapacity)));
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context->Map(state.instances.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) ||
                    !mapped.pData) {
                    reportOnce(g_uploadFailureReported, "instance buffer map failed; batch skipped");
                    return;
                }
                std::memcpy(mapped.pData, instances.data() + start, count * sizeof(InstanceData));
                context->Unmap(state.instances.Get(), 0);
                ID3D11Buffer* buffers[2] = { mesh.vertices.Get(), state.instances.Get() };
                const UINT strides[2] = { sizeof(Position), sizeof(InstanceData) };
                constexpr UINT offsets[2] = { 0, 0 };
                context->IASetVertexBuffers(0, 2, buffers, strides, offsets);
                context->DrawIndexedInstanced(mesh.indexCount, count * 2, 0, 0, 0);
            }
        }

        [[nodiscard]] InstanceData instance(const float* transform, const float* color) noexcept
        {
            InstanceData data{};
            std::memcpy(&data.model, transform, sizeof(data.model));
            std::copy_n(color, 4, data.color);
            return data;
        }

        // Uploads new or changed meshes within the per-frame budget, then maps
        // each batch mesh to its GPU copy (null when not uploaded yet).
        void prepareMeshes(Resources& state, ID3D11Device* device, const world_draw::RenderBatch& batch,
            std::uint64_t frame)
        {
            state.frameMeshes.assign(batch.meshes.size(), nullptr);
            UINT uploads = 0;
            for (std::size_t index = 0; index < batch.meshes.size(); ++index) {
                const auto& reference = batch.meshes[index];
                auto found = state.meshes.find(reference.key);
                if (found == state.meshes.end() || found->second.revision != reference.geometry->revision) {
                    if (uploads >= kMeshUploadsPerFrame) continue;
                    ++uploads;
                    GpuMesh mesh{};
                    const auto& geometry = *reference.geometry;
                    if (!createMesh(device, geometry.positions.data(), static_cast<UINT>(geometry.positions.size() / 3),
                            geometry.indices.data(), static_cast<UINT>(geometry.indices.size()), mesh)) {
                        reportOnce(g_uploadFailureReported, "mesh upload failed; its instances are skipped");
                        continue;
                    }
                    mesh.revision = geometry.revision;
                    found = state.meshes.insert_or_assign(reference.key, std::move(mesh)).first;
                }
                found->second.lastUsedFrame = frame;
                state.frameMeshes[index] = &found->second;
            }
        }

        void evictIdleMeshes(Resources& state, std::uint64_t frame) noexcept
        {
            std::erase_if(state.meshes, [frame](const auto& entry) {
                return frame - entry.second.lastUsedFrame > kMeshIdleFrames;
            });
        }

        void drawMeshes(Resources& state, ID3D11DeviceContext* context, const world_draw::RenderBatch& batch)
        {
            if (batch.instances.empty()) return;
            // Group by mesh and fill so each run is one instanced draw.
            state.order.resize(batch.instances.size());
            for (std::uint32_t index = 0; index < state.order.size(); ++index) state.order[index] = index;
            std::stable_sort(state.order.begin(), state.order.end(), [&](std::uint32_t left, std::uint32_t right) {
                const auto& a = batch.instances[left];
                const auto& b = batch.instances[right];
                return a.mesh != b.mesh ? a.mesh < b.mesh : a.fill < b.fill;
            });
            std::size_t start = 0;
            while (start < state.order.size()) {
                const auto& first = batch.instances[state.order[start]];
                std::size_t end = start + 1;
                while (end < state.order.size() && batch.instances[state.order[end]].mesh == first.mesh &&
                       batch.instances[state.order[end]].fill == first.fill) {
                    ++end;
                }
                if (const auto* mesh = state.frameMeshes[first.mesh]) {
                    state.instanceScratch.clear();
                    for (std::size_t index = start; index < end; ++index) {
                        const auto& draw = batch.instances[state.order[index]];
                        state.instanceScratch.push_back(instance(draw.transform, draw.color));
                    }
                    drawMeshInstances(state, context, *mesh, state.instanceScratch,
                        first.fill == sdk::WorldFillV1::Solid ? state.solid.Get() : state.wire.Get());
                }
                start = end;
            }
        }

        void drawSpheres(Resources& state, ID3D11DeviceContext* context, const world_draw::RenderBatch& batch)
        {
            for (const auto fill : { sdk::WorldFillV1::Solid, sdk::WorldFillV1::Wire }) {
                state.instanceScratch.clear();
                for (const auto& sphere : batch.spheres) {
                    if (sphere.fill != fill) continue;
                    InstanceData data{};
                    DirectX::XMStoreFloat4x4(&data.model,
                        DirectX::XMMatrixScaling(sphere.radius, sphere.radius, sphere.radius) *
                            DirectX::XMMatrixTranslation(sphere.center[0], sphere.center[1], sphere.center[2]));
                    std::copy_n(sphere.color, 4, data.color);
                    state.instanceScratch.push_back(data);
                }
                drawMeshInstances(state, context, state.unitSphere, state.instanceScratch,
                    fill == sdk::WorldFillV1::Solid ? state.solid.Get() : state.wire.Get());
            }
        }

        // Uploads colored vertices in chunks of the buffer capacity and draws
        // each chunk. Chunk sizes stay multiples of the primitive size.
        void drawColoredVertices(Resources& state, ID3D11DeviceContext* context, ID3D11Buffer* buffer,
            UINT capacity, UINT primitiveVertices, UINT instances) noexcept
        {
            const auto& vertices = state.vertexScratch;
            const UINT chunk = capacity - capacity % primitiveVertices;
            constexpr UINT stride = sizeof(ColoredVertex);
            constexpr UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            for (std::size_t start = 0; start < vertices.size(); start += chunk) {
                const auto count = static_cast<UINT>((std::min)(vertices.size() - start, static_cast<std::size_t>(chunk)));
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) || !mapped.pData) {
                    reportOnce(g_uploadFailureReported, "vertex buffer map failed; batch skipped");
                    return;
                }
                std::memcpy(mapped.pData, vertices.data() + start, count * sizeof(ColoredVertex));
                context->Unmap(buffer, 0);
                if (instances > 1) {
                    context->DrawInstanced(count, instances, 0, 0);
                } else {
                    context->Draw(count, 0);
                }
            }
        }

        void drawLines(Resources& state, ID3D11DeviceContext* context, const world_draw::RenderBatch& batch)
        {
            if (batch.lines.empty()) return;
            state.vertexScratch.clear();
            for (const auto& line : batch.lines) {
                ColoredVertex start{ line.start[0], line.start[1], line.start[2] };
                ColoredVertex end{ line.end[0], line.end[1], line.end[2] };
                std::copy_n(line.color, 4, start.color);
                std::copy_n(line.color, 4, end.color);
                state.vertexScratch.push_back(start);
                state.vertexScratch.push_back(end);
            }
            context->IASetInputLayout(state.coloredLayout.Get());
            context->VSSetShader(state.stereoColorVertex.Get(), nullptr, 0);
            context->PSSetShader(state.pixel.Get(), nullptr, 0);
            context->RSSetState(state.wire.Get());
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            drawColoredVertices(state, context, state.lineVertices.Get(), kLineVertexCapacity, 2, 2);
        }

        [[nodiscard]] std::array<std::uint8_t, 7> glyphRows(char ch) noexcept
        {
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            switch (ch) {
            case '0': return { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
            case '1': return { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case '2': return { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
            case '3': return { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
            case '4': return { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
            case '5': return { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
            case '6': return { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E };
            case '7': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
            case '8': return { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
            case '9': return { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
            case 'A': return { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'B': return { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
            case 'C': return { 0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F };
            case 'D': return { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
            case 'E': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
            case 'F': return { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
            case 'G': return { 0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F };
            case 'H': return { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
            case 'I': return { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
            case 'J': return { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
            case 'K': return { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
            case 'L': return { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
            case 'M': return { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
            case 'N': return { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
            case 'O': return { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'P': return { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
            case 'Q': return { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
            case 'R': return { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
            case 'S': return { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
            case 'T': return { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
            case 'U': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
            case 'V': return { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
            case 'W': return { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A };
            case 'X': return { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
            case 'Y': return { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
            case 'Z': return { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
            case '-': return { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
            case '+': return { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
            case '=': return { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 };
            case '.': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
            case ',': return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x08 };
            case ':': return { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
            case '/': return { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
            case '(': return { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
            case ')': return { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
            default: return { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
            }
        }

        void appendTextQuad(std::vector<ColoredVertex>& vertices, float x, float y, float size, float width,
            float height, const float* color)
        {
            const auto vertex = [&](float px, float py) {
                ColoredVertex result{ (px / width) * 2.0f - 1.0f, 1.0f - (py / height) * 2.0f, 0.0f };
                std::copy_n(color, 4, result.color);
                return result;
            };
            const auto a = vertex(x, y);
            const auto b = vertex(x + size, y);
            const auto c = vertex(x + size, y + size);
            const auto d = vertex(x, y + size);
            vertices.insert(vertices.end(), { a, b, c, a, c, d });
        }

        void appendGlyphs(std::vector<ColoredVertex>& vertices, const sdk::WorldTextV1& text, float baseX,
            float baseY, float maxX, float width, float height)
        {
            constexpr float kGlyphColumns = 5.0f;
            constexpr float kGlyphAdvanceColumns = 6.0f;
            const float pixel = (std::max)(1.0f, text.pixelSize);
            float cursorX = baseX;
            for (std::size_t i = 0; i < sizeof(text.text) && text.text[i] != '\0'; ++i) {
                const auto rows = glyphRows(text.text[i]);
                for (std::size_t row = 0; row < rows.size(); ++row) {
                    for (std::uint8_t column = 0; column < static_cast<std::uint8_t>(kGlyphColumns); ++column) {
                        if ((rows[row] & static_cast<std::uint8_t>(1u << (4u - column))) != 0) {
                            appendTextQuad(vertices, cursorX + column * pixel, baseY + row * pixel, pixel, width,
                                height, text.color);
                        }
                    }
                }
                cursorX += kGlyphAdvanceColumns * pixel;
                if (cursorX >= maxX || cursorX >= width - 8.0f) break;
            }
        }

        [[nodiscard]] bool projectAnchor(const sdk::WorldTextV1& text, const DirectX::XMFLOAT4X4& composite,
            const DirectX::XMFLOAT4& origin, std::uint32_t eye, float width, float height, float& outX,
            float& outY) noexcept
        {
            DirectX::XMFLOAT4 clip{};
            const auto world = DirectX::XMVectorSet(text.anchor[0] - origin.x, text.anchor[1] - origin.y,
                text.anchor[2] - origin.z, 1.0f);
            DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(world, DirectX::XMLoadFloat4x4(&composite)));
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.w) ||
                std::fabs(clip.w) < 1.0e-5f) {
                return false;
            }
            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            if (clip.w < 0.0f || ndcX < -2.0f || ndcX > 2.0f || ndcY < -2.0f || ndcY > 2.0f) return false;
            const float halfWidth = width * 0.5f;
            outX = (eye == 0 ? 0.0f : halfWidth) + (ndcX * 0.5f + 0.5f) * halfWidth;
            outY = (-ndcY * 0.5f + 0.5f) * height;
            return true;
        }

        void drawTexts(Resources& state, ID3D11DeviceContext* context, const world_draw::RenderBatch& batch,
            const StereoProjection::Snapshot& projection, float width, float height)
        {
            if (batch.texts.empty() || width <= 0.0f || height <= 0.0f) return;
            state.vertexScratch.clear();
            const float halfWidth = width * 0.5f;
            for (const auto& text : batch.texts) {
                std::size_t length = 0;
                while (length < sizeof(text.text) && text.text[length] != '\0') ++length;
                const float approximateWidth = static_cast<float>(length) * 6.0f * (std::max)(1.0f, text.pixelSize);
                for (std::uint32_t eye = 0; eye < 2; ++eye) {
                    float projectedX = 0.0f;
                    float projectedY = 0.0f;
                    if (!projectAnchor(text, projection.composite[eye], projection.origin[eye], eye, width, height,
                            projectedX, projectedY)) {
                        continue;
                    }
                    const float eyeMinX = eye == 0 ? 0.0f : halfWidth;
                    const float eyeMaxX = eyeMinX + halfWidth;
                    const float minX = eyeMinX + 24.0f;
                    const float maxX = (std::max)(minX, eyeMaxX - approximateWidth - 24.0f);
                    const float baseX = std::clamp(projectedX + text.offsetPixels[0], minX, maxX);
                    const float baseY = std::clamp(projectedY + text.offsetPixels[1], 24.0f,
                        (std::max)(24.0f, height - 64.0f));
                    appendGlyphs(state.vertexScratch, text, baseX, baseY, eyeMaxX - 8.0f, width, height);
                }
            }
            if (state.vertexScratch.empty()) return;
            context->IASetInputLayout(state.coloredLayout.Get());
            context->VSSetShader(state.projectedTextVertex.Get(), nullptr, 0);
            context->PSSetShader(state.pixel.Get(), nullptr, 0);
            context->RSSetState(state.solid.Get());
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            drawColoredVertices(state, context, state.textVertices.Get(), kTextVertexCapacity, 6, 1);
        }
    }

    void Draw(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* target,
        const D3D11_TEXTURE2D_DESC& targetDescription, const StereoProjection::Snapshot& projection,
        const world_draw::RenderBatch& batch, std::uint64_t frame) noexcept
    {
        if (!device || !context || !target || batch.empty() || targetDescription.Width == 0 ||
            targetDescription.Height == 0) {
            return;
        }
        auto& state = resources();
        if (!initialize(state, device)) return;
        try {
            prepareMeshes(state, device, batch, frame);

            context->SetPredication(nullptr, FALSE);
            context->OMSetRenderTargets(1, &target, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(targetDescription.Width);
            viewport.Height = static_cast<float>(targetDescription.Height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            constexpr FLOAT blendFactor[4] = {};
            context->OMSetBlendState(state.blend.Get(), blendFactor, 0xFFFFFFFFu);
            context->OMSetDepthStencilState(state.depthOff.Get(), 0);
            if (!uploadCamera(state, context, projection)) return;

            drawMeshes(state, context, batch);
            drawSpheres(state, context, batch);
            drawLines(state, context, batch);
            drawTexts(state, context, batch, projection, static_cast<float>(targetDescription.Width),
                static_cast<float>(targetDescription.Height));
            evictIdleMeshes(state, frame);
        } catch (...) {
            reportOnce(g_drawFailureReported, "drawing aborted by an exception; frame skipped");
        }
    }

    void ReleaseDeviceResources() noexcept
    {
        resources() = {};
    }
}
