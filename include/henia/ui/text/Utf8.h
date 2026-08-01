#pragma once

#include <cstddef>
#include <string_view>

namespace henia::ui {

struct Utf8Codepoint final {
    char32_t value = U'\uFFFD';
    std::size_t bytes = 0;
    bool valid = false;
};

[[nodiscard]] Utf8Codepoint decodeUtf8(std::string_view text, std::size_t offset) noexcept;

} // namespace henia::ui
