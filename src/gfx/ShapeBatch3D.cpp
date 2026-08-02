#include "henia/gfx/ShapeBatch3D.h"
#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>

namespace henia::gfx {
namespace {

std::atomic_uint64_t gNextIdentity{1};

[[nodiscard]] std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

} // namespace

std::span<const BoxInstance> InstanceBatch::boxes() const noexcept {
    return mBoxes == nullptr ? std::span<const BoxInstance>{} : std::span<const BoxInstance>{*mBoxes};
}
DepthState InstanceBatch::depthState() const noexcept { return mDepthState; }
std::uint64_t InstanceBatch::identity() const noexcept { return mIdentity; }
std::uint64_t InstanceBatch::revision() const noexcept { return mRevision; }
std::size_t InstanceBatch::dirtyOffset() const noexcept { return mDirtyOffset; }
std::size_t InstanceBatch::dirtyCount() const noexcept { return mDirtyCount; }
std::uint64_t InstanceBatch::cpuBuildNanoseconds() const noexcept { return mCpuBuildNanoseconds; }

ShapeBatch3D::ShapeBatch3D()
    : mBoxes(std::make_shared<std::vector<BoxInstance>>()),
      mIdentity(gNextIdentity.fetch_add(1, std::memory_order_relaxed)) {}

void ShapeBatch3D::reserve(std::size_t capacity) {
    ensureWritable();
    mBoxes->reserve(capacity);
}

void ShapeBatch3D::clear() {
    if (mBoxes->empty()) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    ensureWritable();
    const std::size_t oldSize = mBoxes->size();
    mBoxes->clear();
    markDirty(0, oldSize);
    mPendingBuildNanoseconds += elapsedNanoseconds(started);
}

bool ShapeBatch3D::replaceBoxes(std::span<const BoxInstance> boxesValue) {
    for (const BoxInstance& box : boxesValue) {
        if (const std::string_view issue = validate(box); !issue.empty()) {
            ++mRejectedBoxChanges;
            mLastError = issue;
            return false;
        }
    }
    const auto started = std::chrono::steady_clock::now();
    ensureWritable();
    const std::size_t oldSize = mBoxes->size();
    mBoxes->assign(boxesValue.begin(), boxesValue.end());
    markDirty(0, std::max(oldSize, boxesValue.size()));
    mPendingBuildNanoseconds += elapsedNanoseconds(started);
    mLastError = {};
    return true;
}

std::size_t ShapeBatch3D::addBox(BoxInstance box) {
    if (const std::string_view issue = validate(box); !issue.empty()) {
        ++mRejectedBoxChanges;
        mLastError = issue;
        return kInvalidIndex;
    }
    const auto started = std::chrono::steady_clock::now();
    ensureWritable();
    const std::size_t index = mBoxes->size();
    mBoxes->push_back(std::move(box));
    markDirty(index, 1);
    mPendingBuildNanoseconds += elapsedNanoseconds(started);
    mLastError = {};
    return index;
}

bool ShapeBatch3D::updateBox(std::size_t index, BoxInstance box) {
    if (index >= mBoxes->size()) {
        ++mRejectedBoxChanges;
        mLastError = "box.index";
        return false;
    }
    if (const std::string_view issue = validate(box); !issue.empty()) {
        ++mRejectedBoxChanges;
        mLastError = issue;
        return false;
    }
    if ((*mBoxes)[index] == box) {
        mLastError = {};
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    ensureWritable();
    (*mBoxes)[index] = std::move(box);
    markDirty(index, 1);
    mPendingBuildNanoseconds += elapsedNanoseconds(started);
    mLastError = {};
    return true;
}

std::size_t ShapeBatch3D::size() const noexcept { return mBoxes->size(); }
bool ShapeBatch3D::setDepthState(DepthState state) noexcept {
    if (const std::string_view issue = validate(state); !issue.empty()) {
        ++mRejectedBoxChanges;
        mLastError = issue;
        return false;
    }
    mDepthState = state;
    mLastError = {};
    return true;
}
DepthState ShapeBatch3D::depthState() const noexcept { return mDepthState; }
std::uint64_t ShapeBatch3D::rejectedBoxChanges() const noexcept { return mRejectedBoxChanges; }
std::string_view ShapeBatch3D::lastError() const noexcept { return mLastError; }

InstanceBatch ShapeBatch3D::snapshot() {
    if (mDirty) {
        ++mRevision;
    }
    InstanceBatch result;
    result.mBoxes = mBoxes;
    result.mDepthState = mDepthState;
    result.mIdentity = mIdentity;
    result.mRevision = mRevision;
    result.mDirtyOffset = mDirty ? mDirtyOffset : 0;
    result.mDirtyCount = mDirty ? mDirtyEnd - mDirtyOffset : 0;
    result.mCpuBuildNanoseconds = mPendingBuildNanoseconds;
    mDirty = false;
    mDirtyOffset = 0;
    mDirtyEnd = 0;
    mPendingBuildNanoseconds = 0;
    return result;
}

void ShapeBatch3D::ensureWritable() {
    if (mBoxes.use_count() != 1) {
        mBoxes = std::make_shared<std::vector<BoxInstance>>(*mBoxes);
    }
}

void ShapeBatch3D::markDirty(std::size_t offset, std::size_t count) noexcept {
    std::size_t end = 0;
    if (!checkedAdd(offset, count, end)) {
        end = std::numeric_limits<std::size_t>::max();
    }
    if (!mDirty) {
        mDirtyOffset = offset;
        mDirtyEnd = end;
        mDirty = true;
        return;
    }
    mDirtyOffset = std::min(mDirtyOffset, offset);
    mDirtyEnd = std::max(mDirtyEnd, end);
}

} // namespace henia::gfx
