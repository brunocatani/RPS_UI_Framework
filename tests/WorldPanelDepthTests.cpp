#include "render/WorldPanelDepth.h"

#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace depth = rpsui::render::world_panel_depth;

namespace
{
    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    constexpr char shader[] = R"(
        cbuffer Panel : register(b0) { float4 color; float panelDepth; float3 padding; };
        float4 vs(uint id : SV_VertexID) : SV_POSITION {
            float2 positions[3] = {float2(-1,-1), float2(-1,3), float2(3,-1)};
            return float4(positions[id], panelDepth, 1);
        }
        float4 ps() : SV_TARGET { return color; }
    )";

    struct Gpu
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11Texture2D> color, staging, sceneDepth;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11DepthStencilView> dsv, readOnlyDsv;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        ComPtr<ID3D11Buffer> constants;
        ComPtr<ID3D11RasterizerState> rasterizer;
        ComPtr<ID3D11DepthStencilState> panelState;

        Gpu()
        {
            require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf())),
                "WARP device creation failed");
            D3D11_TEXTURE2D_DESC texture{};
            texture.Width = texture.Height = 4;
            texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
            texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texture.BindFlags = D3D11_BIND_RENDER_TARGET;
            require(SUCCEEDED(device->CreateTexture2D(&texture, nullptr, color.GetAddressOf())), "color creation failed");
            require(SUCCEEDED(device->CreateRenderTargetView(color.Get(), nullptr, rtv.GetAddressOf())), "RTV creation failed");
            texture.BindFlags = 0;
            texture.Usage = D3D11_USAGE_STAGING;
            texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(SUCCEEDED(device->CreateTexture2D(&texture, nullptr, staging.GetAddressOf())), "staging creation failed");
            texture.Usage = D3D11_USAGE_DEFAULT;
            texture.CPUAccessFlags = 0;
            texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            texture.Format = DXGI_FORMAT_R24G8_TYPELESS;
            require(SUCCEEDED(device->CreateTexture2D(&texture, nullptr, sceneDepth.GetAddressOf())), "depth creation failed");
            D3D11_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            require(SUCCEEDED(device->CreateDepthStencilView(sceneDepth.Get(), &view, dsv.GetAddressOf())), "DSV creation failed");
            view.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
            require(SUCCEEDED(device->CreateDepthStencilView(sceneDepth.Get(), &view, readOnlyDsv.GetAddressOf())), "read-only DSV creation failed");
            ComPtr<ID3DBlob> vertexCode, pixelCode;
            require(SUCCEEDED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0,
                vertexCode.GetAddressOf(), nullptr)), "VS compilation failed");
            require(SUCCEEDED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0,
                pixelCode.GetAddressOf(), nullptr)), "PS compilation failed");
            require(SUCCEEDED(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(),
                nullptr, vs.GetAddressOf())), "VS creation failed");
            require(SUCCEEDED(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(),
                nullptr, ps.GetAddressOf())), "PS creation failed");
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = 32;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            require(SUCCEEDED(device->CreateBuffer(&buffer, nullptr, constants.GetAddressOf())), "constant buffer creation failed");
            D3D11_RASTERIZER_DESC raster{};
            raster.FillMode = D3D11_FILL_SOLID;
            raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = TRUE;
            require(SUCCEEDED(device->CreateRasterizerState(&raster, rasterizer.GetAddressOf())), "rasterizer creation failed");
            const auto description = depth::readOnlyDescription();
            require(SUCCEEDED(device->CreateDepthStencilState(&description, panelState.GetAddressOf())), "panel state creation failed");
            const D3D11_VIEWPORT viewport{0, 0, 4, 4, depth::kMinDepth, depth::kMaxDepth};
            context->RSSetViewports(1, &viewport);
            context->RSSetState(rasterizer.Get());
            context->VSSetShader(vs.Get(), nullptr, 0);
            context->PSSetShader(ps.Get(), nullptr, 0);
            auto* cb = constants.Get();
            context->VSSetConstantBuffers(0, 1, &cb);
            context->PSSetConstantBuffers(0, 1, &cb);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        void clear(float value)
        {
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, value, 0x7f);
            clearColor();
        }

        void clearColor()
        {
            constexpr float red[]{1, 0, 0, 1};
            context->ClearRenderTargetView(rtv.Get(), red);
        }

        bool draw(float value, ID3D11DepthStencilState* state, bool readOnlyView = true)
        {
            const std::array<float, 8> values{0, 1, 0, 1, value, 0, 0, 0};
            context->UpdateSubresource(constants.Get(), 0, nullptr, values.data(), 0, 0);
            auto* target = rtv.Get();
            context->OMSetRenderTargets(1, &target, readOnlyView ? readOnlyDsv.Get() : dsv.Get());
            context->OMSetDepthStencilState(state, 0);
            context->Draw(3, 0);
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->CopyResource(staging.Get(), color.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "pixel readback failed");
            const auto* pixel = static_cast<const std::uint8_t*>(mapped.pData) + 2 * mapped.RowPitch + 2 * 4;
            const bool green = pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0;
            context->Unmap(staging.Get(), 0);
            return green;
        }
    };
}

int main()
{
    try {
        Gpu gpu;
        // Values measured along the eye-to-panel ray in the failing session.
        constexpr float hand = 0.897907f, panel = 0.9489605f, scenery = 0.9744874f;
        for (const auto comparison : {D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_LESS}) {
            auto description = depth::readOnlyDescription();
            description.DepthFunc = comparison;
            ComPtr<ID3D11DepthStencilState> capturedPass;
            require(SUCCEEDED(gpu.device->CreateDepthStencilState(&description, capturedPass.GetAddressOf())), "source pass state creation failed");
            if (comparison == D3D11_COMPARISON_GREATER_EQUAL) {
                gpu.clear(scenery);
                require(!gpu.draw(panel, capturedPass.Get()), "old comparison must reproduce the hidden panel");
                gpu.clear(hand);
                require(gpu.draw(panel, capturedPass.Get()), "old comparison must reproduce a hand revealing the panel");
            }
            for (const float background : {scenery, 0.999f, 1.0f}) {
                gpu.clear(background);
                gpu.context->OMSetDepthStencilState(capturedPass.Get(), 0x7f);
                require(gpu.draw(panel, gpu.panelState.Get()), "panel must remain visible ahead of scenery regardless of the preceding pass");
            }
            gpu.clear(hand);
            gpu.context->OMSetDepthStencilState(capturedPass.Get(), 0x7f);
            require(!gpu.draw(panel, gpu.panelState.Get()), "nearer geometry must hide the panel");
        }
        // Exercise the state with a writable view as well: UI drawing must not
        // alter scene depth even independently of the read-only DSV protection.
        gpu.clear(scenery);
        require(gpu.draw(panel, gpu.panelState.Get(), false), "first panel must draw");
        gpu.clearColor();
        require(gpu.draw(0.96f, gpu.panelState.Get(), false), "panel drawing must preserve scene depth for the next panel");
        std::cout << "World-panel GPU occlusion regression passed (WARP).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
