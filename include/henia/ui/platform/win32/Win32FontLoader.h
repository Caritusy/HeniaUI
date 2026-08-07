#pragma once

#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/DynamicGlyphAtlas.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"

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
    // Optional floating-point DirectWrite raster height. Zero preserves the
    // pixelHeight behavior. logicalPixelHeight keeps the public face size exact
    // when the physical raster is quantized independently from UI layout.
    float physicalPixelHeight = 0.0F;
    float logicalPixelHeight = 0.0F;
    // Zero disables cumulative per-glyph pixel alignment.
    float pixelAlignedMaximumPhysicalHeight = 20.0F;
};

struct Win32ScaledFontRequest final {
    std::wstring_view family = L"Segoe UI";
    float logicalPixelHeight = 18.0F;
    std::uint32_t atlasWidth = 1024;
    std::uint32_t atlasHeight = 1024;
    std::span<const UnicodeRange> ranges{};
    std::size_t maximumVariants = 32;
    // Physical sizes are cached as bounded fixed-point keys.
    std::uint32_t physicalSizeStepsPerPixel = 8;
    float pixelAlignedMaximumPhysicalHeight = 20.0F;
};

struct Win32FontScaleCacheStatistics final {
    std::size_t variants = 0;
    std::uint64_t cacheHits = 0;
    std::uint64_t cacheMisses = 0;
    std::uint64_t variantLimitFallbacks = 0;
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

// Retains one fixed-point physical raster per required height. A window moving
// back to a previously visited monitor reuses its stable FontHandle and atlas.
class Win32FontScaleCache final : public TextFontRasterResolver {
public:
    Win32FontScaleCache(
        TextureStore& textures,
        FontStore& fonts,
        const Win32ScaledFontRequest& request);

    [[nodiscard]] FontHandle select(float dpiScale);
    [[nodiscard]] FontHandle selectForDpi(std::uint32_t dpi);
    [[nodiscard]] FontHandle selectTextSize(float logicalPixelHeight, float dpiScale);
    [[nodiscard]] FontHandle resolveFont(
        FontHandle font,
        float logicalPixelSize) override;
    // Prewarms known UI typography sizes so later resolver calls stay on the
    // retained fast path without GDI work during an interactive frame.
    [[nodiscard]] bool prewarmTextSizes(
        std::span<const float> logicalPixelSizes,
        float dpiScale);
    [[nodiscard]] Win32FontScaleCacheStatistics statistics() const noexcept;

private:
    struct Variant final {
        std::uint64_t physicalSizeKey = 0;
        float physicalPixelHeight = 0.0F;
        float logicalPixelHeight = 0.0F;
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
    std::size_t mMaximumVariants = 32;
    std::uint32_t mPhysicalSizeStepsPerPixel = 8;
    float mPixelAlignedMaximumPhysicalHeight = 20.0F;
    std::uint64_t mCacheHits = 0;
    std::uint64_t mCacheMisses = 0;
    std::uint64_t mVariantLimitFallbacks = 0;
    float mDpiScale = 1.0F;
};

} // namespace henia::ui
