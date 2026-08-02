#include "henia/ui/text/Utf8.h"

#include <algorithm>
#include <cstdint>

namespace henia::ui {
namespace {

[[nodiscard]] bool continuation(unsigned char value) noexcept { return (value & 0xC0U) == 0x80U; }

} // namespace

Utf8Codepoint decodeUtf8(std::string_view text, std::size_t offset) noexcept {
    if (offset >= text.size()) {
        return {.bytes = 0, .valid = false};
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const unsigned char first = bytes[offset];
    if (first <= 0x7FU) {
        return {.value = static_cast<char32_t>(first), .bytes = 1, .valid = true};
    }

    std::size_t length = 0;
    char32_t value = 0;
    char32_t minimum = 0;
    if ((first & 0xE0U) == 0xC0U) {
        length = 2;
        value = first & 0x1FU;
        minimum = 0x80;
    } else if ((first & 0xF0U) == 0xE0U) {
        length = 3;
        value = first & 0x0FU;
        minimum = 0x800;
    } else if ((first & 0xF8U) == 0xF0U) {
        length = 4;
        value = first & 0x07U;
        minimum = 0x10000;
    } else {
        return {.bytes = 1, .valid = false};
    }

    if (offset + length > text.size()) {
        return {.bytes = 1, .valid = false};
    }
    for (std::size_t index = 1; index < length; ++index) {
        const unsigned char next = bytes[offset + index];
        if (!continuation(next)) {
            return {.bytes = 1, .valid = false};
        }
        value = (value << 6U) | (next & 0x3FU);
    }

    if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return {.bytes = 1, .valid = false};
    }
    return {.value = value, .bytes = length, .valid = true};
}

bool validUtf8(std::string_view text) noexcept {
    for (std::size_t offset = 0; offset < text.size();) {
        const Utf8Codepoint decoded = decodeUtf8(text, offset);
        if (!decoded.valid || decoded.bytes == 0) return false;
        offset += decoded.bytes;
    }
    return true;
}

std::string sanitizeUtf8(std::string_view text) {
    std::string output;
    output.reserve(text.size());
    for (std::size_t offset = 0; offset < text.size();) {
        const Utf8Codepoint decoded = decodeUtf8(text, offset);
        if (decoded.bytes == 0) break;
        if (decoded.valid) {
            output.append(text.substr(offset, decoded.bytes));
        } else {
            static_cast<void>(appendUtf8(output, U'\uFFFD'));
        }
        offset += decoded.bytes;
    }
    return output;
}

bool appendUtf8(std::string& output, char32_t codepoint) {
    if (codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        return false;
    }
    if (codepoint <= 0x7FU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
    return true;
}

bool isUtf8Boundary(std::string_view text, std::size_t offset) noexcept {
    if (offset > text.size()) return false;
    return offset == 0 || offset == text.size()
        || !continuation(static_cast<unsigned char>(text[offset]));
}

std::size_t clampUtf8Boundary(
    std::string_view text,
    std::size_t offset) noexcept {
    offset = std::min(offset, text.size());
    while (offset > 0 && offset < text.size()
        && continuation(static_cast<unsigned char>(text[offset]))) {
        --offset;
    }
    return offset;
}

std::size_t previousUtf8Boundary(
    std::string_view text,
    std::size_t offset) noexcept {
    offset = clampUtf8Boundary(text, offset);
    if (offset == 0) return 0;
    --offset;
    while (offset > 0 && continuation(static_cast<unsigned char>(text[offset]))) {
        --offset;
    }
    return offset;
}

std::size_t nextUtf8Boundary(
    std::string_view text,
    std::size_t offset) noexcept {
    offset = clampUtf8Boundary(text, offset);
    if (offset >= text.size()) return text.size();
    const Utf8Codepoint decoded = decodeUtf8(text, offset);
    return std::min(text.size(), offset + std::max<std::size_t>(decoded.bytes, 1U));
}

} // namespace henia::ui
