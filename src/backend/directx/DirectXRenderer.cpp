#include "henia/ui/backend/directx/DirectXRenderer.h"

#include "henia/CheckedArithmetic.h"
#include "henia/ui/Validation.h"

#include "../FixedError.h"
#include "UiShaders.generated.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace henia::ui {
namespace {

using Microsoft::WRL::ComPtr;

struct D3D11Texture final {
    ComPtr<ID3D11ShaderResourceView> view;
    std::uint64_t handle = 0;
    std::uint64_t revision = 0;
    TextureAlphaMode alphaMode = TextureAlphaMode::Straight;
};

struct alignas(16) UiFrameConstants final {
    std::array<float, 2> viewport{};
    std::array<float, 2> scale{};
    std::array<float, 2> translation{};
    float minimumAntialiasWidth = 0.0F;
    float padding = 0.0F;
};
static_assert(sizeof(UiFrameConstants) == 32);

struct alignas(16) UiTextureConstants final {
    std::array<std::uint32_t, DrawBatch::kTextureCapacity> alphaModes{};
};
static_assert(sizeof(UiTextureConstants) == 32);

[[nodiscard]] bool compileUiShader(
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& output,
    henia::detail::FixedError& error,
    const D3D_SHADER_MACRO* macros = nullptr) noexcept {
    ComPtr<ID3DBlob> errors;
    const HRESULT status = D3DCompile(
        henia::backend::d3d12::generated::ui::kSource,
        sizeof(henia::backend::d3d12::generated::ui::kSource) - 1U,
        "HeniaUI.DirectX.D3D11.UI",
        macros,
        nullptr,
        entry,
        target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &output,
        &errors);
    if (SUCCEEDED(status)) return true;
    if (errors != nullptr && errors->GetBufferPointer() != nullptr) {
        error.assign(
            static_cast<const char*>(errors->GetBufferPointer()),
            errors->GetBufferSize());
    } else {
        error = "D3D11 UI shader compilation failed";
    }
    return false;
}

[[nodiscard]] bool sameComIdentity(IUnknown& left, IUnknown& right) noexcept {
    ComPtr<IUnknown> leftIdentity;
    ComPtr<IUnknown> rightIdentity;
    return SUCCEEDED(left.QueryInterface(IID_PPV_ARGS(&leftIdentity)))
        && SUCCEEDED(right.QueryInterface(IID_PPV_ARGS(&rightIdentity)))
        && leftIdentity.Get() == rightIdentity.Get();
}

[[nodiscard]] D3D11_BLEND_DESC blendDescription(bool additive) noexcept {
    D3D11_BLEND_DESC description{};
    D3D11_RENDER_TARGET_BLEND_DESC& target = description.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_ONE;
    target.DestBlend = additive ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = additive ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    return description;
}

[[nodiscard]] bool isSrgbFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
        || format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

class D3D11UiRenderer final {
public:
    [[nodiscard]] bool initialize(
        ID3D11Device& requestedDevice,
        ID3D11DeviceContext& requestedContext,
        DXGI_FORMAT requestedFormat,
        D3D11RendererConfiguration requested) noexcept {
        if (ready) {
            if (!sameComIdentity(*device.Get(), requestedDevice)
                || !sameComIdentity(*context.Get(), requestedContext)
                || requestedFormat != renderTargetFormat
                || requested.instanceCapacity != configuration.instanceCapacity
                || requested.textureCapacity != configuration.textureCapacity
                || requested.targetColorSpace != configuration.targetColorSpace
                || requested.sampleCount != configuration.sampleCount
                || requested.sampleQuality != configuration.sampleQuality) {
                error = "D3D11 UI renderer is already initialized with a different owner or configuration";
                return false;
            }
            error.clear();
            return true;
        }
        std::size_t instanceBytes = 0;
        if (requested.instanceCapacity == 0 || requested.textureCapacity == 0
            || requested.sampleCount == 0
            || requestedFormat == DXGI_FORMAT_UNKNOWN
            || isSrgbFormat(requestedFormat)
                != (requested.targetColorSpace == RenderTargetColorSpace::Srgb)
            || !checkedMultiply(
                requested.instanceCapacity,
                sizeof(DrawInstance),
                instanceBytes)
            || instanceBytes > std::numeric_limits<UINT>::max()) {
            error = "D3D11 UI renderer configuration is invalid";
            return false;
        }
        ComPtr<ID3D11Device> contextDevice;
        requestedContext.GetDevice(&contextDevice);
        if (contextDevice == nullptr || !sameComIdentity(requestedDevice, *contextDevice.Get())) {
            error = "D3D11 UI context belongs to a different device";
            return false;
        }
        if (requestedContext.GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            error = "D3D11 UI requires the host immediate context";
            return false;
        }
        if (requestedDevice.GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
            error = "D3D11 UI requires feature level 11_0";
            return false;
        }
        UINT formatSupport = 0;
        UINT sampleQualityLevels = 0;
        if (FAILED(requestedDevice.CheckFormatSupport(requestedFormat, &formatSupport))
            || (formatSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0
            || FAILED(requestedDevice.CheckMultisampleQualityLevels(
                requestedFormat,
                requested.sampleCount,
                &sampleQualityLevels))
            || sampleQualityLevels == 0) {
            error = "D3D11 UI render-target format/sample count is unsupported";
            return false;
        }
        if (requested.sampleQuality >= sampleQualityLevels) {
            error = "D3D11 UI sampleQuality is outside the supported range";
            return false;
        }

        device = &requestedDevice;
        context = &requestedContext;
        configuration = requested;
        renderTargetFormat = requestedFormat;
        textures.resize(configuration.textureCapacity);

        ComPtr<ID3DBlob> vertexBlob;
        ComPtr<ID3DBlob> pixelBlob;
        ComPtr<ID3DBlob> textureFreeBlob;
        constexpr D3D_SHADER_MACRO textureFreeMacros[]{
            {"HENIA_TEXTURE_FREE", "1"},
            {nullptr, nullptr},
        };
        if (!compileUiShader("vertexMain", "vs_5_0", vertexBlob, error)
            || !compileUiShader("pixelMain", "ps_5_0", pixelBlob, error)
            || !compileUiShader(
                "pixelMain",
                "ps_5_0",
                textureFreeBlob,
                error,
                textureFreeMacros)) {
            shutdown();
            return false;
        }
        if (FAILED(device->CreateVertexShader(
                vertexBlob->GetBufferPointer(),
                vertexBlob->GetBufferSize(),
                nullptr,
                &vertexShader))
            || FAILED(device->CreatePixelShader(
                pixelBlob->GetBufferPointer(),
                pixelBlob->GetBufferSize(),
                nullptr,
                &pixelShader))
            || FAILED(device->CreatePixelShader(
                textureFreeBlob->GetBufferPointer(),
                textureFreeBlob->GetBufferSize(),
                nullptr,
                &textureFreePixelShader))) {
            error = "D3D11 UI shader creation failed";
            shutdown();
            return false;
        }

        constexpr std::array inputs{
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_BOUNDS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, bounds)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_UV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, uv)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, color)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_METRICS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, radius)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_STYLE", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, static_cast<UINT>(offsetof(DrawInstance, kind)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        if (FAILED(device->CreateInputLayout(
                inputs.data(),
                static_cast<UINT>(inputs.size()),
                vertexBlob->GetBufferPointer(),
                vertexBlob->GetBufferSize(),
                &inputLayout))) {
            error = "D3D11 UI input-layout creation failed";
            shutdown();
            return false;
        }

        const D3D11_BUFFER_DESC instanceDescription{
            .ByteWidth = static_cast<UINT>(instanceBytes),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        const D3D11_BUFFER_DESC frameDescription{
            .ByteWidth = sizeof(UiFrameConstants),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        const D3D11_BUFFER_DESC textureDescription{
            .ByteWidth = sizeof(UiTextureConstants),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        if (FAILED(device->CreateBuffer(&instanceDescription, nullptr, &instanceBuffer))
            || FAILED(device->CreateBuffer(&frameDescription, nullptr, &frameConstants))
            || FAILED(device->CreateBuffer(&textureDescription, nullptr, &textureConstants))) {
            error = "D3D11 UI buffer creation failed";
            shutdown();
            return false;
        }
        const D3D11_RASTERIZER_DESC rasterizerDescription{
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
            .DepthClipEnable = TRUE,
            .ScissorEnable = TRUE,
            .MultisampleEnable = configuration.sampleCount > 1 ? TRUE : FALSE,
        };
        const D3D11_DEPTH_STENCIL_DESC depthDescription{
            .DepthEnable = FALSE,
            .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
            .DepthFunc = D3D11_COMPARISON_ALWAYS,
        };
        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        const D3D11_BLEND_DESC alphaDescription = blendDescription(false);
        const D3D11_BLEND_DESC additiveDescription = blendDescription(true);
        if (FAILED(device->CreateRasterizerState(&rasterizerDescription, &rasterizerState))
            || FAILED(device->CreateDepthStencilState(&depthDescription, &depthState))
            || FAILED(device->CreateSamplerState(&samplerDescription, &samplerState))
            || FAILED(device->CreateBlendState(&alphaDescription, &alphaBlend))
            || FAILED(device->CreateBlendState(&additiveDescription, &additiveBlend))) {
            error = "D3D11 UI fixed-function state creation failed";
            shutdown();
            return false;
        }
        statistics = {};
        uploadedIdentity = 0;
        uploadedRevision = 0;
        ready = true;
        error.clear();
        return true;
    }

    [[nodiscard]] bool synchronizeTextures(TextureStore& store) noexcept {
        if (!ready || store.slotCount() > textures.size()) {
            error = "D3D11 UI renderer or texture capacity is unavailable";
            return false;
        }
        for (std::size_t index = 0; index < textures.size(); ++index) {
            const TextureHandle handle = store.handleAt(index);
            D3D11Texture& texture = textures[index];
            if (!handle.valid()) {
                texture = {};
                continue;
            }
            TextureView view = store.view(handle);
            if (texture.view != nullptr && texture.handle == handle.packed()
                && texture.revision == view.revision) {
                continue;
            }
            if (view.backingPolicy == TextureBackingPolicy::ExternalGpu) {
                error = "D3D11 external texture is not explicitly bound";
                return false;
            }
            if (!view.backingAvailable) {
                static_cast<void>(store.ensureCpuBacking(handle));
                view = store.view(handle);
            }
            const std::uint32_t bytesPerPixel = view.format == TextureFormat::Alpha8 ? 1U : 4U;
            std::size_t minimumPitch = 0;
            std::size_t expectedBytes = 0;
            if (!view.handle.valid() || view.width == 0 || view.height == 0
                || !view.backingAvailable
                || !checkedMultiply(
                    static_cast<std::size_t>(view.width),
                    static_cast<std::size_t>(bytesPerPixel),
                    minimumPitch)
                || view.rowPitch < minimumPitch
                || !checkedMultiply(
                    static_cast<std::size_t>(view.rowPitch),
                    static_cast<std::size_t>(view.height),
                    expectedBytes)
                || expectedBytes != view.pixels.size()) {
                error = "D3D11 texture metadata or CPU backing is invalid";
                return false;
            }
            const DXGI_FORMAT format = view.format == TextureFormat::Alpha8
                ? DXGI_FORMAT_R8_UNORM
                : (view.colorSpace == TextureColorSpace::Srgb
                    ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                    : DXGI_FORMAT_R8G8B8A8_UNORM);
            const D3D11_TEXTURE2D_DESC description{
                .Width = view.width,
                .Height = view.height,
                .MipLevels = 1,
                .ArraySize = 1,
                .Format = format,
                .SampleDesc = {.Count = 1, .Quality = 0},
                .Usage = D3D11_USAGE_IMMUTABLE,
                .BindFlags = D3D11_BIND_SHADER_RESOURCE,
            };
            const D3D11_SUBRESOURCE_DATA initial{
                .pSysMem = view.pixels.data(),
                .SysMemPitch = view.rowPitch,
            };
            ComPtr<ID3D11Texture2D> resource;
            ComPtr<ID3D11ShaderResourceView> resourceView;
            if (FAILED(device->CreateTexture2D(&description, &initial, &resource))
                || FAILED(device->CreateShaderResourceView(resource.Get(), nullptr, &resourceView))) {
                error = "D3D11 texture resource creation failed";
                return false;
            }
            texture.view = std::move(resourceView);
            texture.handle = handle.packed();
            texture.revision = view.revision;
            texture.alphaMode = view.alphaMode;
            ++statistics.textureUploads;
        }
        error.clear();
        return true;
    }

    [[nodiscard]] bool bindExternalTexture(
        const TextureStore& store,
        TextureHandle handle,
        ID3D11ShaderResourceView& resourceView) noexcept {
        if (!ready || !handle.valid() || handle.value() > textures.size()) {
            error = "D3D11 external texture binding is invalid";
            return false;
        }
        const TextureView view = store.view(handle);
        if (!view.handle.valid() || view.backingPolicy != TextureBackingPolicy::ExternalGpu) {
            error = "D3D11 external texture requires an ExternalGpu TextureStore entry";
            return false;
        }
        ComPtr<ID3D11Resource> resource;
        resourceView.GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture2D;
        ComPtr<ID3D11Device> resourceDevice;
        if (resource == nullptr || FAILED(resource.As(&texture2D))
            || texture2D == nullptr) {
            error = "D3D11 external texture view is not backed by a 2D texture";
            return false;
        }
        resource->GetDevice(&resourceDevice);
        if (resourceDevice == nullptr || !sameComIdentity(*device.Get(), *resourceDevice.Get())) {
            error = "D3D11 external texture belongs to a different device";
            return false;
        }
        D3D11_TEXTURE2D_DESC description{};
        texture2D->GetDesc(&description);
        const DXGI_FORMAT expectedFormat = view.format == TextureFormat::Alpha8
            ? DXGI_FORMAT_R8_UNORM
            : (view.colorSpace == TextureColorSpace::Srgb
                ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                : DXGI_FORMAT_R8G8B8A8_UNORM);
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        resourceView.GetDesc(&viewDescription);
        if (description.Width != view.width || description.Height != view.height
            || description.MipLevels == 0 || description.ArraySize != 1
            || description.Format != expectedFormat
            || (description.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0
            || viewDescription.Format != expectedFormat
            || viewDescription.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D
            || viewDescription.Texture2D.MostDetailedMip != 0
            || viewDescription.Texture2D.MipLevels == 0) {
            error = "D3D11 external texture storage does not match its TextureStore entry";
            return false;
        }
        D3D11Texture& texture = textures[handle.value() - 1U];
        texture.view = &resourceView;
        texture.handle = handle.packed();
        texture.revision = view.revision;
        texture.alphaMode = view.alphaMode;
        error.clear();
        return true;
    }

    [[nodiscard]] bool render(const RenderPacket& packet, UiRenderViewport viewport) noexcept {
        ++statistics.frameAttempts;
        if (!ready || !valid(viewport)
            || packet.instances().size() > configuration.instanceCapacity) {
            ++statistics.rejectedFrames;
            error = "D3D11 UI render packet, viewport, or renderer state is invalid";
            return false;
        }
        if (FAILED(device->GetDeviceRemovedReason())) {
            ++statistics.rejectedFrames;
            error = "D3D11 UI device has been removed";
            return false;
        }
        for (const DrawBatch& batch : packet.batches()) {
            if (batch.textureCount > DrawBatch::kTextureCapacity
                || batch.firstInstance > packet.instances().size()
                || batch.instanceCount > packet.instances().size() - batch.firstInstance) {
                ++statistics.rejectedFrames;
                error = "D3D11 UI batch range is invalid";
                return false;
            }
            for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
                const TextureHandle handle = batch.textures[slot];
                if (!handle.valid() || handle.value() > textures.size()) {
                    ++statistics.rejectedFrames;
                    error = "D3D11 UI batch texture handle is invalid";
                    return false;
                }
                const D3D11Texture& texture = textures[handle.value() - 1U];
                if (texture.view == nullptr || texture.handle != handle.packed()) {
                    ++statistics.rejectedFrames;
                    error = "D3D11 UI batch texture is not synchronized";
                    return false;
                }
            }
        }

        if (!packet.instances().empty()
            && (uploadedIdentity != packet.identity() || uploadedRevision != packet.revision())) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(
                    instanceBuffer.Get(),
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mapped))
                || mapped.pData == nullptr) {
                ++statistics.rejectedFrames;
                error = "D3D11 UI instance upload failed";
                return false;
            }
            const std::size_t bytes = packet.instances().size() * sizeof(DrawInstance);
            std::memcpy(mapped.pData, packet.instances().data(), bytes);
            context->Unmap(instanceBuffer.Get(), 0);
            statistics.uploadedInstanceBytes += bytes;
            uploadedIdentity = packet.identity();
            uploadedRevision = packet.revision();
        }

        const UiFrameConstants frame{
            .viewport = {
                static_cast<float>(viewport.framebufferWidth),
                static_cast<float>(viewport.framebufferHeight),
            },
            .scale = {
                viewport.logicalToFramebuffer.scale.x,
                viewport.logicalToFramebuffer.scale.y,
            },
            .translation = {
                viewport.logicalToFramebuffer.translation.x,
                viewport.logicalToFramebuffer.translation.y,
            },
            .minimumAntialiasWidth = 0.75F / std::max(
                viewport.logicalToFramebuffer.scale.x,
                viewport.logicalToFramebuffer.scale.y),
        };
        D3D11_MAPPED_SUBRESOURCE mappedFrame{};
        if (FAILED(context->Map(
                frameConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedFrame))) {
            ++statistics.rejectedFrames;
            error = "D3D11 UI frame-constant upload failed";
            return false;
        }
        std::memcpy(mappedFrame.pData, &frame, sizeof(frame));
        context->Unmap(frameConstants.Get(), 0);

        const UINT stride = sizeof(DrawInstance);
        const UINT offset = 0;
        ID3D11Buffer* instances[]{instanceBuffer.Get()};
        ID3D11Buffer* frameBuffers[]{frameConstants.Get()};
        context->IASetInputLayout(inputLayout.Get());
        context->IASetVertexBuffers(0, 1, instances, &stride, &offset);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, frameBuffers);
        context->PSSetConstantBuffers(0, 1, frameBuffers);
        context->RSSetState(rasterizerState.Get());
        context->OMSetDepthStencilState(depthState.Get(), 0);
        const D3D11_VIEWPORT nativeViewport{
            .TopLeftX = 0.0F,
            .TopLeftY = 0.0F,
            .Width = static_cast<float>(viewport.framebufferWidth),
            .Height = static_cast<float>(viewport.framebufferHeight),
            .MinDepth = 0.0F,
            .MaxDepth = 1.0F,
        };
        context->RSSetViewports(1, &nativeViewport);

        for (const DrawBatch& batch : packet.batches()) {
            if (batch.instanceCount == 0) continue;
            ScissorRect scissor{};
            if (batch.clip.enabled && !makeScissorRect(batch.clip.area, viewport, scissor)) {
                continue;
            }
            const D3D11_RECT nativeScissor = batch.clip.enabled
                ? D3D11_RECT{scissor.left, scissor.top, scissor.right, scissor.bottom}
                : D3D11_RECT{
                    0,
                    0,
                    static_cast<LONG>(viewport.framebufferWidth),
                    static_cast<LONG>(viewport.framebufferHeight),
                };
            context->RSSetScissorRects(1, &nativeScissor);
            context->OMSetBlendState(
                batch.blend == BlendMode::Additive ? additiveBlend.Get() : alphaBlend.Get(),
                nullptr,
                0xFFFFFFFFU);

            if (batch.textureCount == 0) {
                context->PSSetShader(textureFreePixelShader.Get(), nullptr, 0);
            } else {
                std::array<ID3D11ShaderResourceView*, DrawBatch::kTextureCapacity> views{};
                UiTextureConstants textureData{};
                for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
                    const D3D11Texture& texture = textures[batch.textures[slot].value() - 1U];
                    views[slot] = texture.view.Get();
                    textureData.alphaModes[slot] = static_cast<std::uint32_t>(texture.alphaMode);
                }
                D3D11_MAPPED_SUBRESOURCE mappedTextures{};
                if (FAILED(context->Map(
                        textureConstants.Get(),
                        0,
                        D3D11_MAP_WRITE_DISCARD,
                        0,
                        &mappedTextures))) {
                    ++statistics.rejectedFrames;
                    error = "D3D11 UI texture-constant upload failed";
                    return false;
                }
                std::memcpy(mappedTextures.pData, &textureData, sizeof(textureData));
                context->Unmap(textureConstants.Get(), 0);
                ID3D11Buffer* textureBuffers[]{textureConstants.Get()};
                ID3D11SamplerState* samplers[]{samplerState.Get()};
                context->PSSetShader(pixelShader.Get(), nullptr, 0);
                context->PSSetConstantBuffers(1, 1, textureBuffers);
                context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
                context->PSSetSamplers(0, 1, samplers);
            }
            context->DrawInstanced(4, batch.instanceCount, 0, batch.firstInstance);
            ++statistics.drawCalls;
            statistics.submittedInstances += batch.instanceCount;
        }
        ++statistics.successfulFrames;
        error.clear();
        return true;
    }

    void shutdown() noexcept {
        textures.clear();
        additiveBlend.Reset();
        alphaBlend.Reset();
        samplerState.Reset();
        depthState.Reset();
        rasterizerState.Reset();
        textureConstants.Reset();
        frameConstants.Reset();
        instanceBuffer.Reset();
        inputLayout.Reset();
        textureFreePixelShader.Reset();
        pixelShader.Reset();
        vertexShader.Reset();
        context.Reset();
        device.Reset();
        uploadedIdentity = 0;
        uploadedRevision = 0;
        ready = false;
    }

    [[nodiscard]] bool initialized() const noexcept { return ready; }
    [[nodiscard]] DirectXRenderStatistics publicStatistics() const noexcept {
        DirectXRenderStatistics result{};
        result.backend = henia::backend::directx::Api::D3D11;
        result.frameAttempts = statistics.frameAttempts;
        result.successfulFrames = statistics.successfulFrames;
        result.rejectedFrames = statistics.rejectedFrames;
        result.drawCalls = statistics.drawCalls;
        result.submittedInstances = statistics.submittedInstances;
        result.uploadedInstanceBytes = statistics.uploadedInstanceBytes;
        result.textureUploads = statistics.textureUploads;
        return result;
    }
    [[nodiscard]] std::string_view lastError() const noexcept { return error.view(); }

private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11PixelShader> textureFreePixelShader;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11Buffer> instanceBuffer;
    ComPtr<ID3D11Buffer> frameConstants;
    ComPtr<ID3D11Buffer> textureConstants;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11BlendState> alphaBlend;
    ComPtr<ID3D11BlendState> additiveBlend;
    ComPtr<ID3D11SamplerState> samplerState;
    std::vector<D3D11Texture> textures;
    D3D11RendererConfiguration configuration{};
    DirectXRenderStatistics statistics{};
    henia::detail::FixedError error;
    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_UNKNOWN;
    std::uint64_t uploadedIdentity = 0;
    std::uint64_t uploadedRevision = 0;
    bool ready = false;
};

} // namespace

struct DirectXRenderer::Implementation final {
    std::unique_ptr<D3D11UiRenderer> d3d11;
    std::unique_ptr<D3D12Renderer> d3d12;
    henia::backend::directx::Api active = henia::backend::directx::Api::None;
    henia::detail::FixedError error;

    void shutdown() noexcept {
        if (d3d11 != nullptr) d3d11->shutdown();
        if (d3d12 != nullptr) d3d12->shutdown();
        d3d11.reset();
        d3d12.reset();
        active = henia::backend::directx::Api::None;
    }
};

DirectXRenderer::DirectXRenderer() : mImplementation(std::make_unique<Implementation>()) {}
DirectXRenderer::~DirectXRenderer() { shutdown(); }

bool DirectXRenderer::initialize(
    ID3D11Device& device,
    ID3D11DeviceContext& context,
    DXGI_FORMAT format,
    D3D11RendererConfiguration configuration) noexcept {
    try {
        if (mImplementation->active != henia::backend::directx::Api::None
            && mImplementation->active != henia::backend::directx::Api::D3D11) {
            mImplementation->error = "DirectX UI renderer is already initialized with D3D12";
            return false;
        }
        if (mImplementation->d3d11 == nullptr) {
            mImplementation->d3d11 = std::make_unique<D3D11UiRenderer>();
        }
        if (!mImplementation->d3d11->initialize(device, context, format, configuration)) {
            mImplementation->error.assign(
                mImplementation->d3d11->lastError().data(),
                mImplementation->d3d11->lastError().size());
            return false;
        }
        mImplementation->active = henia::backend::directx::Api::D3D11;
        mImplementation->error.clear();
        return true;
    } catch (...) {
        mImplementation->shutdown();
        mImplementation->error = "D3D11 UI initialization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool DirectXRenderer::initialize(
    ID3D12Device& device,
    DXGI_FORMAT format,
    D3D12RendererConfiguration configuration) noexcept {
    try {
        if (mImplementation->active != henia::backend::directx::Api::None
            && mImplementation->active != henia::backend::directx::Api::D3D12) {
            mImplementation->error = "DirectX UI renderer is already initialized with D3D11";
            return false;
        }
        if (mImplementation->d3d12 == nullptr) {
            mImplementation->d3d12 = std::make_unique<D3D12Renderer>();
        }
        if (!mImplementation->d3d12->initialize(device, format, configuration)) {
            mImplementation->error.assign(
                mImplementation->d3d12->lastError().data(),
                mImplementation->d3d12->lastError().size());
            return false;
        }
        mImplementation->active = henia::backend::directx::Api::D3D12;
        mImplementation->error.clear();
        return true;
    } catch (...) {
        mImplementation->shutdown();
        mImplementation->error = "D3D12 UI initialization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool DirectXRenderer::synchronizeTextures(TextureStore& textures) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D11
        || mImplementation->d3d11 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not using D3D11";
        return false;
    }
    const bool result = mImplementation->d3d11->synchronizeTextures(textures);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d11->lastError().data(),
            mImplementation->d3d11->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::synchronizeTextures(
    TextureStore& textures,
    ID3D12CommandQueue& queue) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not using D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->synchronizeTextures(textures, queue);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d12->lastError().data(),
            mImplementation->d3d12->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::bindExternalTexture(
    const TextureStore& textures,
    TextureHandle handle,
    ID3D11ShaderResourceView& texture) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D11
        || mImplementation->d3d11 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not using D3D11";
        return false;
    }
    const bool result = mImplementation->d3d11->bindExternalTexture(
        textures, handle, texture);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d11->lastError().data(),
            mImplementation->d3d11->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::bindExternalTexture(
    const TextureStore& textures,
    TextureHandle handle,
    ID3D12Resource& texture) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not using D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->bindExternalTexture(
        textures, handle, texture);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d12->lastError().data(),
            mImplementation->d3d12->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::pollTextureUploads() noexcept {
    if (mImplementation->active == henia::backend::directx::Api::D3D11
        && mImplementation->d3d11 != nullptr) {
        mImplementation->error.clear();
        return true;
    }
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not initialized";
        return false;
    }
    const bool result = mImplementation->d3d12->pollTextureUploads();
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d12->lastError().data(),
            mImplementation->d3d12->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::render(const RenderPacket& packet, UiRenderViewport viewport) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D11
        || mImplementation->d3d11 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not using D3D11";
        return false;
    }
    const bool result = mImplementation->d3d11->render(packet, viewport);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d11->lastError().data(),
            mImplementation->d3d11->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::record(
    const RenderPacket& packet,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    UiRenderViewport viewport,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "DirectX UI renderer is not using D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->record(
        packet, commandList, submissionSlot, viewport, submissionReuse);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d12->lastError().data(),
            mImplementation->d3d12->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderer::reportGpuTime(
    std::uint64_t sampleId,
    std::uint64_t nanoseconds) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "GPU timestamp reporting is available only for D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->reportGpuTime(sampleId, nanoseconds);
    if (!result) {
        mImplementation->error = "D3D12 UI GPU timestamp sample is unknown or already resolved";
    } else {
        mImplementation->error.clear();
    }
    return result;
}

void DirectXRenderer::shutdown() noexcept {
    if (mImplementation != nullptr) mImplementation->shutdown();
}

bool DirectXRenderer::initialized() const noexcept {
    if (mImplementation->active == henia::backend::directx::Api::D3D11) {
        return mImplementation->d3d11 != nullptr && mImplementation->d3d11->initialized();
    }
    if (mImplementation->active == henia::backend::directx::Api::D3D12) {
        return mImplementation->d3d12 != nullptr && mImplementation->d3d12->initialized();
    }
    return false;
}

henia::backend::directx::Api DirectXRenderer::backend() const noexcept {
    return mImplementation->active;
}

DirectXRenderStatistics DirectXRenderer::statistics() const noexcept {
    if (mImplementation->active == henia::backend::directx::Api::D3D11
        && mImplementation->d3d11 != nullptr) {
        return mImplementation->d3d11->publicStatistics();
    }
    DirectXRenderStatistics result{};
    result.backend = mImplementation->active;
    if (mImplementation->active == henia::backend::directx::Api::D3D12
        && mImplementation->d3d12 != nullptr) {
        result.d3d12 = mImplementation->d3d12->statistics();
        result.frameAttempts = result.d3d12.frameAttempts;
        result.successfulFrames = result.d3d12.successfulFrames;
        result.rejectedFrames = result.d3d12.rejectedFrames;
        result.drawCalls = result.d3d12.drawCalls;
        result.submittedInstances = result.d3d12.submittedInstances;
        result.uploadedInstanceBytes = result.d3d12.uploadedInstanceBytes;
        result.textureUploads = result.d3d12.textureUploads;
    }
    return result;
}

std::string_view DirectXRenderer::lastError() const noexcept {
    return mImplementation->error.view();
}

} // namespace henia::ui
