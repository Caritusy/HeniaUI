#pragma once

#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/DynamicGlyphAtlas.h"
#include "henia/ui/text/FontStore.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace henia::ui {

struct UnicodeRange final {
    char32_t first = U' ';
    char32_t last = U'~';
};

struct Win32FontRequest final {
    std::wstring_view family = L"Segoe UI";
    std::uint32_t pixelHeight = 18;
    std::uint32_t atlasWidth = 1024;
    std::uint32_t atlasHeight = 1024;
    std::span<const UnicodeRange> ranges{};
};

class Win32FontLoader final {
public:
    [[nodiscard]] static FontHandle load(
        TextureStore& textures,
        FontStore& fonts,
        const Win32FontRequest& request);
    // Rasterizes additional BMP glyphs into stable DynamicGlyphAtlas pages.
    // Existing packet UVs remain valid because pages never resize.
    [[nodiscard]] static bool appendGlyphs(
        DynamicGlyphAtlas& atlas,
        const Win32FontRequest& request,
        std::span<const char32_t> codepoints);
};

} // namespace henia::ui
