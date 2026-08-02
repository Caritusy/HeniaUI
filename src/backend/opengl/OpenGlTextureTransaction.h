#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace henia::ui::detail {

struct OpenGlTextureState final {
    std::uint32_t object = 0;
    std::uint32_t stagedObject = 0;
    std::uint64_t revision = 0;
    std::uint64_t stagedRevision = 0;
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
        std::uint64_t revision,
        Create&& create,
        Upload&& upload,
        Destroy&& destroy) noexcept {
        if (index >= mTextures.size() || revision == 0) {
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
        texture.stagedRevision = revision;
        return true;
    }

    template <typename Destroy>
    void rollback(Destroy&& destroy) noexcept {
        for (OpenGlTextureState& texture : mTextures) {
            if (texture.stagedObject != 0) {
                destroy(texture.stagedObject);
                texture.stagedObject = 0;
                texture.stagedRevision = 0;
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
            texture.object = texture.stagedObject;
            texture.revision = texture.stagedRevision;
            texture.stagedObject = 0;
            texture.stagedRevision = 0;
            if (previous != 0) {
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
