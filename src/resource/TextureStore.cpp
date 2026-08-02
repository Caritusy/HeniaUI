#include "henia/ui/resource/TextureStore.h"
#include "henia/CheckedArithmetic.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace henia::ui {
namespace {

constexpr std::uint32_t kInvalidSlot = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool validFormat(TextureFormat format) noexcept {
    return format == TextureFormat::Alpha8 || format == TextureFormat::Rgba8;
}

[[nodiscard]] std::size_t formatBytes(TextureFormat format) noexcept {
    return format == TextureFormat::Alpha8 ? 1U : 4U;
}

[[nodiscard]] bool validatePixels(
    TextureFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t rowPitch,
    std::span<const std::byte> pixels) noexcept {
    std::size_t minimumPitch = 0;
    std::size_t requiredBytes = 0;
    return validFormat(format) && width > 0 && height > 0
        && checkedMultiply(static_cast<std::size_t>(width), formatBytes(format), minimumPitch)
        && checkedMultiply(
            static_cast<std::size_t>(rowPitch),
            static_cast<std::size_t>(height),
            requiredBytes)
        && rowPitch >= minimumPitch && pixels.size() == requiredBytes;
}

} // namespace

TextureHandle TextureStore::create(
    TextureFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t rowPitch,
    std::span<const std::byte> pixels,
    TextureCreateOptions options) {
    if (!validatePixels(format, width, height, rowPitch, pixels)
        || options.backingPolicy == TextureBackingPolicy::ExternalGpu
        || (options.backingPolicy == TextureBackingPolicy::Regenerable
            && !options.regenerator)) {
        return {};
    }

    Entry entry{};
    entry.format = format;
    entry.width = width;
    entry.height = height;
    entry.rowPitch = rowPitch;
    entry.backingPolicy = options.backingPolicy;
    entry.regenerator = std::move(options.regenerator);
    entry.dirtyRegion = {0, 0, width, height};
    entry.fullUpdate = true;
    entry.occupied = true;
    entry.pixels.assign(pixels.begin(), pixels.end());
    return allocate(std::move(entry));
}

TextureHandle TextureStore::createExternal(
    TextureFormat format,
    std::uint32_t width,
    std::uint32_t height) {
    std::size_t pitch = 0;
    if (!validFormat(format) || width == 0 || height == 0
        || !checkedMultiply(static_cast<std::size_t>(width), bytesPerPixel(format), pitch)
        || pitch > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    Entry entry{};
    entry.format = format;
    entry.width = width;
    entry.height = height;
    entry.rowPitch = static_cast<std::uint32_t>(pitch);
    entry.backingPolicy = TextureBackingPolicy::ExternalGpu;
    entry.dirtyRegion = {0, 0, width, height};
    entry.fullUpdate = true;
    entry.occupied = true;
    return allocate(std::move(entry));
}

bool TextureStore::destroy(TextureHandle handle) noexcept {
    Entry* entry = find(handle);
    if (entry == nullptr) return false;
    const std::size_t slot = handle.value() - 1U;
    std::vector<std::byte>{}.swap(entry->pixels);
    std::vector<std::byte>{}.swap(entry->rollbackPixels);
    entry->regenerator = {};
    entry->occupied = false;
    entry->revision = 0;
    entry->width = 0;
    entry->height = 0;
    entry->rowPitch = 0;
    entry->dirtyRegion = {};
    entry->fullUpdate = true;
    --mActiveEntries;
    if (entry->generation != std::numeric_limits<std::uint32_t>::max()) {
        ++entry->generation;
        entry->nextFree = mFreeHead;
        mFreeHead = static_cast<std::uint32_t>(slot);
        ++mReusableSlots;
    } else {
        entry->generation = 0;
        entry->nextFree = kInvalidSlot;
    }
    return true;
}

bool TextureStore::update(
    TextureHandle handle,
    std::uint32_t rowPitch,
    std::span<const std::byte> pixels) {
    Entry* entry = find(handle);
    if (entry == nullptr || entry->backingPolicy == TextureBackingPolicy::ExternalGpu
        || entry->revision == std::numeric_limits<std::uint64_t>::max()
        || !validatePixels(entry->format, entry->width, entry->height, rowPitch, pixels)) {
        return false;
    }

    std::vector<std::byte> replacement(pixels.begin(), pixels.end());
    entry->pixels = std::move(replacement);
    entry->rollbackPixels.clear();
    entry->rowPitch = rowPitch;
    ++entry->revision;
    entry->dirtyRegion = {0, 0, entry->width, entry->height};
    entry->fullUpdate = true;
    return true;
}

bool TextureStore::updateRegion(
    TextureHandle handle,
    TextureRegion region,
    std::uint32_t sourceRowPitch,
    std::span<const std::byte> pixels) {
    Entry* entry = find(handle);
    if (entry == nullptr || entry->backingPolicy == TextureBackingPolicy::ExternalGpu
        || entry->revision == std::numeric_limits<std::uint64_t>::max()
        || region.width == 0 || region.height == 0
        || region.x > entry->width || region.width > entry->width - region.x
        || region.y > entry->height || region.height > entry->height - region.y) {
        return false;
    }
    if (entry->pixels.empty() && !ensureCpuBacking(handle)) return false;

    std::size_t copyBytes = 0;
    std::size_t requiredBytes = 0;
    if (!checkedMultiply(
            static_cast<std::size_t>(region.width),
            bytesPerPixel(entry->format),
            copyBytes)
        || !checkedMultiply(
            static_cast<std::size_t>(sourceRowPitch),
            static_cast<std::size_t>(region.height),
            requiredBytes)
        || sourceRowPitch < copyBytes || pixels.size() != requiredBytes) {
        return false;
    }

    std::vector<std::byte> rollback(copyBytes * region.height);
    const std::size_t pixelBytes = bytesPerPixel(entry->format);
    const std::size_t xOffset = static_cast<std::size_t>(region.x) * pixelBytes;
    for (std::uint32_t row = 0; row < region.height; ++row) {
        std::copy_n(
            entry->pixels.data()
                + static_cast<std::size_t>(region.y + row) * entry->rowPitch + xOffset,
            copyBytes,
            rollback.data() + static_cast<std::size_t>(row) * copyBytes);
        std::copy_n(
            pixels.data() + static_cast<std::size_t>(row) * sourceRowPitch,
            copyBytes,
            entry->pixels.data()
                + static_cast<std::size_t>(region.y + row) * entry->rowPitch + xOffset);
    }
    ++entry->revision;
    entry->rollbackPixels = std::move(rollback);
    entry->dirtyRegion = region;
    entry->fullUpdate = false;
    return true;
}

bool TextureStore::discardCpuBacking(TextureHandle handle) {
    Entry* entry = find(handle);
    if (entry == nullptr
        || (entry->backingPolicy != TextureBackingPolicy::DiscardAfterUpload
            && entry->backingPolicy != TextureBackingPolicy::Regenerable)) {
        return false;
    }
    std::vector<std::byte>{}.swap(entry->pixels);
    std::vector<std::byte>{}.swap(entry->rollbackPixels);
    return true;
}

bool TextureStore::restoreCpuBacking(
    TextureHandle handle,
    std::uint32_t rowPitch,
    std::span<const std::byte> pixels) {
    Entry* entry = find(handle);
    if (entry == nullptr || !entry->pixels.empty()
        || (entry->backingPolicy != TextureBackingPolicy::DiscardAfterUpload
            && entry->backingPolicy != TextureBackingPolicy::Regenerable)
        || !validatePixels(entry->format, entry->width, entry->height, rowPitch, pixels)) {
        return false;
    }
    std::vector<std::byte> replacement(pixels.begin(), pixels.end());
    entry->pixels = std::move(replacement);
    entry->rollbackPixels.clear();
    entry->rowPitch = rowPitch;
    return true;
}

bool TextureStore::ensureCpuBacking(TextureHandle handle) {
    Entry* entry = find(handle);
    if (entry == nullptr) return false;
    if (!entry->pixels.empty()) return true;
    if (entry->backingPolicy != TextureBackingPolicy::Regenerable || !entry->regenerator) {
        return false;
    }
    try {
        std::vector<std::byte> regenerated = entry->regenerator();
        if (!validatePixels(
                entry->format,
                entry->width,
                entry->height,
                entry->rowPitch,
                regenerated)) {
            ++mBackingRestorationFailures;
            return false;
        }
        entry->pixels = std::move(regenerated);
        ++mBackingRestorations;
        return true;
    } catch (...) {
        ++mBackingRestorationFailures;
        return false;
    }
}

TextureView TextureStore::view(TextureHandle handle) const noexcept {
    const Entry* entry = find(handle);
    if (entry == nullptr) return {};
    return {
        .handle = handle,
        .format = entry->format,
        .width = entry->width,
        .height = entry->height,
        .rowPitch = entry->rowPitch,
        .revision = entry->revision,
        .backingPolicy = entry->backingPolicy,
        .dirtyRegion = entry->dirtyRegion,
        .fullUpdate = entry->fullUpdate,
        .backingAvailable = !entry->pixels.empty(),
        .pixels = entry->pixels,
        .rollbackPixels = entry->rollbackPixels,
    };
}

TextureHandle TextureStore::handleAt(std::size_t slotIndex) const noexcept {
    if (slotIndex >= mEntries.size() || !mEntries[slotIndex].occupied) return {};
    return TextureHandle{
        static_cast<std::uint32_t>(slotIndex + 1U),
        mEntries[slotIndex].generation,
    };
}

std::size_t TextureStore::size() const noexcept { return mActiveEntries; }
std::size_t TextureStore::slotCount() const noexcept { return mEntries.size(); }

TextureStoreStatistics TextureStore::statistics() const noexcept {
    TextureStoreStatistics result{
        .activeTextures = mActiveEntries,
        .slots = mEntries.size(),
        .reusableSlots = mReusableSlots,
        .backingRestorations = mBackingRestorations,
        .backingRestorationFailures = mBackingRestorationFailures,
    };
    for (const Entry& entry : mEntries) {
        if (!entry.occupied) continue;
        result.cpuBackingBytes += entry.pixels.capacity() + entry.rollbackPixels.capacity();
        if (entry.backingPolicy == TextureBackingPolicy::ExternalGpu) {
            ++result.externalTextures;
        } else if (entry.pixels.empty()) {
            ++result.discardedBackings;
        }
    }
    return result;
}

std::size_t TextureStore::bytesPerPixel(TextureFormat format) noexcept {
    return formatBytes(format);
}

TextureHandle TextureStore::allocate(Entry entry) {
    if (mFreeHead != kInvalidSlot) {
        const std::uint32_t slot = mFreeHead;
        Entry& reused = mEntries[slot];
        mFreeHead = reused.nextFree;
        entry.generation = reused.generation;
        entry.nextFree = kInvalidSlot;
        reused = std::move(entry);
        --mReusableSlots;
        ++mActiveEntries;
        return TextureHandle{slot + 1U, reused.generation};
    }
    if (mEntries.size() >= std::numeric_limits<std::uint32_t>::max()) return {};
    entry.generation = 1;
    entry.nextFree = kInvalidSlot;
    mEntries.push_back(std::move(entry));
    ++mActiveEntries;
    return TextureHandle{static_cast<std::uint32_t>(mEntries.size()), 1};
}

TextureStore::Entry* TextureStore::find(TextureHandle handle) noexcept {
    if (!handle.valid() || handle.value() > mEntries.size()) return nullptr;
    Entry& entry = mEntries[handle.value() - 1U];
    return entry.occupied && entry.generation == handle.generation() ? &entry : nullptr;
}

const TextureStore::Entry* TextureStore::find(TextureHandle handle) const noexcept {
    if (!handle.valid() || handle.value() > mEntries.size()) return nullptr;
    const Entry& entry = mEntries[handle.value() - 1U];
    return entry.occupied && entry.generation == handle.generation() ? &entry : nullptr;
}

} // namespace henia::ui
