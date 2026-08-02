#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace henia::ui::detail {

struct OpenGlTextureState final {
    std::uint32_t object = 0;
    std::uint32_t stagedObject = 0;
    std::uint64_t handle = 0;
    std::uint64_t stagedHandle = 0;
    std::uint64_t revision = 0;
    std::uint64_t stagedRevision = 0;
    std::uint64_t byteSize = 0;
    std::uint64_t stagedByteSize = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t stagedWidth = 0;
    std::uint32_t stagedHeight = 0;
    std::uint8_t format = 0;
    std::uint8_t stagedFormat = 0;
    bool external = false;
    bool stagedExternal = false;
    bool owned = true;
    bool stagedOwned = true;
};

// Stages replacement objects without changing the renderer-visible object or
// revision. A synchronization call commits every staged texture together, or
// destroys every candidate and leaves all previous objects retryable.
class OpenGlTextureTransaction final {
public:
    explicit OpenGlTextureTransaction(std::span<OpenGlTextureState> textures) noexcept
        : mTextures(textures) {}

    template <typename Create, typename Upload, typename Destroy>
    [[nodiscard]] bool stage(
        std::size_t index,
        std::uint64_t handle,
        std::uint64_t revision,
        Create&& create,
        Upload&& upload,
        Destroy&& destroy) noexcept {
        if (index >= mTextures.size() || handle == 0 || revision == 0) {
            return false;
        }
        OpenGlTextureState& texture = mTextures[index];
        if (texture.stagedObject != 0) {
            return false;
        }
        const std::uint32_t candidate = std::forward<Create>(create)();
        if (candidate == 0) {
            return false;
        }
        if (!std::forward<Upload>(upload)(candidate)) {
            std::forward<Destroy>(destroy)(candidate);
            return false;
        }
        texture.stagedObject = candidate;
        texture.stagedHandle = handle;
        texture.stagedRevision = revision;
        return true;
    }

    template <typename Destroy>
    void rollback(Destroy&& destroy) noexcept {
        for (OpenGlTextureState& texture : mTextures) {
            if (texture.stagedObject != 0) {
                destroy(texture.stagedObject);
                texture.stagedObject = 0;
                texture.stagedHandle = 0;
                texture.stagedRevision = 0;
                texture.stagedByteSize = 0;
                texture.stagedWidth = 0;
                texture.stagedHeight = 0;
                texture.stagedFormat = 0;
                texture.stagedExternal = false;
                texture.stagedOwned = true;
            }
        }
    }

    template <typename Destroy>
    [[nodiscard]] std::size_t commit(Destroy&& destroy) noexcept {
        std::size_t committed = 0;
        for (OpenGlTextureState& texture : mTextures) {
            if (texture.stagedObject == 0) {
                continue;
            }
            const std::uint32_t previous = texture.object;
            const bool previousOwned = texture.owned;
            texture.object = texture.stagedObject;
            texture.handle = texture.stagedHandle;
            texture.revision = texture.stagedRevision;
            texture.byteSize = texture.stagedByteSize;
            texture.width = texture.stagedWidth;
            texture.height = texture.stagedHeight;
            texture.format = texture.stagedFormat;
            texture.external = texture.stagedExternal;
            texture.owned = texture.stagedOwned;
            texture.stagedObject = 0;
            texture.stagedHandle = 0;
            texture.stagedRevision = 0;
            texture.stagedByteSize = 0;
            texture.stagedWidth = 0;
            texture.stagedHeight = 0;
            texture.stagedFormat = 0;
            texture.stagedExternal = false;
            texture.stagedOwned = true;
            if (previous != 0 && previousOwned) {
                destroy(previous);
            }
            ++committed;
        }
        return committed;
    }

private:
    std::span<OpenGlTextureState> mTextures;
};

} // namespace henia::ui::detail
