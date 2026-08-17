#pragma once

#include "henia/backend/directx/DirectXBackend.h"
#include "henia/gfx/backend/d3d12/D3D12RenderDevice.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgiformat.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::gfx {

struct D3D11GfxConfiguration final {
    std::size_t boxCapacity = 65536;
    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
    std::uint32_t sampleCount = 1;
    std::uint32_t sampleQuality = 0;
};

struct DirectXGfxStatistics final {
    backend::directx::Api backend = backend::directx::Api::None;
    D3D12GfxStatistics d3d12{};
    std::uint64_t frameAttempts = 0;
    std::uint64_t successfulFrames = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint64_t depthFallbacks = 0;
};

// Unified instanced-box entry point. D3D11 render() uses the host-bound
// render/depth targets; D3D12 record() uses the host command list and fences.
class DirectXRenderDevice final {
public:
    DirectXRenderDevice();
    ~DirectXRenderDevice();

    DirectXRenderDevice(const DirectXRenderDevice&) = delete;
    DirectXRenderDevice& operator=(const DirectXRenderDevice&) = delete;
    DirectXRenderDevice(DirectXRenderDevice&&) = delete;
    DirectXRenderDevice& operator=(DirectXRenderDevice&&) = delete;

    [[nodiscard]] bool initialize(
        ID3D11Device& device,
        ID3D11DeviceContext& context,
        D3D11GfxConfiguration configuration = {}) noexcept;
    [[nodiscard]] bool initialize(
        ID3D12Device& device,
        D3D12GfxConfiguration configuration = {}) noexcept;
    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable = false,
        VisibilityOptions visibility = {}) noexcept;
    [[nodiscard]] bool record(
        const InstanceBatch& batch,
        const ViewParameters& view,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        henia::backend::d3d12::SubmissionReuse submissionReuse = {}) noexcept;
    [[nodiscard]] bool record(
        const InstanceBatch& batch,
        const ViewParameters& view,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        VisibilityOptions visibility,
        henia::backend::d3d12::SubmissionReuse submissionReuse = {}) noexcept;
    [[nodiscard]] bool reportGpuTime(
        std::uint64_t sampleId,
        std::uint64_t nanoseconds) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] backend::directx::Api backend() const noexcept;
    [[nodiscard]] DirectXGfxStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::gfx
