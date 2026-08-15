#pragma once

#include <array>

namespace henia::detail {

template <typename Value>
[[nodiscard]] constexpr bool isPolygonMode(Value value) noexcept {
    return value == static_cast<Value>(0x1B00)
        || value == static_cast<Value>(0x1B01)
        || value == static_cast<Value>(0x1B02);
}

template <typename Value>
constexpr void normalizeCapturedPolygonModes(std::array<Value, 2>& modes) noexcept {
    if (isPolygonMode(modes[0]) && !isPolygonMode(modes[1])) {
        modes[1] = modes[0];
    }
}

} // namespace henia::detail
