#pragma once

#include "henia/backend/directx/DirectXBackend.h"
#include "henia/ui/CoordinateTransform.h"
#include "henia/ui/RenderPacket.h"
#include "henia/ui/backend/d3d12/D3D12Renderer.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgiformat.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::ui {

struct D3D11RendererConfiguration final {
    std::size_t instanceCapacity = 32768;
    std::size_t textureCapacity = 256;
    RenderTargetColorSpace targetColorSpace = RenderTargetColorSpace::Linear;
    std::uint32_t sampleCount = 1;
    std::uint32_t sampleQuality = 0;
};

struct DirectXRenderStatistics final {
    backend::directx::Api backend = backend::directx::Api::None;
    D3D12RenderStatistics d3d12{};
    std::uint64_t frameAttempts = 0;
    std::uint64_t successfulFrames = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint64_t textureUploads = 0;
    std::uint64_t depthFallbacks = 0;
};

// Unified UI entry point. D3D11 render() consumes the currently bound host
// render target; D3D12 record() consumes an open host command list. Neither
// path creates a window, swap chain, back buffer, queue, allocator, or fence.
class DirectXRenderer final {
public:
    DirectXRenderer();
    ~DirectXRenderer();

    DirectXRenderer(const DirectXRenderer&) = delete;
    DirectXRenderer& operator=(const DirectXRenderer&) = delete;
    DirectXRenderer(DirectXRenderer&&) = delete;
    DirectXRenderer& operator=(DirectXRenderer&&) = delete;

    [[nodiscard]] bool initialize(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11RendererConfiguration configuration = {}) noexcept;
    [[nodiscard]] bool initialize(
        ID3D12Device& device,
        DXGI_FORMAT renderTargetFormat,
        D3D12RendererConfiguration configuration = {}) noexcept;
    [[nodiscard]] bool synchronizeTextures(TextureStore& textures) noexcept;
    [[nodiscard]] bool synchronizeTextures(
        TextureStore& textures,
        ID3D12CommandQueue& directQueue) noexcept;
    [[nodiscard]] bool bindExternalTexture(
        const TextureStore& textures,
        TextureHandle handle,
        ID3D11ShaderResourceView& texture) noexcept;
    [[nodiscard]] bool bindExternalTexture(
        const TextureStore& textures,
        TextureHandle handle,
        ID3D12Resource& texture) noexcept;
    [[nodiscard]] bool pollTextureUploads() noexcept;
    [[nodiscard]] bool render(
        const RenderPacket& packet,
        UiRenderViewport viewport) noexcept;
    [[nodiscard]] bool record(
        const RenderPacket& packet,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        UiRenderViewport viewport,
        henia::backend::d3d12::SubmissionReuse submissionReuse = {}) noexcept;
    [[nodiscard]] bool reportGpuTime(
        std::uint64_t sampleId,
        std::uint64_t nanoseconds) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] backend::directx::Api backend() const noexcept;
    [[nodiscard]] DirectXRenderStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::ui
