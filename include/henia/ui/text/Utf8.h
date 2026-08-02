#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace henia::ui {

struct Utf8Codepoint final {
    char32_t value = U'\uFFFD';
    std::size_t bytes = 0;
    bool valid = false;
};

[[nodiscard]] Utf8Codepoint decodeUtf8(std::string_view text, std::size_t offset) noexcept;
[[nodiscard]] bool validUtf8(std::string_view text) noexcept;
[[nodiscard]] std::string sanitizeUtf8(std::string_view text);
[[nodiscard]] bool appendUtf8(std::string& output, char32_t codepoint);
[[nodiscard]] bool isUtf8Boundary(std::string_view text, std::size_t offset) noexcept;
[[nodiscard]] std::size_t clampUtf8Boundary(
    std::string_view text,
    std::size_t offset) noexcept;
[[nodiscard]] std::size_t previousUtf8Boundary(
    std::string_view text,
    std::size_t offset) noexcept;
[[nodiscard]] std::size_t nextUtf8Boundary(
    std::string_view text,
    std::size_t offset) noexcept;

} // namespace henia::ui
