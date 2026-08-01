#pragma once

#include "henia/gfx/Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace henia::gfx {

class ShapeBatch3D;

class InstanceBatch final {
public:
    [[nodiscard]] std::span<const BoxInstance> boxes() const noexcept;
    [[nodiscard]] DepthState depthState() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] std::size_t dirtyOffset() const noexcept;
    [[nodiscard]] std::size_t dirtyCount() const noexcept;
    [[nodiscard]] std::uint64_t cpuBuildNanoseconds() const noexcept;

private:
    friend class ShapeBatch3D;
    std::shared_ptr<const std::vector<BoxInstance>> mBoxes;
    DepthState mDepthState{};
    std::uint64_t mIdentity = 0;
    std::uint64_t mRevision = 0;
    std::size_t mDirtyOffset = 0;
    std::size_t mDirtyCount = 0;
    std::uint64_t mCpuBuildNanoseconds = 0;
};

} // namespace henia::gfx
