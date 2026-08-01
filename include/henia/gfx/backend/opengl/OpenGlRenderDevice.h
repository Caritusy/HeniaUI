#pragma once

#include "henia/gfx/InstanceBatch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::gfx {

struct OpenGlGfxStatistics final {
    std::uint64_t frames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t fullInstanceUploads = 0;
    std::uint64_t partialInstanceUploads = 0;
    std::uint64_t viewUpdates = 0;
    std::uint64_t depthFallbacks = 0;
    std::uint64_t rejectedFrames = 0;
    RenderProfile profile{};
};

// Host-owned OpenGL context contract. The device allocates only its own pipeline
// and instance buffer; it never creates, switches, or presents a native context.
class OpenGlRenderDevice final {
public:
    OpenGlRenderDevice();
    ~OpenGlRenderDevice();

    OpenGlRenderDevice(const OpenGlRenderDevice&) = delete;
    OpenGlRenderDevice& operator=(const OpenGlRenderDevice&) = delete;
    OpenGlRenderDevice(OpenGlRenderDevice&&) noexcept;
    OpenGlRenderDevice& operator=(OpenGlRenderDevice&&) noexcept;

    [[nodiscard]] bool initialize(std::size_t boxCapacity = 65536) noexcept;
    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable = false) noexcept;
    void reportGpuTime(std::uint64_t nanoseconds) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t boxCapacity() const noexcept;
    [[nodiscard]] OpenGlGfxStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::gfx
