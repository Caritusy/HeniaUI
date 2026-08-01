#pragma once

#include "henia/gfx/InstanceBatch.h"

#include <cstddef>
#include <span>
#include <vector>

namespace henia::gfx {

// Mutable producer used outside render callbacks. snapshot() returns immutable,
// shareable storage that remains valid while a producer prepares the next revision.
class ShapeBatch3D final {
public:
    ShapeBatch3D();

    void reserve(std::size_t capacity);
    void clear();
    void replaceBoxes(std::span<const BoxInstance> boxes);
    std::size_t addBox(BoxInstance box);
    [[nodiscard]] bool updateBox(std::size_t index, BoxInstance box);
    [[nodiscard]] std::size_t size() const noexcept;
    void setDepthState(DepthState state) noexcept;
    [[nodiscard]] DepthState depthState() const noexcept;
    [[nodiscard]] InstanceBatch snapshot();

private:
    void ensureWritable();
    void markDirty(std::size_t offset, std::size_t count) noexcept;

    std::shared_ptr<std::vector<BoxInstance>> mBoxes;
    DepthState mDepthState{};
    std::uint64_t mIdentity = 0;
    std::uint64_t mRevision = 0;
    std::size_t mDirtyOffset = 0;
    std::size_t mDirtyEnd = 0;
    std::uint64_t mPendingBuildNanoseconds = 0;
    bool mDirty = true;
};

} // namespace henia::gfx
