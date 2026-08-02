#pragma once

#include <cstdint>

namespace henia::ui {

template <typename Tag>
class ResourceHandle final {
public:
    static constexpr std::uint32_t kMaxValue = 0xFFFFU;
    static constexpr std::uint32_t kMaxGeneration = 0xFFFFU;

    constexpr ResourceHandle() noexcept = default;
    // Single-value construction preserves source compatibility for first-
    // generation handles used by static fixtures and serialized defaults.
    explicit constexpr ResourceHandle(std::uint32_t value) noexcept
        : ResourceHandle(value, value == 0 ? 0U : 1U) {}
    constexpr ResourceHandle(std::uint32_t value, std::uint32_t generation) noexcept
        : mPacked(value <= kMaxValue && generation <= kMaxGeneration
                ? (generation << 16U) | value
                : 0U) {}

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value() != 0 && generation() != 0;
    }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return mPacked & kMaxValue;
    }
    [[nodiscard]] constexpr std::uint32_t generation() const noexcept {
        return mPacked >> 16U;
    }
    [[nodiscard]] constexpr std::uint32_t packed() const noexcept { return mPacked; }

    friend constexpr bool operator==(ResourceHandle, ResourceHandle) noexcept = default;

private:
    std::uint32_t mPacked = 0;
};

struct TextureTag;
struct FontTag;

using TextureHandle = ResourceHandle<TextureTag>;
using FontHandle    = ResourceHandle<FontTag>;

static_assert(sizeof(TextureHandle) == sizeof(std::uint32_t));
static_assert(sizeof(FontHandle) == sizeof(std::uint32_t));

} // namespace henia::ui
