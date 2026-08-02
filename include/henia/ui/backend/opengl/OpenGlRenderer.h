#pragma once

#include "henia/ui/RenderPacket.h"
#include "henia/ui/resource/TextureStore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace henia::ui {

struct OpenGlRenderStatistics final {
    std::uint64_t frames = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t instanceUploads = 0;
    std::uint64_t uploadedInstanceBytes = 0;
    std::uint64_t uploadSlotExhaustions = 0;
    std::uint64_t fullUploadFallbacks = 0;
    std::uint64_t uploadFenceFailures = 0;
    std::uint64_t textureUploads = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t invalidInputFrames = 0;
    std::uint64_t capacityRejectedFrames = 0;
};

// OpenGlRenderer never creates, binds, or swaps a native context. Its owner must
// keep one renderer instance per resource-sharing context group and make the
// correct OpenGL 3.3+ context current for every call. The renderer is not
// movable: its resources must be shut down on the owning context and thread.
// Instance upload slots are fence-owned and polled with zero timeout; render()
// returns false rather than waiting when changed content has no safe slot.
class OpenGlRenderer final {
public:
    OpenGlRenderer();
    ~OpenGlRenderer();

    OpenGlRenderer(const OpenGlRenderer&) = delete;
    OpenGlRenderer& operator=(const OpenGlRenderer&) = delete;
    OpenGlRenderer(OpenGlRenderer&&) = delete;
    OpenGlRenderer& operator=(OpenGlRenderer&&) = delete;

    // Initialization performs all CPU bookkeeping and upload-ring allocation.
    // Texture synchronization rejects stores larger than textureCapacity.
    [[nodiscard]] bool initialize(
        std::size_t instanceCapacity = 16384,
        std::size_t textureCapacity = 256,
        std::size_t uploadSlotCount = 3) noexcept;
    [[nodiscard]] bool synchronizeTextures(const TextureStore& textures) noexcept;
    [[nodiscard]] bool render(
        const RenderPacket& packet,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t instanceCapacity() const noexcept;
    [[nodiscard]] std::size_t textureCapacity() const noexcept;
    [[nodiscard]] std::size_t uploadSlotCount() const noexcept;
    [[nodiscard]] OpenGlRenderStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::ui
