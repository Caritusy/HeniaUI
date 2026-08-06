#pragma once

#include "henia/gfx/VisibilityList.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::gfx {

enum class OpenGlStatePolicy : std::uint8_t {
    Preserve,
    DedicatedContext,
};

struct OpenGlGfxStatistics final {
    std::uint64_t frameAttempts = 0;
    std::uint64_t successfulFrames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t fullInstanceUploads = 0;
    std::uint64_t partialInstanceUploads = 0;
    std::uint64_t zeroWorkInstanceRevisions = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint64_t uploadSlotExhaustions = 0;
    std::uint64_t fullUploadFallbacks = 0;
    std::uint64_t uploadFenceFailures = 0;
    std::uint64_t viewUpdates = 0;
    std::uint64_t directVisibilityFrames = 0;
    std::uint64_t cpuCulledFrames = 0;
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
    std::uint64_t wrongContextCalls = 0;
    std::uint64_t ignoredHostErrors = 0;
    std::uint64_t stateRestoreFailures = 0;
    std::uint64_t dedicatedContextFrames = 0;
    std::uint64_t initializationFailures = 0;
    std::uint64_t lifecycleRejections = 0;
    std::uint64_t abandonedContexts = 0;
    RenderProfile profile{};
};

// Host-owned OpenGL context contract. The device allocates only its own pipeline
// and instance-buffer ring; it never creates, switches, or presents a native context.
// The device is not movable: its resources must be shut down on the owning
// context and thread. The exact context used by initialize() must be current;
// WGL cannot portably validate membership of a different shared context.
// Instance upload slots are fence-owned and polled with zero timeout; render()
// returns false rather than waiting when changed content has no safe slot.
// DedicatedContext skips host-state capture/restoration and is valid only when
// the caller exclusively owns every state transition on the current context.
// Repeated initialize() is idempotent only for the exact owner/configuration;
// use shutdown() for an orderly rebuild or abandon() after permanent context loss.
class OpenGlRenderDevice final {
public:
    OpenGlRenderDevice();
    ~OpenGlRenderDevice();

    OpenGlRenderDevice(const OpenGlRenderDevice&) = delete;
    OpenGlRenderDevice& operator=(const OpenGlRenderDevice&) = delete;
    OpenGlRenderDevice(OpenGlRenderDevice&&) = delete;
    OpenGlRenderDevice& operator=(OpenGlRenderDevice&&) = delete;

    [[nodiscard]] bool initialize(
        std::size_t boxCapacity = 65536,
        std::size_t uploadSlotCount = 3,
        OpenGlStatePolicy statePolicy = OpenGlStatePolicy::Preserve) noexcept;
    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable = false,
        VisibilityOptions visibility = {}) noexcept;
    // Associates a resolved host timestamp with a retained successful sample.
    // Unknown, duplicate, expired, and previous-lifetime IDs return false.
    [[nodiscard]] bool reportGpuTime(
        std::uint64_t sampleId,
        std::uint64_t nanoseconds) noexcept;
    // Returns false and preserves every GL object when the owner context is not
    // current, allowing the host to make it current and retry destruction.
    [[nodiscard]] bool shutdown() noexcept;
    // Use only after the initialize() context has been permanently destroyed.
    // Drops stale object names without issuing GL calls and permits recreation.
    void abandon() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t boxCapacity() const noexcept;
    [[nodiscard]] std::size_t uploadSlotCount() const noexcept;
    [[nodiscard]] OpenGlGfxStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::gfx
