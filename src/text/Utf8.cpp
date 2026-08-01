#include "henia/ui/text/Utf8.h"

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

} // namespace henia::ui
