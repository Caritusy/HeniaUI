#pragma once

#include "henia/ui/DisplayList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

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
    std::uint64_t rejectedCommands = 0;
    std::uint64_t invalidInputCommands = 0;
    std::uint64_t capacityRejectedCommands = 0;
    std::uint64_t instanceCapacityGrowths = 0;
    std::uint64_t batchCapacityGrowths = 0;
};

namespace detail {

struct RenderPacketPool;
struct RenderPacketStorage;

} // namespace detail

// A cheap immutable snapshot handle. Copies may be consumed on other threads
// after the host publishes the handle with its normal synchronization primitive.
class RenderPacket final {
public:
    RenderPacket() noexcept = default;
    ~RenderPacket();
    RenderPacket(const RenderPacket& other) noexcept;
    RenderPacket& operator=(const RenderPacket& other) noexcept;
    RenderPacket(RenderPacket&& other) noexcept;
    RenderPacket& operator=(RenderPacket&& other) noexcept;

    [[nodiscard]] std::span<const DrawInstance> instances() const noexcept;
    [[nodiscard]] std::span<const DrawBatch> batches() const noexcept;
    [[nodiscard]] const PacketStatistics& statistics() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t instanceCapacity() const noexcept;
    [[nodiscard]] std::size_t batchCapacity() const noexcept;
    [[nodiscard]] CapacityPolicy capacityPolicy() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

private:
    friend class RenderPacketBuilder;

    RenderPacket(
        std::shared_ptr<detail::RenderPacketPool> pool,
        detail::RenderPacketStorage* storage) noexcept;
    void retain() noexcept;
    void release() noexcept;

    std::shared_ptr<detail::RenderPacketPool> mPool;
    detail::RenderPacketStorage* mStorage = nullptr;
};

// Single-producer mutable packet builder backed by reusable snapshot slots.
// begin()/publish() delimit one build. Published storage is never mutated while
// any RenderPacket handle still refers to it.
class RenderPacketBuilder final {
public:
    static constexpr std::size_t kDefaultSnapshotSlots = 3;

    RenderPacketBuilder();
    ~RenderPacketBuilder();
    RenderPacketBuilder(const RenderPacketBuilder&) = delete;
    RenderPacketBuilder& operator=(const RenderPacketBuilder&) = delete;
    RenderPacketBuilder(RenderPacketBuilder&&) = delete;
    RenderPacketBuilder& operator=(RenderPacketBuilder&&) = delete;

    void reserve(
        std::size_t instanceCapacity,
        std::size_t batchCapacity,
        CapacityPolicy capacityPolicy = CapacityPolicy::Grow,
        std::size_t snapshotSlots = kDefaultSnapshotSlots);
    [[nodiscard]] bool begin() noexcept;
    [[nodiscard]] RenderPacket publish() noexcept;

    [[nodiscard]] std::size_t snapshotSlotCount() const noexcept;
    [[nodiscard]] std::uint64_t snapshotSlotGrowths() const noexcept;
    [[nodiscard]] std::uint64_t rejectedBuilds() const noexcept;

private:
    friend class BatchCompiler;

    [[nodiscard]] bool active() const noexcept;
    void clear() noexcept;
    [[nodiscard]] DrawBatch* lastBatch() noexcept;
    [[nodiscard]] std::size_t instanceCount() const noexcept;
    [[nodiscard]] bool appendInstance(const DrawInstance& instance) noexcept;
    [[nodiscard]] DrawBatch* appendBatch(const DrawBatch& batch) noexcept;
    void setSourceCommands(std::size_t count) noexcept;
    [[nodiscard]] bool rejectPacket(bool invalidInput = false) noexcept;
    void completePacket() noexcept;

    std::shared_ptr<detail::RenderPacketPool> mPool;
    detail::RenderPacketStorage* mStorage = nullptr;
};

} // namespace henia::ui
