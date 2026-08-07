#pragma once

#include "henia/ui/DisplayList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace henia::ui {

struct DrawInstance final {
    // One ordered payload stream serves every primitive kind:
    // - rect/image/glyph: bounds is geometry, uv is texture data;
    // - stroke: bounds is the tight region, uv is the logical rectangle;
    // - line: bounds contains endpoints, uv contains adjacent endpoints.
    // - advanced analytic primitives: bounds is geometry and uv is the
    //   kind-specific four-float parameter block documented by Canvas.
    Rect bounds{};
    Rect uv{};
    Color color{};
    // Glyph instances store their shared logical run origin here. Their shader
    // parameter selects shared-origin or cumulative per-glyph pixel snapping.
    float radius = 0.0F;
    float thickness = 0.0F;
    PrimitiveKind kind = PrimitiveKind::SolidRect;
    std::uint8_t textureSlot = 0xFFU;
    LineCap lineCap = LineCap::Round;
    // Bit 0 is LineJoin; bits 1..2 are kLineHasPrevious/kLineHasNext.
    std::uint8_t lineStyle = static_cast<std::uint8_t>(LineJoin::Round);

    [[nodiscard]] constexpr LineJoin lineJoin() const noexcept {
        return static_cast<LineJoin>(lineStyle & 0x1U);
    }
    [[nodiscard]] constexpr std::uint8_t lineFlags() const noexcept {
        return static_cast<std::uint8_t>((lineStyle >> 1U) & 0x3U);
    }
    constexpr void setLineStyle(LineJoin join, std::uint8_t flags) noexcept {
        lineStyle = static_cast<std::uint8_t>(
            (static_cast<std::uint8_t>(join) & 0x1U) | ((flags & 0x3U) << 1U));
    }
    [[nodiscard]] constexpr std::uint8_t shaderParameter() const noexcept {
        return lineStyle;
    }
    constexpr void setShaderParameter(std::uint8_t parameter) noexcept {
        lineStyle = parameter;
    }
};

static_assert(sizeof(DrawInstance) == 60, "DrawInstance layout is the GPU upload contract");
static_assert(offsetof(DrawInstance, uv) == 16);
static_assert(offsetof(DrawInstance, color) == 32);
static_assert(offsetof(DrawInstance, radius) == 48);
static_assert(offsetof(DrawInstance, kind) == 56);

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
    // Sum of max(batch.instanceCount - 1, 0). This measures draw batching,
    // never source-command or instance-data elimination.
    std::uint64_t batchedInstancesBeyondFirst = 0;
    std::uint64_t maxInstancesPerBatch = 0;
    std::uint64_t texturedBatches = 0;
    std::uint64_t textureSlotsUsed = 0;
    std::uint64_t maxTextureSlotsPerBatch = 0;
    std::uint64_t clipStateBoundaries = 0;
    std::uint64_t blendStateBoundaries = 0;
    std::uint64_t textureTableCapacityBoundaries = 0;
    // Bytes needed to upload every compiled instance once. Actual backend
    // upload bytes can be lower when an immutable packet revision is reused.
    std::uint64_t fullInstanceUploadBytes = 0;
    // Conservative pixel-space quad area before viewport/scissor clipping.
    // This tracks fragment-work bounds; it is not a hardware occlusion query.
    std::uint64_t estimatedFragmentArea = 0;
    // Effect-only observability. Variant transitions count adjacent instance
    // kind changes without forcing a batch or pipeline split.
    std::uint64_t effectInstances = 0;
    std::uint64_t shaderVariantTransitions = 0;
    std::uint64_t effectEstimatedFragmentArea = 0;
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
    [[nodiscard]] std::uint64_t cpuBuildNanoseconds() const noexcept;
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
    void addEstimatedFragmentArea(std::uint64_t area) noexcept;
    void addEffectEstimatedFragmentArea(std::uint64_t area) noexcept;
    void setSourceCommands(std::size_t count) noexcept;
    [[nodiscard]] bool rejectPacket(bool invalidInput = false) noexcept;
    void completePacket() noexcept;

    std::shared_ptr<detail::RenderPacketPool> mPool;
    detail::RenderPacketStorage* mStorage = nullptr;
};

} // namespace henia::ui
