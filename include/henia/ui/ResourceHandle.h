#pragma once

#include <cstdint>

namespace henia::ui {

template <typename Tag>
class ResourceHandle final {
public:
    constexpr ResourceHandle() noexcept = default;
    // Single-value construction preserves source compatibility for first-
    // generation handles used by static fixtures and serialized defaults.
    explicit constexpr ResourceHandle(std::uint32_t value) noexcept
        : ResourceHandle(value, value == 0 ? 0U : 1U) {}
    constexpr ResourceHandle(std::uint32_t value, std::uint32_t generation) noexcept
        : mPacked((static_cast<std::uint64_t>(generation) << 32U) | value) {}

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value() != 0 && generation() != 0;
    }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept {
        return static_cast<std::uint32_t>(mPacked);
    }
    [[nodiscard]] constexpr std::uint32_t generation() const noexcept {
        return static_cast<std::uint32_t>(mPacked >> 32U);
    }
    [[nodiscard]] constexpr std::uint64_t packed() const noexcept { return mPacked; }

    friend constexpr bool operator==(ResourceHandle, ResourceHandle) noexcept = default;

private:
    std::uint64_t mPacked = 0;
};

struct TextureTag;
struct FontTag;

using TextureHandle = ResourceHandle<TextureTag>;
using FontHandle    = ResourceHandle<FontTag>;

} // namespace henia::ui
