#pragma once

#include "henia/gfx/Types.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>

namespace henia::gfx {

class ShapeBatch3D;
namespace detail {
struct InstanceStorage;
}

struct DirtyRange final {
    std::size_t offset = 0;
    std::size_t count = 0;

    [[nodiscard]] constexpr bool operator==(const DirtyRange&) const noexcept = default;
};

// A segmented immutable view. Pages remain stable for the lifetime of the
// owning InstanceBatch; iteration and indexed reads never allocate or lock.
class BoxInstanceView final {
public:
    BoxInstanceView() = default;

    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = BoxInstance;
        using difference_type = std::ptrdiff_t;
        using pointer = const BoxInstance*;
        using reference = const BoxInstance&;

        [[nodiscard]] reference operator*() const noexcept;
        [[nodiscard]] pointer operator->() const noexcept;
        const_iterator& operator++() noexcept;
        const_iterator operator++(int) noexcept;
        [[nodiscard]] bool operator==(const const_iterator&) const noexcept = default;

    private:
        friend class BoxInstanceView;
        const detail::InstanceStorage* mStorage = nullptr;
        std::size_t mIndex = 0;
    };

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const BoxInstance& operator[](std::size_t index) const noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

private:
    friend class InstanceBatch;
    const detail::InstanceStorage* mStorage = nullptr;
};

class InstanceBatch final {
public:
    static constexpr std::size_t kBoxesPerPage = 256;

    [[nodiscard]] BoxInstanceView boxes() const noexcept;
    [[nodiscard]] std::size_t boxPageCount() const noexcept;
    [[nodiscard]] std::span<const BoxInstance> boxPage(std::size_t pageIndex) const noexcept;
    [[nodiscard]] std::span<const DirtyRange> dirtyRanges() const noexcept;
    [[nodiscard]] DepthState depthState() const noexcept;
    [[nodiscard]] std::uint64_t identity() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    // Compatibility summary spanning every dirty range. Upload backends use
    // dirtyRanges() so distant changes do not collapse into this outer bound.
    [[nodiscard]] std::size_t dirtyOffset() const noexcept;
    [[nodiscard]] std::size_t dirtyCount() const noexcept;
    [[nodiscard]] bool requiresFullUpload() const noexcept;
    [[nodiscard]] std::size_t copiedBoxCount() const noexcept;
    [[nodiscard]] std::size_t storageBytes() const noexcept;
    [[nodiscard]] std::uint64_t cpuBuildNanoseconds() const noexcept;

private:
    friend class ShapeBatch3D;
    std::shared_ptr<const detail::InstanceStorage> mStorage;
    DepthState mDepthState{};
    std::uint64_t mIdentity = 0;
    std::uint64_t mRevision = 0;
    std::size_t mDirtyRangeCount = 0;
    std::size_t mCopiedBoxCount = 0;
    std::uint64_t mCpuBuildNanoseconds = 0;
    bool mRequiresFullUpload = false;
};

} // namespace henia::gfx
