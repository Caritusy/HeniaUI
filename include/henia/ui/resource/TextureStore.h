#pragma once

#include "henia/ui/ResourceHandle.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace henia::ui {

enum class TextureFormat : std::uint8_t {
    Alpha8,
    Rgba8,
};

enum class TextureAlphaMode : std::uint8_t {
    // Resolves to AlphaMask for Alpha8 and Straight for Rgba8 at creation.
    FormatDefault,
    Straight,
    Premultiplied,
    Opaque,
    AlphaMask,
};

static_assert(static_cast<std::uint8_t>(TextureAlphaMode::Straight) == 1);
static_assert(static_cast<std::uint8_t>(TextureAlphaMode::Premultiplied) == 2);
static_assert(static_cast<std::uint8_t>(TextureAlphaMode::Opaque) == 3);
static_assert(static_cast<std::uint8_t>(TextureAlphaMode::AlphaMask) == 4);

enum class TextureColorSpace : std::uint8_t {
    Linear,
    Srgb,
};

enum class TextureBackingPolicy : std::uint8_t {
    Retained,
    DiscardAfterUpload,
    Regenerable,
    ExternalGpu,
};

struct TextureRegion final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool operator==(const TextureRegion&) const noexcept = default;
};

using TextureRegenerator = std::function<std::vector<std::byte>()>;

struct TextureCreateOptions final {
    TextureBackingPolicy backingPolicy = TextureBackingPolicy::Retained;
    TextureRegenerator regenerator;
    TextureAlphaMode alphaMode = TextureAlphaMode::FormatDefault;
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
};

struct TextureView final {
    TextureHandle handle{};
    TextureFormat format = TextureFormat::Rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    std::uint64_t revision = 0;
    TextureBackingPolicy backingPolicy = TextureBackingPolicy::Retained;
    // Always resolved: FormatDefault is never returned by TextureStore::view.
    TextureAlphaMode alphaMode = TextureAlphaMode::Straight;
    TextureColorSpace colorSpace = TextureColorSpace::Linear;
    TextureRegion dirtyRegion{};
    bool fullUpdate = true;
    bool backingAvailable = false;
    std::span<const std::byte> pixels{};
    // Tight previous contents for dirtyRegion, retained for transactional
    // rollback by a renderer exactly one revision behind.
    std::span<const std::byte> rollbackPixels{};
};

struct TextureStoreStatistics final {
    std::size_t activeTextures = 0;
    std::size_t slots = 0;
    std::size_t reusableSlots = 0;
    std::size_t cpuBackingBytes = 0;
    std::size_t discardedBackings = 0;
    std::size_t externalTextures = 0;
    std::uint64_t backingRestorations = 0;
    std::uint64_t backingRestorationFailures = 0;
};

class TextureStore final {
public:
    [[nodiscard]] TextureHandle create(
        TextureFormat format,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t rowPitch,
        std::span<const std::byte> pixels,
        TextureCreateOptions options = {});
    // Only alphaMode/colorSpace from options are used. External entries always
    // have ExternalGpu backing and never retain a regenerator.
    [[nodiscard]] TextureHandle createExternal(
        TextureFormat format,
        std::uint32_t width,
        std::uint32_t height,
        TextureCreateOptions options = {});
    [[nodiscard]] bool destroy(TextureHandle handle) noexcept;
    [[nodiscard]] bool update(
        TextureHandle handle,
        std::uint32_t rowPitch,
        std::span<const std::byte> pixels);
    [[nodiscard]] bool updateRegion(
        TextureHandle handle,
        TextureRegion region,
        std::uint32_t sourceRowPitch,
        std::span<const std::byte> pixels);
    // Discard is explicit so a host with multiple renderers can acknowledge
    // synchronization on every device/context before releasing CPU data.
    [[nodiscard]] bool discardCpuBacking(TextureHandle handle);
    [[nodiscard]] bool restoreCpuBacking(
        TextureHandle handle,
        std::uint32_t rowPitch,
        std::span<const std::byte> pixels);
    [[nodiscard]] bool ensureCpuBacking(TextureHandle handle);

    [[nodiscard]] TextureView view(TextureHandle handle) const noexcept;
    [[nodiscard]] TextureHandle handleAt(std::size_t slotIndex) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t slotCount() const noexcept;
    [[nodiscard]] TextureStoreStatistics statistics() const noexcept;

private:
    struct Entry final {
        TextureFormat format = TextureFormat::Rgba8;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t rowPitch = 0;
        std::uint64_t revision = 1;
        std::uint16_t generation = 1;
        TextureBackingPolicy backingPolicy = TextureBackingPolicy::Retained;
        TextureRegenerator regenerator;
        TextureAlphaMode alphaMode = TextureAlphaMode::Straight;
        TextureColorSpace colorSpace = TextureColorSpace::Linear;
        TextureRegion dirtyRegion{};
        std::uint32_t nextFree = std::numeric_limits<std::uint32_t>::max();
        bool fullUpdate = true;
        bool occupied = false;
        std::vector<std::byte> pixels;
        std::vector<std::byte> rollbackPixels;
    };

    [[nodiscard]] static std::size_t bytesPerPixel(TextureFormat format) noexcept;
    [[nodiscard]] TextureHandle allocate(Entry entry);
    [[nodiscard]] Entry* find(TextureHandle handle) noexcept;
    [[nodiscard]] const Entry* find(TextureHandle handle) const noexcept;

    std::vector<Entry> mEntries;
    std::uint32_t mFreeHead = std::numeric_limits<std::uint32_t>::max();
    std::size_t mActiveEntries = 0;
    std::size_t mReusableSlots = 0;
    std::uint64_t mBackingRestorations = 0;
    std::uint64_t mBackingRestorationFailures = 0;
};

} // namespace henia::ui
