#pragma once

#include "henia/gfx/InstanceBatch.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace henia::gfx {

enum class VisibilityMode : std::uint8_t {
    Direct,
    CpuFrustum,
    Automatic,
};

// Direct preserves the original one-upload/one-draw path. CpuFrustum builds a
// stable, compact instance stream. Automatic selects CpuFrustum only when the
// source count reaches automaticThreshold.
struct VisibilityOptions final {
    VisibilityMode mode = VisibilityMode::Direct;
    std::uint32_t applicationVisibilityMask = ~std::uint32_t{0};
    float minimumProjectedExtentPixels = 0.0F;
    std::size_t automaticThreshold = 32768;

    [[nodiscard]] constexpr bool operator==(const VisibilityOptions&) const noexcept = default;
};

[[nodiscard]] inline std::string_view validate(VisibilityOptions options) noexcept {
    if (static_cast<std::uint8_t>(options.mode)
        > static_cast<std::uint8_t>(VisibilityMode::Automatic)) {
        return "visibility.mode";
    }
    if (!std::isfinite(options.minimumProjectedExtentPixels)
        || options.minimumProjectedExtentPixels < 0.0F) {
        return "visibility.minimumProjectedExtentPixels";
    }
    return {};
}

[[nodiscard]] constexpr bool usesCpuVisibility(
    VisibilityOptions options,
    std::size_t sourceCount) noexcept {
    return options.mode == VisibilityMode::CpuFrustum
        || (options.mode == VisibilityMode::Automatic
            && sourceCount >= options.automaticThreshold);
}

struct VisibilityStatistics final {
    std::uint64_t sourceInstances = 0;
    std::uint64_t visibleInstances = 0;
    std::uint64_t frustumRejectedInstances = 0;
    std::uint64_t applicationMaskRejectedInstances = 0;
    std::uint64_t projectedSizeRejectedInstances = 0;
    std::uint64_t chunkTests = 0;
    std::uint64_t chunkRejectedInstances = 0;
    std::uint64_t rebuiltChunks = 0;
    std::uint64_t reusedChunks = 0;
    std::uint64_t cullingNanoseconds = 0;
    bool resultReused = false;
};

// Reusable CPU visibility workspace. reserve() is explicit so update() can be
// allocation-free on the frame path. Visible indices preserve source order and
// compact instances preserve every BoxInstance field, including effect data.
class VisibilityList final {
public:
    VisibilityList();
    ~VisibilityList();

    VisibilityList(const VisibilityList&) = delete;
    VisibilityList& operator=(const VisibilityList&) = delete;
    VisibilityList(VisibilityList&&) = delete;
    VisibilityList& operator=(VisibilityList&&) = delete;

    [[nodiscard]] bool reserve(std::size_t sourceCapacity) noexcept;
    [[nodiscard]] bool update(
        const InstanceBatch& batch,
        const ViewParameters& view,
        VisibilityOptions options = {}) noexcept;

    [[nodiscard]] std::span<const std::size_t> indices() const noexcept;
    [[nodiscard]] std::span<const BoxInstance> boxes() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t storageBytes() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] VisibilityStatistics statistics() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;

private:
    struct Implementation;
    std::unique_ptr<Implementation> mImplementation;
};

} // namespace henia::gfx
