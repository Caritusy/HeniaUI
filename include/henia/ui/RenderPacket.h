#pragma once

#include "henia/ui/DisplayList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace henia::ui {

struct DrawInstance final {
    Rect bounds{};
    Rect uv{};
    Vec2 pointA{};
    Vec2 pointB{};
    Color color{};
    float radius = 0.0F;
    float thickness = 0.0F;
    std::uint32_t textureSlot = 0;
    PrimitiveKind kind = PrimitiveKind::SolidRect;
};

struct DrawBatch final {
    static constexpr std::size_t kTextureCapacity = 8;

    ClipRect clip{};
    BlendMode blend = BlendMode::PremultipliedAlpha;
    std::array<TextureHandle, kTextureCapacity> textures{};
    std::uint32_t textureCount = 0;
    std::uint32_t firstInstance = 0;
    std::uint32_t instanceCount = 0;
};

struct PacketStatistics final {
    std::uint64_t sourceCommands = 0;
    std::uint64_t instances = 0;
    std::uint64_t batches = 0;
    std::uint64_t mergedCommands = 0;
    std::uint64_t instanceCapacityGrowths = 0;
    std::uint64_t batchCapacityGrowths = 0;
};

class RenderPacket final {
public:
    RenderPacket() noexcept;
    RenderPacket(const RenderPacket&) = delete;
    RenderPacket& operator=(const RenderPacket&) = delete;
    RenderPacket(RenderPacket&&) = delete;
    RenderPacket& operator=(RenderPacket&&) = delete;

    void reserve(std::size_t instanceCapacity, std::size_t batchCapacity);
    void clear() noexcept;

    [[nodiscard]] std::span<const DrawInstance> instances() const noexcept;
    [[nodiscard]] std::span<const DrawBatch> batches() const noexcept;
    [[nodiscard]] const PacketStatistics& statistics() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t instanceCapacity() const noexcept;
    [[nodiscard]] std::size_t batchCapacity() const noexcept;

private:
    friend class BatchCompiler;

    void appendInstance(const DrawInstance& instance);
    DrawBatch& appendBatch(const DrawBatch& batch);

    std::vector<DrawInstance> mInstances;
    std::vector<DrawBatch> mBatches;
    PacketStatistics mStatistics{};
    std::uint64_t mIdentity = 0;
    std::uint64_t mRevision = 0;
};

} // namespace henia::ui
