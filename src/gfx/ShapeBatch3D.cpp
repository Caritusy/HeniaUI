#include "henia/gfx/ShapeBatch3D.h"
#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace henia::gfx {
namespace detail {

struct InstancePage final {
    static_assert(std::is_trivially_copyable_v<BoxInstance>);
    static_assert(std::is_trivially_destructible_v<BoxInstance>);

    InstancePage() noexcept {}
    InstancePage(const InstancePage& other) noexcept : count(other.count) {
        std::memcpy(storage.data(), other.storage.data(), count * sizeof(BoxInstance));
    }

    [[nodiscard]] BoxInstance* data() noexcept {
        return reinterpret_cast<BoxInstance*>(storage.data());
    }
    [[nodiscard]] const BoxInstance* data() const noexcept {
        return reinterpret_cast<const BoxInstance*>(storage.data());
    }

    alignas(BoxInstance) std::array<
        std::byte,
        sizeof(BoxInstance) * InstanceBatch::kBoxesPerPage> storage;
    std::size_t count = 0;
};

inline constexpr std::size_t kPagesPerSlab = 32;

struct InstancePageSlab final {
    std::array<InstancePage, kPagesPerSlab> pages{};
};

struct InstanceStorage final {
    std::vector<std::shared_ptr<InstancePage>> pages;
    std::vector<std::uint8_t> writablePages;
    std::vector<std::uint8_t> pageUsesSlab;
    std::vector<DirtyRange> dirtyRanges;
    std::size_t size = 0;
    std::size_t allocatedPageCapacity = 0;
    bool fullUploadRequired = true;
};

} // namespace detail
namespace {

std::atomic_uint64_t gNextIdentity{1};

[[nodiscard]] std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

[[nodiscard]] const BoxInstance& storageBoxAt(
    const detail::InstanceStorage& storage,
    std::size_t index) noexcept {
    const std::size_t pageIndex = index / InstanceBatch::kBoxesPerPage;
    const std::size_t localIndex = index % InstanceBatch::kBoxesPerPage;
    return storage.pages[pageIndex]->data()[localIndex];
}

void addSaturated(std::size_t value, std::size_t& total) noexcept {
    total = value > std::numeric_limits<std::size_t>::max() - total
        ? std::numeric_limits<std::size_t>::max()
        : total + value;
}

} // namespace

BoxInstanceView::const_iterator::reference BoxInstanceView::const_iterator::operator*() const noexcept {
    return storageBoxAt(*mStorage, mIndex);
}

BoxInstanceView::const_iterator::pointer BoxInstanceView::const_iterator::operator->() const noexcept {
    return &operator*();
}

BoxInstanceView::const_iterator& BoxInstanceView::const_iterator::operator++() noexcept {
    ++mIndex;
    return *this;
}

BoxInstanceView::const_iterator BoxInstanceView::const_iterator::operator++(int) noexcept {
    const const_iterator previous = *this;
    ++*this;
    return previous;
}

std::size_t BoxInstanceView::size() const noexcept {
    return mStorage == nullptr ? 0 : mStorage->size;
}

bool BoxInstanceView::empty() const noexcept { return size() == 0; }

const BoxInstance& BoxInstanceView::operator[](std::size_t index) const noexcept {
    return storageBoxAt(*mStorage, index);
}

BoxInstanceView::const_iterator BoxInstanceView::begin() const noexcept {
    const_iterator result;
    result.mStorage = mStorage;
    return result;
}

BoxInstanceView::const_iterator BoxInstanceView::end() const noexcept {
    const_iterator result;
    result.mStorage = mStorage;
    result.mIndex = size();
    return result;
}

BoxInstanceView InstanceBatch::boxes() const noexcept {
    BoxInstanceView result;
    result.mStorage = mStorage.get();
    return result;
}

std::size_t InstanceBatch::boxPageCount() const noexcept {
    return mStorage == nullptr ? 0 : mStorage->pages.size();
}

std::span<const BoxInstance> InstanceBatch::boxPage(std::size_t pageIndex) const noexcept {
    if (mStorage == nullptr || pageIndex >= mStorage->pages.size()) return {};
    const detail::InstancePage& page = *mStorage->pages[pageIndex];
    return {page.data(), page.count};
}

std::span<const DirtyRange> InstanceBatch::dirtyRanges() const noexcept {
    if (mStorage == nullptr || mDirtyRangeCount == 0) return {};
    return {mStorage->dirtyRanges.data(), mDirtyRangeCount};
}

DepthState InstanceBatch::depthState() const noexcept { return mDepthState; }
std::uint64_t InstanceBatch::identity() const noexcept { return mIdentity; }
std::uint64_t InstanceBatch::revision() const noexcept { return mRevision; }

std::size_t InstanceBatch::dirtyOffset() const noexcept {
    const std::span<const DirtyRange> ranges = dirtyRanges();
    return ranges.empty() ? 0 : ranges.front().offset;
}

std::size_t InstanceBatch::dirtyCount() const noexcept {
    const std::span<const DirtyRange> ranges = dirtyRanges();
    if (ranges.empty()) return 0;
    std::size_t end = 0;
    if (!checkedAdd(ranges.back().offset, ranges.back().count, end)) {
        return std::numeric_limits<std::size_t>::max() - ranges.front().offset;
    }
    return end - ranges.front().offset;
}

bool InstanceBatch::requiresFullUpload() const noexcept { return mRequiresFullUpload; }
std::size_t InstanceBatch::copiedBoxCount() const noexcept { return mCopiedBoxCount; }

std::size_t InstanceBatch::storageBytes() const noexcept {
    if (mStorage == nullptr) return 0;
    std::size_t total = sizeof(detail::InstanceStorage);
    const auto addProduct = [&total](std::size_t count, std::size_t size) noexcept {
        std::size_t bytes = 0;
        if (!checkedMultiply(count, size, bytes)) {
            total = std::numeric_limits<std::size_t>::max();
        } else {
            addSaturated(bytes, total);
        }
    };
    addProduct(mStorage->pages.capacity(), sizeof(std::shared_ptr<detail::InstancePage>));
    addProduct(mStorage->writablePages.capacity(), sizeof(std::uint8_t));
    addProduct(mStorage->pageUsesSlab.capacity(), sizeof(std::uint8_t));
    addProduct(mStorage->allocatedPageCapacity, sizeof(detail::InstancePage));
    addProduct(mStorage->dirtyRanges.capacity(), sizeof(DirtyRange));
    return total;
}

std::uint64_t InstanceBatch::cpuBuildNanoseconds() const noexcept { return mCpuBuildNanoseconds; }

ShapeBatch3D::ShapeBatch3D()
    : mStorage(std::make_shared<detail::InstanceStorage>()),
      mIdentity(gNextIdentity.fetch_add(1, std::memory_order_relaxed)) {}

void ShapeBatch3D::reserve(std::size_t capacity) {
    ensureStorageWritable();
    const std::size_t pageCapacity = capacity / InstanceBatch::kBoxesPerPage
        + (capacity % InstanceBatch::kBoxesPerPage == 0 ? 0U : 1U);
    mStorage->pages.reserve(pageCapacity);
    mStorage->writablePages.reserve(pageCapacity);
    mStorage->pageUsesSlab.reserve(pageCapacity);
    mStorage->dirtyRanges.reserve(pageCapacity);
}

void ShapeBatch3D::clear() {
    if (mStorage->size == 0) return;
    const auto started = std::chrono::steady_clock::now();
    ensureStorageWritable();
    const std::size_t oldSize = mStorage->size;
    markDirty(0, oldSize);
    mStorage->pages.clear();
    mStorage->writablePages.clear();
    mStorage->pageUsesSlab.clear();
    mStorage->size = 0;
    mStorage->allocatedPageCapacity = 0;
    mStorage->fullUploadRequired = true;
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
    const bool reuseEmptyStorage = mStorage.use_count() == 1 && mStorage->size == 0;
    auto replacement = reuseEmptyStorage
        ? mStorage
        : std::make_shared<detail::InstanceStorage>();
    replacement->pages.clear();
    replacement->writablePages.clear();
    replacement->pageUsesSlab.clear();
    replacement->dirtyRanges.clear();
    replacement->allocatedPageCapacity = 0;
    const std::size_t pageCount = boxesValue.size() / InstanceBatch::kBoxesPerPage
        + (boxesValue.size() % InstanceBatch::kBoxesPerPage == 0 ? 0U : 1U);
    try {
        replacement->pages.reserve(pageCount);
        replacement->writablePages.reserve(pageCount);
        replacement->pageUsesSlab.reserve(pageCount);
        replacement->dirtyRanges.reserve(std::max<std::size_t>(pageCount, 1));
        std::size_t pageIndex = 0;
        while (pageCount - pageIndex >= detail::kPagesPerSlab) {
            auto slab = std::make_shared<detail::InstancePageSlab>();
            for (std::size_t slabIndex = 0; slabIndex < detail::kPagesPerSlab; ++slabIndex) {
                detail::InstancePage& page = slab->pages[slabIndex];
                const std::size_t offset = pageIndex * InstanceBatch::kBoxesPerPage;
                page.count = InstanceBatch::kBoxesPerPage;
                std::memcpy(
                    page.data(),
                    boxesValue.data() + offset,
                    page.count * sizeof(BoxInstance));
                replacement->pages.emplace_back(slab, &page);
                replacement->writablePages.push_back(1);
                replacement->pageUsesSlab.push_back(1);
                ++pageIndex;
            }
            replacement->allocatedPageCapacity += detail::kPagesPerSlab;
        }
        while (pageIndex < pageCount) {
            auto page = std::make_shared<detail::InstancePage>();
            const std::size_t offset = pageIndex * InstanceBatch::kBoxesPerPage;
            page->count = std::min(InstanceBatch::kBoxesPerPage, boxesValue.size() - offset);
            std::memcpy(
                page->data(),
                boxesValue.data() + offset,
                page->count * sizeof(BoxInstance));
            replacement->pages.push_back(std::move(page));
            replacement->writablePages.push_back(1);
            replacement->pageUsesSlab.push_back(0);
            ++replacement->allocatedPageCapacity;
            ++pageIndex;
        }
    } catch (...) {
        if (reuseEmptyStorage) {
            replacement->pages.clear();
            replacement->writablePages.clear();
            replacement->pageUsesSlab.clear();
            replacement->dirtyRanges.clear();
            replacement->allocatedPageCapacity = 0;
        }
        throw;
    }
    replacement->size = boxesValue.size();
    replacement->fullUploadRequired = true;
    const std::size_t dirtyCount = std::max(mStorage->size, boxesValue.size());
    if (dirtyCount > 0) replacement->dirtyRanges.push_back({0, dirtyCount});
    mStorage = std::move(replacement);
    addSaturated(boxesValue.size(), mPendingCopiedBoxCount);
    mDirty = true;
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
    ensureStorageWritable();
    const std::size_t index = mStorage->size;
    const std::size_t pageIndex = index / InstanceBatch::kBoxesPerPage;
    const std::size_t localIndex = index % InstanceBatch::kBoxesPerPage;
    if (pageIndex == mStorage->pages.size()) {
        auto page = std::make_shared<detail::InstancePage>();
        const std::size_t requiredCapacity = mStorage->pages.size() + 1U;
        if (mStorage->pages.capacity() < requiredCapacity
            || mStorage->writablePages.capacity() < requiredCapacity
            || mStorage->pageUsesSlab.capacity() < requiredCapacity) {
            const std::size_t oldCapacity = std::max({
                mStorage->pages.capacity(),
                mStorage->writablePages.capacity(),
                mStorage->pageUsesSlab.capacity(),
            });
            const std::size_t newCapacity = oldCapacity == 0 ? 1U
                : oldCapacity > std::numeric_limits<std::size_t>::max() / 2U
                ? std::numeric_limits<std::size_t>::max()
                : oldCapacity * 2U;
            const std::size_t targetCapacity = std::max(requiredCapacity, newCapacity);
            mStorage->pages.reserve(targetCapacity);
            mStorage->writablePages.reserve(targetCapacity);
            mStorage->pageUsesSlab.reserve(targetCapacity);
        }
        markDirty(index, 1);
        mStorage->pages.push_back(std::move(page));
        mStorage->writablePages.push_back(1);
        mStorage->pageUsesSlab.push_back(0);
        ++mStorage->allocatedPageCapacity;
    } else {
        ensurePageWritable(pageIndex);
        markDirty(index, 1);
    }
    detail::InstancePage& page = *mStorage->pages[pageIndex];
    std::memcpy(page.data() + localIndex, &box, sizeof(BoxInstance));
    page.count = std::max(page.count, localIndex + 1U);
    ++mStorage->size;
    mPendingBuildNanoseconds += elapsedNanoseconds(started);
    mLastError = {};
    return index;
}

bool ShapeBatch3D::updateBox(std::size_t index, BoxInstance box) {
    if (index >= mStorage->size) {
        ++mRejectedBoxChanges;
        mLastError = "box.index";
        return false;
    }
    if (const std::string_view issue = validate(box); !issue.empty()) {
        ++mRejectedBoxChanges;
        mLastError = issue;
        return false;
    }
    if (boxAt(index) == box) {
        mLastError = {};
        return false;
    }

    const auto started = std::chrono::steady_clock::now();
    ensureStorageWritable();
    const std::size_t pageIndex = index / InstanceBatch::kBoxesPerPage;
    ensurePageWritable(pageIndex);
    markDirty(index, 1);
    mStorage->pages[pageIndex]->data()[index % InstanceBatch::kBoxesPerPage] = std::move(box);
    mPendingBuildNanoseconds += elapsedNanoseconds(started);
    mLastError = {};
    return true;
}

std::size_t ShapeBatch3D::size() const noexcept { return mStorage->size; }

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
    if (mDirty) ++mRevision;
    InstanceBatch result;
    result.mStorage = mStorage;
    result.mDepthState = mDepthState;
    result.mIdentity = mIdentity;
    result.mRevision = mRevision;
    result.mDirtyRangeCount = mDirty ? mStorage->dirtyRanges.size() : 0;
    result.mCopiedBoxCount = mPendingCopiedBoxCount;
    result.mCpuBuildNanoseconds = mPendingBuildNanoseconds;
    result.mRequiresFullUpload = mDirty && mStorage->fullUploadRequired;
    mDirty = false;
    mPendingCopiedBoxCount = 0;
    mPendingBuildNanoseconds = 0;
    return result;
}

void ShapeBatch3D::ensureStorageWritable() {
    if (mStorage.use_count() == 1) return;
    auto writable = std::make_shared<detail::InstanceStorage>();
    writable->pages = mStorage->pages;
    writable->pages.reserve(mStorage->pages.capacity());
    writable->writablePages.assign(mStorage->pages.size(), 0);
    writable->writablePages.reserve(mStorage->writablePages.capacity());
    writable->pageUsesSlab = mStorage->pageUsesSlab;
    writable->pageUsesSlab.reserve(mStorage->pageUsesSlab.capacity());
    writable->dirtyRanges.reserve(std::max(
        mStorage->dirtyRanges.capacity(),
        mStorage->pages.capacity()));
    writable->size = mStorage->size;
    writable->allocatedPageCapacity = mStorage->allocatedPageCapacity;
    writable->fullUploadRequired = false;
    mStorage = std::move(writable);
}

void ShapeBatch3D::ensurePageWritable(std::size_t pageIndex) {
    if (mStorage->writablePages[pageIndex] != 0) return;
    std::shared_ptr<detail::InstancePage>& page = mStorage->pages[pageIndex];
    page = std::make_shared<detail::InstancePage>(*page);
    mStorage->writablePages[pageIndex] = 1;
    if (mStorage->pageUsesSlab[pageIndex] != 0) {
        mStorage->pageUsesSlab[pageIndex] = 0;
        ++mStorage->allocatedPageCapacity;
        const std::size_t slabStart = pageIndex / detail::kPagesPerSlab * detail::kPagesPerSlab;
        const std::size_t slabEnd = std::min(
            slabStart + detail::kPagesPerSlab,
            mStorage->pageUsesSlab.size());
        const bool slabStillUsed = std::any_of(
            mStorage->pageUsesSlab.begin() + static_cast<std::ptrdiff_t>(slabStart),
            mStorage->pageUsesSlab.begin() + static_cast<std::ptrdiff_t>(slabEnd),
            [](std::uint8_t used) noexcept { return used != 0; });
        if (!slabStillUsed) mStorage->allocatedPageCapacity -= detail::kPagesPerSlab;
    }
    addSaturated(InstanceBatch::kBoxesPerPage, mPendingCopiedBoxCount);
}

const BoxInstance& ShapeBatch3D::boxAt(std::size_t index) const noexcept {
    return storageBoxAt(*mStorage, index);
}

void ShapeBatch3D::markDirty(std::size_t offset, std::size_t count) {
    if (count == 0) return;
    std::size_t mergedEnd = 0;
    if (!checkedAdd(offset, count, mergedEnd)) {
        throw std::length_error("ShapeBatch3D dirty range overflow");
    }

    DirtyRange merged{offset, count};
    auto first = mStorage->dirtyRanges.begin();
    while (first != mStorage->dirtyRanges.end()) {
        std::size_t existingEnd = 0;
        if (!checkedAdd(first->offset, first->count, existingEnd) || existingEnd >= merged.offset) {
            break;
        }
        ++first;
    }

    auto last = first;
    while (last != mStorage->dirtyRanges.end() && last->offset <= mergedEnd) {
        std::size_t existingEnd = 0;
        if (!checkedAdd(last->offset, last->count, existingEnd)) {
            existingEnd = std::numeric_limits<std::size_t>::max();
        }
        merged.offset = std::min(merged.offset, last->offset);
        mergedEnd = std::max(mergedEnd, existingEnd);
        ++last;
    }
    merged.count = mergedEnd - merged.offset;
    if (first == last) {
        mStorage->dirtyRanges.insert(first, merged);
    } else {
        *first = merged;
        mStorage->dirtyRanges.erase(first + 1, last);
    }
    mDirty = true;
}

} // namespace henia::gfx
