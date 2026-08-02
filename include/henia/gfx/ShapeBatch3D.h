#pragma once

#include "henia/gfx/InstanceBatch.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

namespace henia::gfx {

// Mutable producer used outside render callbacks. snapshot() returns immutable,
// shareable storage that remains valid while a producer prepares the next revision.
class ShapeBatch3D final {
public:
    static constexpr std::size_t kInvalidIndex = std::numeric_limits<std::size_t>::max();

    ShapeBatch3D();

    void reserve(std::size_t capacity);
    void clear();
    bool replaceBoxes(std::span<const BoxInstance> boxes);
    std::size_t addBox(BoxInstance box);
    [[nodiscard]] bool updateBox(std::size_t index, BoxInstance box);
    [[nodiscard]] std::size_t size() const noexcept;
    bool setDepthState(DepthState state) noexcept;
    [[nodiscard]] DepthState depthState() const noexcept;
    [[nodiscard]] std::uint64_t rejectedBoxChanges() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;
    [[nodiscard]] InstanceBatch snapshot();

private:
    void ensureStorageWritable();
    void ensurePageWritable(std::size_t pageIndex);
    [[nodiscard]] const BoxInstance& boxAt(std::size_t index) const noexcept;
    void markDirty(std::size_t offset, std::size_t count);

    std::shared_ptr<detail::InstanceStorage> mStorage;
    DepthState mDepthState{};
    std::uint64_t mIdentity = 0;
    std::uint64_t mRevision = 0;
    std::size_t mPendingCopiedBoxCount = 0;
    std::uint64_t mPendingBuildNanoseconds = 0;
    std::uint64_t mRejectedBoxChanges = 0;
    std::string_view mLastError{};
    bool mDirty = true;
};

} // namespace henia::gfx
