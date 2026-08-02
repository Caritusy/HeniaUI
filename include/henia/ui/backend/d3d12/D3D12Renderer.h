#pragma once

#include "henia/backend/d3d12/D3D12SubmissionReuse.h"
#include "henia/ui/RenderPacket.h"
#include "henia/ui/resource/TextureStore.h"

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::ui {

struct D3D12RendererConfiguration final {
    std::size_t instanceCapacity = 32768;
    std::uint32_t submissionCapacity = 8;
    std::uint32_t batchCapacity = 256;
    std::uint32_t textureCapacity = 256;
    std::uint32_t textureUploadBatchCapacity = 3;
};

struct D3D12RenderStatistics final {
    std::uint64_t recordedFrames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t instanceUploads = 0;
    std::uint64_t textureUploads = 0;
    std::uint64_t textureUploadBatches = 0;
    std::uint64_t failedTextureUploadBatches = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t invalidInputFrames = 0;
    std::uint64_t capacityRejectedFrames = 0;
    std::uint64_t commandListValidationFailures = 0;
    std::uint64_t descriptorHeapBindings = 0;
    std::uint64_t descriptorTableCopies = 0;
    std::uint64_t descriptorTableCacheHits = 0;
    std::uint64_t textureFreeFrames = 0;
    std::uint64_t submissionFenceChecks = 0;
    std::uint64_t submissionSlotBusyRejections = 0;
    std::uint64_t deviceRemovalRejections = 0;
    std::uint64_t lifecycleRejections = 0;
};

// The host owns command allocators, back-buffer transitions, render targets,
// queue submission, and fences. A submission slot may be reused only after the
// host has observed completion of every command list that referenced it. The
// renderer is not movable because its resources and submission slots remain
// associated with that host device and fence lifecycle.
// Pass SubmissionReuse to record() to check a previous slot fence before any
// mapped upload write or retained-texture release. An empty value declares that
// the slot is new or that the host synchronized it by another mechanism.
//
// Texture synchronization records work on the supplied direct queue without
// waiting for it to become idle. A successful synchronizeTextures call means
// that every current revision is committed or has fence-tracked upload work.
// pollTextureUploads commits completed revisions and never waits. Replaced
// textures remain alive through every submission slot that copied their SRV.
// Use one direct queue for the renderer's lifetime and make it idle before
// shutdown.
//
// record() requires an open DIRECT command list created by the initialize()
// device and compatible with the configured single-sample RT format. It does
// not bind or transition render targets. It overwrites graphics root signature
// and parameters, PSO, IA topology/VB slot 0, viewport 0, and scissor 0. A
// textured packet also replaces the CBV/SRV/UAV descriptor heap, invalidating
// host descriptor tables. D3D12 cannot query/restore these states: the host must
// fully rebind every state used by subsequent draws. Texture-free packets use a
// heap-free pipeline and do not call SetDescriptorHeaps().
class D3D12Renderer final {
public:
    D3D12Renderer();
    ~D3D12Renderer();

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;
    D3D12Renderer(D3D12Renderer&&) = delete;
    D3D12Renderer& operator=(D3D12Renderer&&) = delete;

    [[nodiscard]] bool initialize(
        ID3D12Device& device,
        DXGI_FORMAT renderTargetFormat,
        D3D12RendererConfiguration configuration = {}) noexcept;
    [[nodiscard]] bool synchronizeTextures(
        const TextureStore& textures,
        ID3D12CommandQueue& directQueue) noexcept;
    [[nodiscard]] bool pollTextureUploads() noexcept;
    [[nodiscard]] bool record(
        const RenderPacket& packet,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight,
        henia::backend::d3d12::SubmissionReuse submissionReuse = {}) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t instanceCapacity() const noexcept;
    [[nodiscard]] std::uint32_t submissionCapacity() const noexcept;
    [[nodiscard]] std::uint32_t pendingTextureUploadBatches() const noexcept;
    [[nodiscard]] D3D12RenderStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::ui
