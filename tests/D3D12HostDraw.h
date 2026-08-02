#pragma once

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstring>

namespace henia::test {

class D3D12HostDraw final {
public:
    [[nodiscard]] bool initialize(
        ID3D12Device& device,
        DXGI_FORMAT renderTargetFormat,
        std::uint32_t sampleCount = 1) noexcept {
        constexpr const char* source = R"hlsl(
cbuffer Marker : register(b0) {
    float4 bounds;
    float4 color;
    float2 viewportSize;
};
static const float2 corners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};
struct Output { float4 position : SV_Position; };
Output vertexMain(uint vertexId : SV_VertexID) {
    Output output;
    float2 pixel = lerp(bounds.xy, bounds.zw, corners[vertexId]);
    float2 normalized = pixel / viewportSize;
    output.position = float4(
        normalized.x * 2.0 - 1.0,
        1.0 - normalized.y * 2.0,
        0.0,
        1.0);
    return output;
}
float4 pixelMain() : SV_Target { return color; }
)hlsl";
        Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;
        constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
        if (FAILED(D3DCompile(
                source, std::strlen(source), "HostDraw", nullptr, nullptr,
                "vertexMain", "vs_5_0", flags, 0, &vertexShader, nullptr))
            || FAILED(D3DCompile(
                source, std::strlen(source), "HostDraw", nullptr, nullptr,
                "pixelMain", "ps_5_0", flags, 0, &pixelShader, nullptr))) {
            return false;
        }

        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.ShaderRegister = 0;
        parameter.Constants.Num32BitValues = 10;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = 1;
        rootDescription.pParameters = &parameter;
        rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> serialized;
        if (FAILED(D3D12SerializeRootSignature(
                &rootDescription,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &serialized,
                nullptr))
            || FAILED(device.CreateRootSignature(
                0,
                serialized->GetBufferPointer(),
                serialized->GetBufferSize(),
                IID_PPV_ARGS(&mRootSignature)))) {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = mRootSignature.Get();
        pipeline.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        pipeline.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        D3D12_RENDER_TARGET_BLEND_DESC& blend = pipeline.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline.SampleMask = UINT_MAX;
        pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline.RasterizerState.DepthClipEnable = TRUE;
        pipeline.DepthStencilState.DepthEnable = FALSE;
        pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        pipeline.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        pipeline.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        pipeline.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline.DepthStencilState.BackFace = pipeline.DepthStencilState.FrontFace;
        pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline.NumRenderTargets = 1;
        pipeline.RTVFormats[0] = renderTargetFormat;
        pipeline.SampleDesc.Count = sampleCount;
        if (FAILED(device.CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&mPipeline)))) {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        return SUCCEEDED(device.CreateDescriptorHeap(&heap, IID_PPV_ARGS(&mHeap)));
    }

    void record(
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        std::array<float, 4> bounds,
        std::array<float, 4> color) const noexcept {
        ID3D12DescriptorHeap* heaps[]{mHeap.Get()};
        commandList.SetDescriptorHeaps(1, heaps);
        commandList.SetPipelineState(mPipeline.Get());
        commandList.SetGraphicsRootSignature(mRootSignature.Get());
        const std::array constants{
            bounds[0], bounds[1], bounds[2], bounds[3],
            color[0], color[1], color[2], color[3],
            static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
        };
        commandList.SetGraphicsRoot32BitConstants(
            0,
            static_cast<UINT>(constants.size()),
            constants.data(),
            0);
        commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList.IASetVertexBuffers(0, 0, nullptr);
        const D3D12_VIEWPORT viewport{
            0.0F, 0.0F,
            static_cast<float>(viewportWidth), static_cast<float>(viewportHeight),
            0.0F, 1.0F,
        };
        const D3D12_RECT scissor{
            0, 0,
            static_cast<LONG>(viewportWidth),
            static_cast<LONG>(viewportHeight),
        };
        commandList.RSSetViewports(1, &viewport);
        commandList.RSSetScissorRects(1, &scissor);
        commandList.DrawInstanced(6, 1, 0, 0);
    }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipeline;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
};

} // namespace henia::test
