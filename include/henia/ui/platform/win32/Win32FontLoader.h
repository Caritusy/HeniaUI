#pragma once

#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/DynamicGlyphAtlas.h"
#include "henia/ui/text/FontStore.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    // Physical raster pixels per logical UI unit. Metrics exposed through the
    // FontStore are divided by this value while atlas texels remain physical.
    float metricsScale = 1.0F;
};

struct Win32ScaledFontRequest final {
    std::wstring_view family = L"Segoe UI";
    float logicalPixelHeight = 18.0F;
    std::uint32_t atlasWidth = 1024;
    std::uint32_t atlasHeight = 1024;
    std::span<const UnicodeRange> ranges{};
};

struct Win32FontScaleCacheStatistics final {
    std::size_t variants = 0;
    std::uint64_t cacheHits = 0;
    std::uint64_t cacheMisses = 0;
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

// Retains one physical raster per required pixel height. A window moving back
// to a previously visited monitor reuses its stable FontHandle and atlas.
class Win32FontScaleCache final {
public:
    Win32FontScaleCache(
        TextureStore& textures,
        FontStore& fonts,
        const Win32ScaledFontRequest& request);

    [[nodiscard]] FontHandle select(float dpiScale);
    [[nodiscard]] FontHandle selectForDpi(std::uint32_t dpi);
    [[nodiscard]] Win32FontScaleCacheStatistics statistics() const noexcept;

private:
    struct Variant final {
        std::uint32_t pixelHeight = 0;
        FontHandle font{};
    };

    TextureStore* mTextures = nullptr;
    FontStore* mFonts = nullptr;
    std::wstring mFamily;
    float mLogicalPixelHeight = 0.0F;
    std::uint32_t mAtlasWidth = 0;
    std::uint32_t mAtlasHeight = 0;
    std::vector<UnicodeRange> mRanges;
    std::vector<Variant> mVariants;
    std::uint64_t mCacheHits = 0;
    std::uint64_t mCacheMisses = 0;
};

} // namespace henia::ui
