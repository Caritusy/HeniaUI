#pragma once

#include "henia/ui/DisplayList.h"
#include "henia/ui/RenderPacket.h"

namespace henia::ui {

class BatchCompiler final {
public:
    void compile(const DisplayList& displayList, RenderPacket& output) const;

private:
    [[nodiscard]] static bool compatible(const DrawBatch& batch, const DrawCommand& command) noexcept;
    [[nodiscard]] static bool resolveTextureSlot(
        DrawBatch& batch,
        TextureHandle texture,
        std::uint32_t& slot) noexcept;
    [[nodiscard]] static DrawInstance makeInstance(
        const DrawCommand& command,
        std::uint32_t textureSlot) noexcept;
};

} // namespace henia::ui
