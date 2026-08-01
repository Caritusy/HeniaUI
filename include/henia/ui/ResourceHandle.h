#pragma once

#include <cstdint>

namespace henia::ui {

template <typename Tag>
class ResourceHandle final {
public:
    constexpr ResourceHandle() noexcept = default;
    explicit constexpr ResourceHandle(std::uint32_t value) noexcept : mValue(value) {}

    [[nodiscard]] constexpr bool valid() const noexcept { return mValue != 0; }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return mValue; }

    friend constexpr bool operator==(ResourceHandle, ResourceHandle) noexcept = default;

private:
    std::uint32_t mValue = 0;
};

struct TextureTag;
struct FontTag;

using TextureHandle = ResourceHandle<TextureTag>;
using FontHandle    = ResourceHandle<FontTag>;

} // namespace henia::ui
