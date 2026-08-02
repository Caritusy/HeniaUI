#pragma once

#include "henia/ui/DisplayList.h"
#include "henia/ui/RenderPacket.h"

#include <span>
#include <vector>

namespace henia::ui {

class BatchCompiler final {
public:
    [[nodiscard]] bool compile(
        const DisplayList& displayList,
        RenderPacketBuilder& output) const noexcept;
    // Reuses validated, backend-neutral instances for unchanged retained
    // segments. Global batch and texture-table ordering is still rebuilt so a
    // RenderPacket remains one immutable, self-contained snapshot.
    [[nodiscard]] bool compile(
        std::span<const DisplayListSegment> segments,
        RenderPacketBuilder& output) const noexcept;

private:
    struct PreparedCommand final {
        DrawInstance instance{};
        ClipRect clip{};
        BlendMode blend = BlendMode::PremultipliedAlpha;
        TextureHandle texture{};
    };

    struct PreparedSegment final {
        std::uint64_t identity = 0;
        std::uint64_t revision = 0;
        std::vector<PreparedCommand> commands;
    };

    enum class PrepareResult : std::uint8_t {
        Ready,
        InvalidInput,
        OutOfMemory,
    };

    [[nodiscard]] PrepareResult prepare(
        const DisplayListSegment& segment,
        PreparedSegment& output) const noexcept;
    [[nodiscard]] static bool append(
        const PreparedCommand& command,
        RenderPacketBuilder& output) noexcept;
    [[nodiscard]] static bool compatible(const DrawBatch& batch, const DrawCommand& command) noexcept;
    [[nodiscard]] static bool compatible(
        const DrawBatch& batch,
        const PreparedCommand& command) noexcept;
    [[nodiscard]] static bool resolveTextureSlot(
        DrawBatch& batch,
        TextureHandle texture,
        std::uint32_t& slot) noexcept;
    [[nodiscard]] static DrawInstance makeInstance(
        const DrawCommand& command,
        std::uint32_t textureSlot) noexcept;

    mutable std::vector<PreparedSegment> mPreparedSegments;
};

} // namespace henia::ui
