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
    std::uint64_t fullTextureUploads = 0;
    std::uint64_t partialTextureUploads = 0;
    std::uint64_t uploadedTextureBytes = 0;
    std::uint64_t gpuTextureBytes = 0;
    std::uint64_t retiredTextures = 0;
    std::uint64_t externalTextures = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t invalidInputFrames = 0;
    std::uint64_t capacityRejectedFrames = 0;
    std::uint64_t wrongContextCalls = 0;
    std::uint64_t ignoredHostErrors = 0;
    std::uint64_t stateRestoreFailures = 0;
    std::uint64_t initializationFailures = 0;
    std::uint64_t textureSynchronizationFailures = 0;
    std::uint64_t lifecycleRejections = 0;
    std::uint64_t abandonedContexts = 0;
};

enum class OpenGlExternalTextureOwnership : std::uint8_t {
    Borrowed,
    Transferred,
};

// OpenGlRenderer never creates, binds, or swaps a native context. Its owner must
// keep the exact OpenGL 3.3+ context used by initialize() current for every
// call. Shared contexts are not accepted because WGL cannot portably prove
// share-group membership. The renderer is not
// movable: its resources must be shut down on the owning context and thread.
// Instance upload slots are fence-owned and polled with zero timeout; render()
// returns false rather than waiting when changed content has no safe slot.
// Repeated initialize() is idempotent only for the exact owner/configuration;
// use shutdown() for an orderly rebuild or abandon() after permanent context loss.
class OpenGlRenderer final {
public:
    OpenGlRenderer();
    ~OpenGlRenderer();

    OpenGlRenderer(const OpenGlRenderer&) = delete;
    OpenGlRenderer& operator=(const OpenGlRenderer&) = delete;
    OpenGlRenderer(OpenGlRenderer&&) = delete;
    OpenGlRenderer& operator=(OpenGlRenderer&&) = delete;

    // Initialization commits only after every GL object and buffer allocation
    // succeeds. Texture synchronization rejects stores larger than
    // textureCapacity. Full changes are transactionally replaced; a single
    // region revision uses an exact subresource update with rollback on failure.
    [[nodiscard]] bool initialize(
        std::size_t instanceCapacity = 16384,
        std::size_t textureCapacity = 256,
        std::size_t uploadSlotCount = 3) noexcept;
    [[nodiscard]] bool synchronizeTextures(TextureStore& textures) noexcept;
    // Validates level-zero storage in the owner context. Borrowed names are
    // never deleted; transferred names follow renderer-owned lifetime.
    [[nodiscard]] bool bindExternalTexture(
        const TextureStore& textures,
        TextureHandle handle,
        std::uint32_t textureObject,
        OpenGlExternalTextureOwnership ownership = OpenGlExternalTextureOwnership::Borrowed) noexcept;
    [[nodiscard]] bool render(
        const RenderPacket& packet,
        std::uint32_t viewportWidth,
        std::uint32_t viewportHeight) noexcept;
    // Returns false and preserves every GL object when the owner context is not
    // current, allowing the host to make it current and retry destruction.
    [[nodiscard]] bool shutdown() noexcept;
    // Use only after the initialize() context has been permanently destroyed.
    // Drops stale object names without issuing GL calls so this instance can be
    // initialized on a replacement context. Calling abandon() on a live context
    // intentionally leaks that context's objects until the context is destroyed.
    void abandon() noexcept;

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
