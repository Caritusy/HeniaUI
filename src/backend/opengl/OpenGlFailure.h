#pragma once

#include "../FixedError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace henia::detail {

inline void assignGlFailure(
    FixedError& output,
    const char* category,
    std::uint32_t glError,
    const char* resource,
    std::uint64_t handle) noexcept {
    std::array<char, 256> message{};
    const int written = std::snprintf(
        message.data(),
        message.size(),
        "%s (%s=%llu, GL error=0x%04X)",
        category,
        resource,
        static_cast<unsigned long long>(handle),
        static_cast<unsigned int>(glError));
    const std::size_t size = written > 0
        ? std::min(static_cast<std::size_t>(written), message.size() - 1U)
        : 0U;
    output.assign(message.data(), size);
}

} // namespace henia::detail
