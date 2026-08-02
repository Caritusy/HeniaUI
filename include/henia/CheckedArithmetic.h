#pragma once

#include <concepts>
#include <limits>
#include <utility>

namespace henia {

template <std::unsigned_integral Value>
[[nodiscard]] constexpr bool checkedAdd(Value left, Value right, Value& result) noexcept {
    if (right > std::numeric_limits<Value>::max() - left) {
        return false;
    }
    result = static_cast<Value>(left + right);
    return true;
}

template <std::unsigned_integral Value>
[[nodiscard]] constexpr bool checkedMultiply(Value left, Value right, Value& result) noexcept {
    if (left != 0 && right > std::numeric_limits<Value>::max() / left) {
        return false;
    }
    result = static_cast<Value>(left * right);
    return true;
}

template <std::integral Destination, std::integral Source>
[[nodiscard]] constexpr bool checkedNarrow(Source value, Destination& result) noexcept {
    if (!std::in_range<Destination>(value)) {
        return false;
    }
    result = static_cast<Destination>(value);
    return true;
}

} // namespace henia
