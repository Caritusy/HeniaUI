#pragma once

#include "henia/backend/d3d12/D3D12InstanceStorage.h"
#include "henia/backend/d3d12/D3D12SubmissionReuse.h"
#include "henia/gfx/VisibilityList.h"

#include <d3d12.h>
#include <dxgiformat.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::gfx {

struct D3D12GfxConfiguration final {
    std::size_t boxCapacity = 65536;
    std::uint32_t submissionCapacity = 8;
    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
    std::uint32_t sampleCount = 1;
    henia::backend::d3d12::InstanceStorageStrategy instanceStorage =
        henia::backend::d3d12::InstanceStorageStrategy::Automatic;
    std::size_t gpuLocalInstanceThresholdBytes =
        henia::backend::d3d12::kDefaultGpuLocalInstanceThresholdBytes;
    // Optional host-owned cache. It is borrowed only for initialize() and must
    // have been created by the same device.
    ID3D12PipelineLibrary* pipelineLibrary = nullptr;
};

struct D3D12GfxStatistics final {
    std::uint64_t frameAttempts = 0;
    std::uint64_t successfulFrames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t fullInstanceUploads = 0;
    std::uint64_t partialInstanceUploads = 0;
    std::uint64_t zeroWorkInstanceRevisions = 0;
    // CPU writes into the submission slot's mapped staging resource.
    std::uint64_t uploadedInstanceBytes = 0;
    // Default-heap CopyBufferRegion work, separate from staging writes.
    std::uint64_t instanceCopyOperations = 0;
    std::uint64_t copiedInstanceBytes = 0;
    // Logical instance bytes consumed by draws bound directly to upload memory.
    std::uint64_t uploadHeapReadBytes = 0;
    std::uint64_t gpuLocalResidentBytes = 0;
    std::uint64_t gpuLocalFrames = 0;
    std::uint64_t directUploadFrames = 0;
    std::uint64_t viewUpdates = 0;
    std::uint64_t directVisibilityFrames = 0;
    std::uint64_t cpuCulledFrames = 0;
    std::uint64_t indirectDrawCalls = 0;
    std::uint64_t indirectArgumentUpdates = 0;
    std::uint64_t visibilitySourceInstances = 0;
    std::uint64_t visibilityRejectedInstances = 0;
    std::uint64_t visibilityChunkTests = 0;
    std::uint64_t visibilityChunkRejectedInstances = 0;
    std::uint64_t visibilityResultReuses = 0;
    std::uint64_t visibilityCullingNanoseconds = 0;
    std::uint64_t depthFallbacks = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t invalidInputFrames = 0;
    std::uint64_t capacityRejectedFrames = 0;
    std::uint64_t commandListValidationFailures = 0;
    std::uint64_t submissionFenceChecks = 0;
    std::uint64_t submissionSlotBusyRejections = 0;
    std::uint64_t deviceRemovalRejections = 0;
    std::uint64_t lifecycleRejections = 0;
    std::uint64_t pipelineCacheHits = 0;
    std::uint64_t pipelineCacheMisses = 0;
    std::uint64_t pipelineCacheStores = 0;
    std::uint64_t pipelineCacheStoreFailures = 0;
    bool adapterArchitectureKnown = false;
    bool adapterUma = true;
    RenderProfile profile{};
};

// The host owns command allocators, RT/DS attachments, transitions, queue
// submission and fences. A slot is reusable only after its host fence completes.
// The device is not movable because its resources and submission slots remain
// associated with that host device and fence lifecycle.
// Pass SubmissionReuse to record() to check a previous slot fence before any
// mapped upload write. An empty value declares independent host synchronization.
// record() requires an open DIRECT list from the initialize() device and host-
// bound RT/optional DS targets matching the configured formats/sample count. It
// overwrites graphics root signature/constants, PSO, IA topology/VB slot 0,
// viewport 0, and scissor 0. D3D12 cannot restore them, so the host must fully
// rebind every state consumed by later draws.
class D3D12RenderDevice final {
public:
    D3D12RenderDevice();
    ~D3D12RenderDevice();

    D3D12RenderDevice(const D3D12RenderDevice&) = delete;
    D3D12RenderDevice& operator=(const D3D12RenderDevice&) = delete;
    D3D12RenderDevice(D3D12RenderDevice&&) = delete;
    D3D12RenderDevice& operator=(D3D12RenderDevice&&) = delete;

    [[nodiscard]] bool initialize(ID3D12Device& device, D3D12GfxConfiguration configuration = {}) noexcept;
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
    // Associates a resolved host timestamp with a retained successful sample.
    // Unknown, duplicate, expired, and previous-lifetime IDs return false.
    [[nodiscard]] bool reportGpuTime(
        std::uint64_t sampleId,
        std::uint64_t nanoseconds) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t boxCapacity() const noexcept;
    [[nodiscard]] std::uint32_t submissionCapacity() const noexcept;
    [[nodiscard]] D3D12GfxStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::gfx
