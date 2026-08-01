#include "henia/ui/resource/TextureStore.h"

#include <limits>

namespace henia::ui {

TextureHandle TextureStore::create(
    TextureFormat format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t rowPitch,
    std::span<const std::byte> pixels) {
    const std::size_t pixelBytes = bytesPerPixel(format);
    const std::size_t minimumPitch = static_cast<std::size_t>(width) * pixelBytes;
    const std::size_t requiredBytes = static_cast<std::size_t>(rowPitch) * height;
    if (width == 0 || height == 0 || rowPitch < minimumPitch || pixels.size() != requiredBytes
        || mEntries.size() == std::numeric_limits<std::uint32_t>::max() - 1ULL) {
        return {};
    }

    Entry entry{};
    entry.format = format;
    entry.width = width;
    entry.height = height;
    entry.rowPitch = rowPitch;
    entry.pixels.assign(pixels.begin(), pixels.end());
    mEntries.push_back(std::move(entry));
    return TextureHandle{static_cast<std::uint32_t>(mEntries.size())};
}

bool TextureStore::update(
    TextureHandle handle,
    std::uint32_t rowPitch,
    std::span<const std::byte> pixels) {
    Entry* entry = find(handle);
    if (entry == nullptr) {
        return false;
    }
    const std::size_t minimumPitch = static_cast<std::size_t>(entry->width) * bytesPerPixel(entry->format);
    const std::size_t requiredBytes = static_cast<std::size_t>(rowPitch) * entry->height;
    if (rowPitch < minimumPitch || pixels.size() != requiredBytes) {
        return false;
    }

    entry->rowPitch = rowPitch;
    entry->pixels.assign(pixels.begin(), pixels.end());
    ++entry->revision;
    return true;
}

TextureView TextureStore::view(TextureHandle handle) const noexcept {
    const Entry* entry = find(handle);
    if (entry == nullptr) {
        return {};
    }
    return {
        .handle = handle,
        .format = entry->format,
        .width = entry->width,
        .height = entry->height,
        .rowPitch = entry->rowPitch,
        .revision = entry->revision,
        .pixels = entry->pixels,
    };
}

std::size_t TextureStore::size() const noexcept { return mEntries.size(); }

std::size_t TextureStore::bytesPerPixel(TextureFormat format) noexcept {
    return format == TextureFormat::Alpha8 ? 1U : 4U;
}

TextureStore::Entry* TextureStore::find(TextureHandle handle) noexcept {
    if (!handle.valid() || handle.value() > mEntries.size()) {
        return nullptr;
    }
    return &mEntries[handle.value() - 1U];
}

const TextureStore::Entry* TextureStore::find(TextureHandle handle) const noexcept {
    if (!handle.valid() || handle.value() > mEntries.size()) {
        return nullptr;
    }
    return &mEntries[handle.value() - 1U];
}

} // namespace henia::ui
