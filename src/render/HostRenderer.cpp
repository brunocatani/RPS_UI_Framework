#include "PCH.h"

#include "FrameworkRuntime.h"
#include "Logger.h"
#include "render/D3D11StateGuard.h"
#include "render/HostRenderer.h"
#include "render/SceneDepthCapture.h"
#include "render/StereoProjection.h"

#include <DirectXTK/CommonStates.h>
#include <DirectXTK/Effects.h>
#include <DirectXTK/PrimitiveBatch.h>
#include <DirectXTK/VertexTypes.h>
#include <d3d11_1.h>

namespace rpsui::render
{
    namespace
    {
        using Vertex = DirectX::VertexPositionColorTexture;

        struct PanelGpu
        {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderResource;
            std::uint32_t width{ 0 };
            std::uint32_t height{ 0 };
            std::uint64_t lastSeenFrame{ 0 };
        };

        struct Resources
        {
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
            Microsoft::WRL::ComPtr<ID3D11Device1> device1;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
            Microsoft::WRL::ComPtr<ID3DDeviceContextState> isolatedContextState;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> submittedTexture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> submittedRenderTarget;

            std::unique_ptr<DirectX::CommonStates> commonStates;
            std::unique_ptr<DirectX::BasicEffect> effect;
            std::unique_ptr<DirectX::PrimitiveBatch<Vertex>> batch;
            Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
            std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilState>, 9>
                depthReadStates{};
            std::array<bool, 9> depthStateFailed{};
            std::unordered_map<std::uint64_t, PanelGpu> panelGpu;

            bool initialized{ false };
            bool initializationFailed{ false };
            bool depthUnavailable{ false };
            std::uint64_t frameSequence{ 0 };
            std::uint64_t deviceGeneration{ 0 };
            std::chrono::steady_clock::time_point lastFrame{};
            std::chrono::steady_clock::time_point nextDepthWarning{};
            std::mutex renderMutex;
        };

        [[nodiscard]] Resources& resources() noexcept
        {
            static auto* value = new Resources();
            return *value;
        }

        void releaseDeviceResources(Resources& state) noexcept
        {
            state.panelGpu.clear();
            state.batch.reset();
            state.effect.reset();
            state.commonStates.reset();
            state.inputLayout.Reset();
            state.depthReadStates = {};
            state.depthStateFailed = {};
            state.submittedRenderTarget.Reset();
            state.submittedTexture.Reset();
            state.isolatedContextState.Reset();
            state.context1.Reset();
            state.device1.Reset();
            state.context.Reset();
            state.device.Reset();
            state.initialized = false;
        }

        [[nodiscard]] bool initialize(
            Resources& state,
            ID3D11Texture2D* submittedTexture) noexcept
        {
            if (!submittedTexture || state.initializationFailed) {
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D11Device> device;
            submittedTexture->GetDevice(device.GetAddressOf());
            if (!device) {
                return false;
            }
            if (state.initialized && state.device.Get() == device.Get()) {
                return true;
            }
            if (state.initialized) {
                releaseDeviceResources(state);
            }

            try {
                state.device = device;
                device->GetImmediateContext(state.context.GetAddressOf());
                if (!state.context) {
                    throw std::runtime_error("immediate D3D11 context unavailable");
                }

                state.commonStates =
                    std::make_unique<DirectX::CommonStates>(device.Get());
                state.effect =
                    std::make_unique<DirectX::BasicEffect>(device.Get());
                state.effect->SetTextureEnabled(true);
                state.effect->SetVertexColorEnabled(true);
                state.effect->SetLightingEnabled(false);
                state.batch =
                    std::make_unique<DirectX::PrimitiveBatch<Vertex>>(
                        state.context.Get());

                const void* bytecode = nullptr;
                std::size_t bytecodeLength = 0;
                state.effect->GetVertexShaderBytecode(
                    &bytecode,
                    &bytecodeLength);
                if (!bytecode || bytecodeLength == 0 ||
                    FAILED(device->CreateInputLayout(
                        Vertex::InputElements,
                        Vertex::InputElementCount,
                        bytecode,
                        bytecodeLength,
                        state.inputLayout.GetAddressOf()))) {
                    throw std::runtime_error(
                        "world-panel input layout unavailable");
                }

                (void)state.device.As(&state.device1);
                (void)state.context.As(&state.context1);
                if (state.device1 && state.context1) {
                    constexpr std::array featureLevels{
                        D3D_FEATURE_LEVEL_11_1,
                        D3D_FEATURE_LEVEL_11_0,
                        D3D_FEATURE_LEVEL_10_1,
                        D3D_FEATURE_LEVEL_10_0,
                    };
                    D3D_FEATURE_LEVEL selected{};
                    if (FAILED(state.device1->CreateDeviceContextState(
                            0,
                            featureLevels.data(),
                            static_cast<UINT>(featureLevels.size()),
                            D3D11_SDK_VERSION,
                            __uuidof(ID3D11Device),
                            &selected,
                            state.isolatedContextState.GetAddressOf()))) {
                        state.isolatedContextState.Reset();
                        log::warn(
                            "RPS UI Framework will use bounded D3D11 state restoration");
                    }
                }

                ++state.deviceGeneration;
                state.frameSequence = 0;
                state.lastFrame = std::chrono::steady_clock::now();
                state.initialized = true;
                log::info(
                    "RPS UI Framework D3D11 compositor initialized (device generation {})",
                    state.deviceGeneration);
                return true;
            } catch (const std::exception& error) {
                log::critical(
                    "RPS UI Framework renderer initialization failed: {}",
                    error.what());
            } catch (...) {
                log::critical(
                    "RPS UI Framework renderer initialization failed");
            }
            releaseDeviceResources(state);
            state.initializationFailed = true;
            return false;
        }

        [[nodiscard]] DXGI_FORMAT renderTargetFormat(
            DXGI_FORMAT format) noexcept
        {
            switch (format) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_TYPELESS:
                return DXGI_FORMAT_B8G8R8X8_UNORM;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            default:
                return format;
            }
        }

        [[nodiscard]] bool ensureSubmittedRenderTarget(
            Resources& state,
            ID3D11Texture2D* texture,
            const D3D11_TEXTURE2D_DESC& description) noexcept
        {
            if (description.ArraySize != 1 ||
                description.MipLevels == 0 ||
                (description.BindFlags & D3D11_BIND_RENDER_TARGET) == 0) {
                return false;
            }
            if (state.submittedTexture.Get() == texture &&
                state.submittedRenderTarget) {
                return true;
            }
            state.submittedRenderTarget.Reset();
            state.submittedTexture.Reset();

            D3D11_RENDER_TARGET_VIEW_DESC view{};
            view.Format = renderTargetFormat(description.Format);
            view.ViewDimension =
                description.SampleDesc.Count > 1 ?
                D3D11_RTV_DIMENSION_TEXTURE2DMS :
                D3D11_RTV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipSlice = 0;
            if (FAILED(state.device->CreateRenderTargetView(
                    texture,
                    &view,
                    state.submittedRenderTarget.GetAddressOf()))) {
                return false;
            }
            state.submittedTexture = texture;
            return true;
        }

        [[nodiscard]] PanelGpu* ensurePanelGpu(
            Resources& state,
            const RenderPanelSnapshot& panel) noexcept
        {
            auto& gpu = state.panelGpu[panel.panelHandle];
            if (gpu.texture &&
                gpu.width == panel.pixelWidth &&
                gpu.height == panel.pixelHeight) {
                gpu.lastSeenFrame = state.frameSequence;
                return std::addressof(gpu);
            }

            gpu = {};
            D3D11_TEXTURE2D_DESC description{};
            description.Width = panel.pixelWidth;
            description.Height = panel.pixelHeight;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(state.device->CreateTexture2D(
                    &description,
                    nullptr,
                    gpu.texture.GetAddressOf())) ||
                FAILED(state.device->CreateRenderTargetView(
                    gpu.texture.Get(),
                    nullptr,
                    gpu.renderTarget.GetAddressOf())) ||
                FAILED(state.device->CreateShaderResourceView(
                    gpu.texture.Get(),
                    nullptr,
                    gpu.shaderResource.GetAddressOf()))) {
                state.panelGpu.erase(panel.panelHandle);
                return nullptr;
            }
            gpu.width = panel.pixelWidth;
            gpu.height = panel.pixelHeight;
            gpu.lastSeenFrame = state.frameSequence;
            return std::addressof(gpu);
        }

        class ScopedPresentationState final
        {
        public:
            explicit ScopedPresentationState(Resources& state) noexcept :
                state_(state)
            {
                if (state_.context1 && state_.isolatedContextState) {
                    state_.context1->SwapDeviceContextState(
                        state_.isolatedContextState.Get(),
                        previous_.GetAddressOf());
                    swapped_ = previous_ != nullptr;
                }
                if (!swapped_ && state_.context) {
                    fallback_.emplace(state_.context.Get());
                }
            }

            ~ScopedPresentationState() noexcept
            {
                fallback_.reset();
                if (!swapped_ ||
                    !state_.context1 ||
                    !previous_) {
                    return;
                }
                Microsoft::WRL::ComPtr<ID3DDeviceContextState> uiState;
                state_.context1->SwapDeviceContextState(
                    previous_.Get(),
                    uiState.GetAddressOf());
                if (uiState) {
                    state_.isolatedContextState = std::move(uiState);
                }
            }

            ScopedPresentationState(const ScopedPresentationState&) = delete;
            ScopedPresentationState& operator=(
                const ScopedPresentationState&) = delete;

        private:
            Resources& state_;
            Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous_;
            std::optional<ScopedD3D11State> fallback_;
            bool swapped_{ false };
        };

        [[nodiscard]] ID3D11DepthStencilState* depthReadState(
            Resources& state,
            D3D11_COMPARISON_FUNC comparison) noexcept
        {
            const auto index = static_cast<std::size_t>(comparison);
            if (index >= state.depthReadStates.size() ||
                comparison < D3D11_COMPARISON_NEVER ||
                comparison > D3D11_COMPARISON_ALWAYS) {
                return nullptr;
            }
            if (state.depthReadStates[index]) {
                return state.depthReadStates[index].Get();
            }
            if (state.depthStateFailed[index]) {
                return nullptr;
            }
            D3D11_DEPTH_STENCIL_DESC description{};
            description.DepthEnable = TRUE;
            description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            description.DepthFunc = comparison;
            description.StencilEnable = FALSE;
            if (FAILED(state.device->CreateDepthStencilState(
                    &description,
                    state.depthReadStates[index].GetAddressOf()))) {
                state.depthStateFailed[index] = true;
                return nullptr;
            }
            return state.depthReadStates[index].Get();
        }

        [[nodiscard]] DirectX::XMFLOAT3 addScaled(
            const DirectX::XMFLOAT3& origin,
            const DirectX::XMFLOAT3& first,
            float firstScale,
            const DirectX::XMFLOAT3& second,
            float secondScale) noexcept
        {
            return {
                origin.x + first.x * firstScale +
                    second.x * secondScale,
                origin.y + first.y * firstScale +
                    second.y * secondScale,
                origin.z + first.z * firstScale +
                    second.z * secondScale,
            };
        }

        [[nodiscard]] Vertex makeVertex(
            const DirectX::XMFLOAT3& position,
            float u,
            float v) noexcept
        {
            return {
                position,
                DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f },
                DirectX::XMFLOAT2{ u, v },
            };
        }

        [[nodiscard]] bool drawWorldPanel(
            Resources& state,
            const RenderPanelSnapshot& panel,
            ID3D11ShaderResourceView* shaderResource,
            const StereoProjection::Snapshot& projection,
            const SceneDepthCapture::FrameDepth& depth,
            const D3D11_TEXTURE2D_DESC& outputDescription) noexcept
        {
            auto* depthState =
                depthReadState(state, depth.comparison);
            if (!depthState ||
                !shaderResource ||
                outputDescription.Width < 2 ||
                (outputDescription.Width % 2) != 0) {
                return false;
            }

            const DirectX::XMFLOAT3 center{
                panel.pose.center[0],
                panel.pose.center[1],
                panel.pose.center[2],
            };
            const DirectX::XMFLOAT3 right{
                panel.pose.right[0],
                panel.pose.right[1],
                panel.pose.right[2],
            };
            const DirectX::XMFLOAT3 up{
                panel.pose.up[0],
                panel.pose.up[1],
                panel.pose.up[2],
            };
            const float halfWidth =
                panel.pose.physicalWidth * 0.5f;
            const float halfHeight =
                panel.pose.physicalHeight * 0.5f;
            const std::array vertices{
                makeVertex(
                    addScaled(center, right, -halfWidth, up, halfHeight),
                    0.0f,
                    0.0f),
                makeVertex(
                    addScaled(center, right, halfWidth, up, halfHeight),
                    1.0f,
                    0.0f),
                makeVertex(
                    addScaled(center, right, halfWidth, up, -halfHeight),
                    1.0f,
                    1.0f),
                makeVertex(
                    addScaled(center, right, -halfWidth, up, -halfHeight),
                    0.0f,
                    1.0f),
            };

            state.context->SetPredication(nullptr, FALSE);
            state.context->HSSetShader(nullptr, nullptr, 0);
            state.context->DSSetShader(nullptr, nullptr, 0);
            state.context->GSSetShader(nullptr, nullptr, 0);
            state.context->IASetInputLayout(state.inputLayout.Get());
            state.context->OMSetBlendState(
                state.commonStates->AlphaBlend(),
                nullptr,
                0xFFFFFFFFu);
            state.context->RSSetState(state.commonStates->CullNone());
            auto* sampler = state.commonStates->LinearClamp();
            state.context->PSSetSamplers(0, 1, &sampler);

            auto* output = state.submittedRenderTarget.Get();
            state.context->OMSetRenderTargets(
                1,
                &output,
                depth.view.Get());
            state.effect->SetTexture(shaderResource);
            state.effect->SetView(DirectX::XMMatrixIdentity());

            const float eyeWidth =
                static_cast<float>(outputDescription.Width / 2);
            for (std::size_t eye = 0; eye < 2; ++eye) {
                D3D11_VIEWPORT viewport{};
                viewport.TopLeftX = eye == 0 ? 0.0f : eyeWidth;
                viewport.Width = eyeWidth;
                viewport.Height =
                    static_cast<float>(outputDescription.Height);
                viewport.MinDepth = 0.0f;
                viewport.MaxDepth = 1.0f;
                state.context->RSSetViewports(1, &viewport);
                state.context->OMSetDepthStencilState(depthState, 0);

                const auto& origin = projection.origin[eye];
                state.effect->SetWorld(
                    DirectX::XMMatrixTranslation(
                        -origin.x,
                        -origin.y,
                        -origin.z));
                state.effect->SetProjection(
                    DirectX::XMLoadFloat4x4(
                        &projection.composite[eye]));
                state.effect->Apply(state.context.Get());
                state.batch->Begin();
                state.batch->DrawQuad(
                    vertices[0],
                    vertices[1],
                    vertices[2],
                    vertices[3]);
                state.batch->End();
            }
            state.effect->SetTexture(nullptr);
            return true;
        }

        void reportDepthState(
            Resources& state,
            bool available) noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            if (available) {
                if (state.depthUnavailable) {
                    state.depthUnavailable = false;
                    log::info(
                        "RPS UI Framework scene-depth composition recovered");
                }
                return;
            }
            state.depthUnavailable = true;
            if (now >= state.nextDepthWarning) {
                state.nextDepthWarning =
                    now + std::chrono::seconds(5);
                log::warn(
                    "RPS UI panels withheld because matching same-frame "
                    "scene depth is unavailable");
            }
        }

        [[nodiscard]] bool renderConsumerPanel(
            Resources& state,
            const RenderPanelSnapshot& panel,
            PanelGpu& gpu,
            float deltaSeconds) noexcept
        {
            auto* renderTarget = gpu.renderTarget.Get();
            state.context->OMSetRenderTargets(
                1,
                &renderTarget,
                nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(panel.pixelWidth);
            viewport.Height = static_cast<float>(panel.pixelHeight);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            state.context->RSSetViewports(1, &viewport);
            std::array clear{
                0.035f,
                0.043f,
                0.055f,
                1.0f,
            };
            if (sdk::hasPanelFlag(panel.flags, sdk::PanelFlagV1::Transparent)) clear.fill(0.0f);
            state.context->ClearRenderTargetView(
                renderTarget,
                clear.data());

            rpsui::sdk::PanelRenderFrameV1 frame{};
            frame.panelHandle = panel.panelHandle;
            frame.frameSequence = state.frameSequence;
            frame.deviceGeneration = state.deviceGeneration;
            frame.d3dDevice = state.device.Get();
            frame.d3dContext = state.context.Get();
            frame.renderTargetView = renderTarget;
            frame.pixelWidth = panel.pixelWidth;
            frame.pixelHeight = panel.pixelHeight;
            frame.deltaSeconds = deltaSeconds;
            frame.pointerPixelX = panel.pointerPixelX;
            frame.pointerPixelY = panel.pointerPixelY;
            frame.scrollAxisX = panel.scrollAxisX;
            frame.scrollAxisY = panel.scrollAxisY;
            frame.physicalWidth = panel.pose.physicalWidth;
            frame.physicalHeight = panel.pose.physicalHeight;
            frame.pointerHand = panel.pointerHand;
            frame.hoveredResizeHandle =
                panel.hoveredResizeHandle;
            frame.activeResizeHandle =
                panel.activeResizeHandle;
            frame.pointerValid = panel.pointerValid ? 1 : 0;
            frame.primaryDown = panel.primaryDown ? 1 : 0;
            frame.backDown = panel.backDown ? 1 : 0;

            try {
                panel.renderCallback(&frame, panel.userData);
                return true;
            } catch (...) {
                log::error(
                    "UI consumer callback for panel {} threw across its boundary",
                    panel.panelHandle);
                return false;
            }
        }
    }

    void RenderSubmittedTexture(ID3D11Texture2D* texture) noexcept
    {
        if (!texture) {
            return;
        }
        SceneDepthCapture::SetSubmittedTarget(texture);
        auto panels =
            FrameworkRuntime::get().snapshotRenderPanels();
        if (panels.empty()) {
            return;
        }

        auto& state = resources();
        std::unique_lock renderLock(
            state.renderMutex,
            std::try_to_lock);
        if (!renderLock.owns_lock() ||
            !initialize(state, texture)) {
            return;
        }

        D3D11_TEXTURE2D_DESC outputDescription{};
        texture->GetDesc(&outputDescription);
        if (outputDescription.Width == 0 ||
            outputDescription.Height == 0 ||
            !ensureSubmittedRenderTarget(
                state,
                texture,
                outputDescription)) {
            return;
        }

        const auto depth =
            SceneDepthCapture::AcquireForSubmittedTarget(
                texture,
                outputDescription);
        reportDepthState(state, depth.IsValid());
        if (!depth.IsValid()) {
            return;
        }

        StereoProjection::Snapshot projection{};
        if (!StereoProjection::CaptureSnapshot(projection)) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::clamp(
            std::chrono::duration<float>(
                now - state.lastFrame).count(),
            1.0f / 240.0f,
            0.1f);
        state.lastFrame = now;
        ++state.frameSequence;

        ScopedPresentationState presentationState(state);
        for (const auto& panel : panels) {
            auto* gpu = ensurePanelGpu(state, panel);
            if (!gpu) {
                continue;
            }
            if (!renderConsumerPanel(
                    state,
                    panel,
                    *gpu,
                    deltaSeconds)) {
                continue;
            }
            (void)drawWorldPanel(
                state,
                panel,
                gpu->shaderResource.Get(),
                projection,
                depth,
                outputDescription);
        }

        for (auto it = state.panelGpu.begin();
             it != state.panelGpu.end();) {
            if (it->second.lastSeenFrame != state.frameSequence) {
                it = state.panelGpu.erase(it);
            } else {
                ++it;
            }
        }
    }

    void Shutdown() noexcept
    {
        auto& state = resources();
        std::scoped_lock lock(state.renderMutex);
        releaseDeviceResources(state);
        SceneDepthCapture::Reset();
    }
}
