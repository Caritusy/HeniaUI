#include "henia/ui/resource/TextureStore.h"
#include "henia/CheckedArithmetic.h"

#include <algorithm>
#include <array>
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

[[nodiscard]] bool resolveTextureSemantics(
    TextureFormat format,
    TextureAlphaMode requestedAlphaMode,
    TextureColorSpace colorSpace,
    TextureAlphaMode& resolvedAlphaMode) noexcept {
    if (colorSpace != TextureColorSpace::Linear && colorSpace != TextureColorSpace::Srgb) {
        return false;
    }
    if (format == TextureFormat::Alpha8) {
        if (requestedAlphaMode != TextureAlphaMode::FormatDefault
            && requestedAlphaMode != TextureAlphaMode::AlphaMask) {
            return false;
        }
        // Coverage is scalar data, never transfer-encoded color.
        if (colorSpace != TextureColorSpace::Linear) return false;
        resolvedAlphaMode = TextureAlphaMode::AlphaMask;
        return true;
    }
    if (format != TextureFormat::Rgba8 || requestedAlphaMode == TextureAlphaMode::AlphaMask) {
        return false;
    }
    resolvedAlphaMode = requestedAlphaMode == TextureAlphaMode::FormatDefault
        ? TextureAlphaMode::Straight
        : requestedAlphaMode;
    return resolvedAlphaMode == TextureAlphaMode::Straight
        || resolvedAlphaMode == TextureAlphaMode::Premultiplied
        || resolvedAlphaMode == TextureAlphaMode::Opaque;
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
    TextureAlphaMode alphaMode = TextureAlphaMode::Straight;
    if (!validatePixels(format, width, height, rowPitch, pixels)
        || !resolveTextureSemantics(format, options.alphaMode, options.colorSpace, alphaMode)
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
    entry.alphaMode = alphaMode;
    entry.colorSpace = options.colorSpace;
    entry.dirtyRegion = {0, 0, width, height};
    entry.fullUpdate = true;
    entry.occupied = true;
    entry.pixels.assign(pixels.begin(), pixels.end());
    return allocate(std::move(entry));
}

TextureHandle TextureStore::createExternal(
    TextureFormat format,
    std::uint32_t width,
    std::uint32_t height,
    TextureCreateOptions options) {
    std::size_t pitch = 0;
    TextureAlphaMode alphaMode = TextureAlphaMode::Straight;
    if (!validFormat(format) || width == 0 || height == 0
        || !resolveTextureSemantics(format, options.alphaMode, options.colorSpace, alphaMode)
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
    entry.alphaMode = alphaMode;
    entry.colorSpace = options.colorSpace;
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
    if (entry->generation != TextureHandle::kMaxGeneration) {
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
    const std::array updates{TextureRegionUpdate{handle, region, sourceRowPitch, pixels}};
    return updateRegions(updates);
}

PreparedTextureUpdate TextureStore::prepareRegionUpdates(
    std::span<const TextureRegionUpdate> updates) {
    PreparedTextureUpdate result;
    if (updates.empty()) return result;
    result.mEntries.reserve(updates.size());
    for (const TextureRegionUpdate& update : updates) {
        Entry* entry = find(update.handle);
        if (entry == nullptr || entry->backingPolicy == TextureBackingPolicy::ExternalGpu
            || entry->revision == std::numeric_limits<std::uint64_t>::max()
            || update.region.width == 0 || update.region.height == 0
            || update.region.x > entry->width
            || update.region.width > entry->width - update.region.x
            || update.region.y > entry->height
            || update.region.height > entry->height - update.region.y) {
            return {};
        }
        std::size_t copyBytes = 0;
        std::size_t requiredBytes = 0;
        if (!checkedMultiply(
                static_cast<std::size_t>(update.region.width),
                bytesPerPixel(entry->format),
                copyBytes)
            || !checkedMultiply(
                static_cast<std::size_t>(update.sourceRowPitch),
                static_cast<std::size_t>(update.region.height),
                requiredBytes)
            || update.sourceRowPitch < copyBytes || update.pixels.size() != requiredBytes) {
            return {};
        }

        auto staged = std::find_if(
            result.mEntries.begin(), result.mEntries.end(),
            [&update](const PreparedTextureUpdate::Entry& value) {
                return value.handle == update.handle;
            });
        if (staged == result.mEntries.end()) {
            std::vector<std::byte> stagedPixels;
            bool regeneratedBacking = false;
            if (entry->pixels.empty()) {
                if (!regenerateCpuBacking(update.handle, stagedPixels)) return {};
                entry = find(update.handle);
                if (entry == nullptr || entry->backingPolicy == TextureBackingPolicy::ExternalGpu
                    || entry->revision == std::numeric_limits<std::uint64_t>::max()
                    || update.region.x > entry->width
                    || update.region.width > entry->width - update.region.x
                    || update.region.y > entry->height
                    || update.region.height > entry->height - update.region.y) {
                    return {};
                }
                if (!entry->pixels.empty()) {
                    stagedPixels = entry->pixels;
                } else {
                    regeneratedBacking = true;
                }
            } else {
                stagedPixels = entry->pixels;
            }
            result.mEntries.push_back({
                .handle = update.handle,
                .expectedRevision = entry->revision,
                .dirtyRegion = update.region,
                .pixels = std::move(stagedPixels),
                .regeneratedBacking = regeneratedBacking,
            });
            staged = result.mEntries.end() - 1;
            if (regeneratedBacking) {
                staged->rollbackSourcePixels = staged->pixels;
            }
        } else {
            const std::uint32_t left = std::min(staged->dirtyRegion.x, update.region.x);
            const std::uint32_t top = std::min(staged->dirtyRegion.y, update.region.y);
            const std::uint32_t right = std::max(
                staged->dirtyRegion.x + staged->dirtyRegion.width,
                update.region.x + update.region.width);
            const std::uint32_t bottom = std::max(
                staged->dirtyRegion.y + staged->dirtyRegion.height,
                update.region.y + update.region.height);
            staged->dirtyRegion = {left, top, right - left, bottom - top};
        }
        ++staged->regionCount;
        const std::size_t xOffset = static_cast<std::size_t>(update.region.x)
            * bytesPerPixel(entry->format);
        for (std::uint32_t row = 0; row < update.region.height; ++row) {
            std::copy_n(
                update.pixels.data() + static_cast<std::size_t>(row) * update.sourceRowPitch,
                copyBytes,
                staged->pixels.data()
                    + static_cast<std::size_t>(update.region.y + row) * entry->rowPitch
                    + xOffset);
        }
    }

    for (PreparedTextureUpdate::Entry& staged : result.mEntries) {
        const Entry* entry = find(staged.handle);
        if (entry == nullptr || entry->revision != staged.expectedRevision) return {};
        std::size_t rowBytes = 0;
        std::size_t rollbackBytes = 0;
        if (!checkedMultiply(
                static_cast<std::size_t>(staged.dirtyRegion.width),
                bytesPerPixel(entry->format),
                rowBytes)
            || !checkedMultiply(
                rowBytes,
                static_cast<std::size_t>(staged.dirtyRegion.height),
                rollbackBytes)) {
            return {};
        }
        staged.rollbackPixels.resize(rollbackBytes);
        const std::size_t xOffset = static_cast<std::size_t>(staged.dirtyRegion.x)
            * bytesPerPixel(entry->format);
        const std::span<const std::byte> rollbackSource = staged.rollbackSourcePixels.empty()
            ? entry->pixels
            : std::span<const std::byte>(staged.rollbackSourcePixels);
        if (rollbackSource.empty()) return {};
        for (std::uint32_t row = 0; row < staged.dirtyRegion.height; ++row) {
            std::copy_n(
                rollbackSource.data()
                    + static_cast<std::size_t>(staged.dirtyRegion.y + row) * entry->rowPitch
                    + xOffset,
                rowBytes,
                staged.rollbackPixels.data() + static_cast<std::size_t>(row) * rowBytes);
        }
    }
    result.mReady = true;
    return result;
}

bool TextureStore::commit(PreparedTextureUpdate&& update) noexcept {
    if (!update.mReady || update.mEntries.empty()) return false;
    for (const PreparedTextureUpdate::Entry& staged : update.mEntries) {
        const Entry* entry = find(staged.handle);
        if (entry == nullptr || entry->revision != staged.expectedRevision) return false;
    }
    for (PreparedTextureUpdate::Entry& staged : update.mEntries) {
        Entry* entry = find(staged.handle);
        entry->pixels = std::move(staged.pixels);
        entry->rollbackPixels = std::move(staged.rollbackPixels);
        entry->dirtyRegion = staged.dirtyRegion;
        entry->fullUpdate = false;
        ++entry->revision;
        if (staged.regeneratedBacking) ++mBackingRestorations;
        mRegionUpdates += staged.regionCount;
        mCoalescedDirtyArea += static_cast<std::uint64_t>(staged.dirtyRegion.width)
            * staged.dirtyRegion.height;
    }
    update.mReady = false;
    return true;
}

bool TextureStore::updateRegions(std::span<const TextureRegionUpdate> updates) {
    PreparedTextureUpdate prepared = prepareRegionUpdates(updates);
    return prepared.valid() && commit(std::move(prepared));
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

bool TextureStore::regenerateCpuBacking(
    TextureHandle handle,
    std::vector<std::byte>& output) {
    Entry* entry = find(handle);
    if (entry == nullptr) return false;
    if (!entry->pixels.empty()) {
        output = entry->pixels;
        return true;
    }
    if (entry->backingPolicy != TextureBackingPolicy::Regenerable || !entry->regenerator) {
        return false;
    }

    const TextureFormat expectedFormat = entry->format;
    const std::uint32_t expectedWidth = entry->width;
    const std::uint32_t expectedHeight = entry->height;
    const std::uint32_t expectedRowPitch = entry->rowPitch;
    const std::uint64_t expectedRevision = entry->revision;
    try {
        TextureRegenerator regenerator = entry->regenerator;
        std::vector<std::byte> regenerated = regenerator();
        entry = find(handle);
        if (entry == nullptr) {
            return false;
        }
        // A reentrant update or explicit restoration is newer than the
        // callback result and remains authoritative.
        if (!entry->pixels.empty()) {
            output = entry->pixels;
            return true;
        }
        if (entry->backingPolicy != TextureBackingPolicy::Regenerable
            || entry->revision != expectedRevision
            || entry->format != expectedFormat
            || entry->width != expectedWidth
            || entry->height != expectedHeight
            || entry->rowPitch != expectedRowPitch) {
            return false;
        }
        if (!validatePixels(
                expectedFormat,
                expectedWidth,
                expectedHeight,
                expectedRowPitch,
                regenerated)) {
            return false;
        }
        output = std::move(regenerated);
        return true;
    } catch (...) {
        return false;
    }
}

bool TextureStore::ensureCpuBacking(TextureHandle handle) {
    Entry* entry = find(handle);
    if (entry == nullptr) return false;
    if (!entry->pixels.empty()) return true;
    if (entry->backingPolicy != TextureBackingPolicy::Regenerable || !entry->regenerator) {
        return false;
    }
    std::vector<std::byte> regenerated;
    if (!regenerateCpuBacking(handle, regenerated)) {
        ++mBackingRestorationFailures;
        return false;
    }
    entry = find(handle);
    if (entry == nullptr) {
        ++mBackingRestorationFailures;
        return false;
    }
    if (!entry->pixels.empty()) return true;
    entry->pixels = std::move(regenerated);
    ++mBackingRestorations;
    return true;
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
        .alphaMode = entry->alphaMode,
        .colorSpace = entry->colorSpace,
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
        .regionUpdates = mRegionUpdates,
        .coalescedDirtyArea = mCoalescedDirtyArea,
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
    if (mEntries.size() >= TextureHandle::kMaxValue) return {};
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
