#include "henia/ui/platform/win32/Win32FontLoader.h"

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace henia::ui {
namespace {

struct GdiObjects final {
    HDC context = nullptr;
    HFONT font = nullptr;
    HGDIOBJ previous = nullptr;

    ~GdiObjects() {
        if (context != nullptr && previous != nullptr) {
            SelectObject(context, previous);
        }
        if (font != nullptr) {
            DeleteObject(font);
        }
        if (context != nullptr) {
            DeleteDC(context);
        }
    }
};

[[nodiscard]] MAT2 identityMatrix() noexcept {
    MAT2 matrix{};
    matrix.eM11.value = 1;
    matrix.eM22.value = 1;
    return matrix;
}

[[nodiscard]] bool containsCodepoint(
    std::span<const UnicodeRange> ranges,
    char32_t codepoint) noexcept {
    for (const UnicodeRange range : ranges) {
        if (codepoint >= range.first && codepoint <= range.last) {
            return true;
        }
    }
    return false;
}

} // namespace

FontHandle Win32FontLoader::load(
    TextureStore& textures,
    FontStore& fonts,
    const Win32FontRequest& request) {
    if (request.family.empty() || request.pixelHeight == 0 || request.atlasWidth == 0
        || request.atlasHeight == 0 || request.ranges.empty()
        || !std::isfinite(request.metricsScale) || request.metricsScale <= 0.0F) {
        return {};
    }
    const float inverseMetricsScale = 1.0F / request.metricsScale;

    GdiObjects gdi{};
    gdi.context = CreateCompatibleDC(nullptr);
    if (gdi.context == nullptr) {
        return {};
    }

    const std::wstring family(request.family);
    gdi.font = CreateFontW(
        -static_cast<int>(request.pixelHeight),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        family.c_str());
    if (gdi.font == nullptr) {
        return {};
    }
    gdi.previous = SelectObject(gdi.context, gdi.font);
    if (gdi.previous == nullptr || gdi.previous == HGDI_ERROR) {
        return {};
    }

    TEXTMETRICW textMetrics{};
    if (!GetTextMetricsW(gdi.context, &textMetrics)) {
        return {};
    }

    const std::size_t atlasBytes = static_cast<std::size_t>(request.atlasWidth) * request.atlasHeight;
    std::vector<std::byte> atlas(atlasBytes, std::byte{0});
    std::vector<GlyphMetrics> glyphs;
    glyphs.reserve(256);

    std::uint32_t penX = 1;
    std::uint32_t penY = 1;
    std::uint32_t shelfHeight = 0;
    const MAT2 matrix = identityMatrix();

    for (const UnicodeRange range : request.ranges) {
        if (range.first > range.last || range.last > 0xFFFF) {
            return {};
        }
        for (char32_t codepoint = range.first; codepoint <= range.last; ++codepoint) {
            GLYPHMETRICS metrics{};
            const DWORD bitmapBytes = GetGlyphOutlineW(
                gdi.context,
                static_cast<UINT>(codepoint),
                GGO_GRAY8_BITMAP,
                &metrics,
                0,
                nullptr,
                &matrix);
            if (bitmapBytes == GDI_ERROR) {
                continue;
            }

            const std::uint32_t glyphWidth = metrics.gmBlackBoxX;
            const std::uint32_t glyphHeight = metrics.gmBlackBoxY;
            const std::uint32_t packedWidth = glyphWidth == 0 ? 1 : glyphWidth;
            const std::uint32_t packedHeight = glyphHeight == 0 ? 1 : glyphHeight;

            if (penX + packedWidth + 1 > request.atlasWidth) {
                penX = 1;
                penY += shelfHeight + 1;
                shelfHeight = 0;
            }
            if (penY + packedHeight + 1 > request.atlasHeight) {
                return {};
            }

            if (bitmapBytes != 0 && glyphWidth != 0 && glyphHeight != 0) {
                std::vector<std::byte> bitmap(bitmapBytes);
                if (GetGlyphOutlineW(
                        gdi.context,
                        static_cast<UINT>(codepoint),
                        GGO_GRAY8_BITMAP,
                        &metrics,
                        bitmapBytes,
                        bitmap.data(),
                        &matrix)
                    == GDI_ERROR) {
                    return {};
                }

                const std::uint32_t sourcePitch = (glyphWidth + 3U) & ~3U;
                for (std::uint32_t row = 0; row < glyphHeight; ++row) {
                    for (std::uint32_t column = 0; column < glyphWidth; ++column) {
                        const auto coverage = static_cast<unsigned char>(bitmap[
                            static_cast<std::size_t>(row) * sourcePitch + column]);
                        const auto normalized = static_cast<unsigned char>(
                            std::min(coverage, static_cast<unsigned char>(64U)) * 255U / 64U);
                        atlas[static_cast<std::size_t>(penY + row) * request.atlasWidth + penX + column]
                            = static_cast<std::byte>(normalized);
                    }
                }
            }

            WCHAR character = static_cast<WCHAR>(codepoint);
            WORD glyphIndex = 0;
            if (GetGlyphIndicesW(
                    gdi.context,
                    &character,
                    1,
                    &glyphIndex,
                    GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR
                || glyphIndex == 0xFFFFU) {
                glyphIndex = 0;
            }
            glyphs.push_back({
                .codepoint = codepoint,
                .uv = {
                    {
                        static_cast<float>(penX) / static_cast<float>(request.atlasWidth),
                        static_cast<float>(penY) / static_cast<float>(request.atlasHeight),
                    },
                    {
                        static_cast<float>(penX + glyphWidth) / static_cast<float>(request.atlasWidth),
                        static_cast<float>(penY + glyphHeight) / static_cast<float>(request.atlasHeight),
                    },
                },
                .size = {
                    static_cast<float>(glyphWidth) * inverseMetricsScale,
                    static_cast<float>(glyphHeight) * inverseMetricsScale,
                },
                .bearing = {
                    static_cast<float>(metrics.gmptGlyphOrigin.x) * inverseMetricsScale,
                    static_cast<float>(metrics.gmptGlyphOrigin.y) * inverseMetricsScale,
                },
                .advance = static_cast<float>(metrics.gmCellIncX) * inverseMetricsScale,
                .glyphId = glyphIndex,
            });

            penX += packedWidth + 1;
            shelfHeight = std::max(shelfHeight, packedHeight);
            if (codepoint == std::numeric_limits<char32_t>::max()) {
                break;
            }
        }
    }

    if (glyphs.empty()) {
        return {};
    }

    const TextureHandle atlasHandle = textures.create(
        TextureFormat::Alpha8,
        request.atlasWidth,
        request.atlasHeight,
        request.atlasWidth,
        atlas);
    if (!atlasHandle.valid()) {
        return {};
    }

    std::vector<KerningPair> kerning;
    const DWORD pairCount = GetKerningPairsW(gdi.context, 0, nullptr);
    if (pairCount != 0) {
        std::vector<KERNINGPAIR> pairs(pairCount);
        const DWORD loaded = GetKerningPairsW(gdi.context, pairCount, pairs.data());
        kerning.reserve(loaded);
        for (DWORD index = 0; index < loaded; ++index) {
            const KERNINGPAIR& pair = pairs[index];
            if (containsCodepoint(request.ranges, pair.wFirst)
                && containsCodepoint(request.ranges, pair.wSecond)
                && pair.iKernAmount != 0) {
                kerning.push_back({
                    static_cast<char32_t>(pair.wFirst),
                    static_cast<char32_t>(pair.wSecond),
                    static_cast<float>(pair.iKernAmount) * inverseMetricsScale,
                });
            }
        }
    }

    return fonts.add({
        .atlas = atlasHandle,
        .pixelSize = static_cast<float>(request.pixelHeight) * inverseMetricsScale,
        .ascent = static_cast<float>(textMetrics.tmAscent) * inverseMetricsScale,
        .descent = static_cast<float>(textMetrics.tmDescent) * inverseMetricsScale,
        .lineGap = static_cast<float>(textMetrics.tmExternalLeading) * inverseMetricsScale,
        .glyphs = std::move(glyphs),
        .kerning = std::move(kerning),
    });
}

bool Win32FontLoader::appendGlyphs(
    DynamicGlyphAtlas& atlas,
    const Win32FontRequest& request,
    std::span<const char32_t> codepoints) {
    if (request.family.empty() || request.pixelHeight == 0 || codepoints.empty()
        || !std::isfinite(request.metricsScale) || request.metricsScale <= 0.0F) {
        return false;
    }
    const float inverseMetricsScale = 1.0F / request.metricsScale;

    GdiObjects gdi{};
    gdi.context = CreateCompatibleDC(nullptr);
    if (gdi.context == nullptr) return false;
    const std::wstring family(request.family);
    gdi.font = CreateFontW(
        -static_cast<int>(request.pixelHeight),
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        family.c_str());
    if (gdi.font == nullptr) return false;
    gdi.previous = SelectObject(gdi.context, gdi.font);
    if (gdi.previous == nullptr || gdi.previous == HGDI_ERROR) return false;

    const MAT2 matrix = identityMatrix();
    std::vector<std::vector<std::byte>> pixelStorage;
    std::vector<RasterizedGlyph> rasterized;
    pixelStorage.reserve(codepoints.size());
    rasterized.reserve(codepoints.size());
    for (char32_t codepoint : codepoints) {
        if (codepoint == U'\0' || codepoint > 0xFFFFU
            || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            return false;
        }
        GLYPHMETRICS metrics{};
        const DWORD bitmapBytes = GetGlyphOutlineW(
            gdi.context,
            static_cast<UINT>(codepoint),
            GGO_GRAY8_BITMAP,
            &metrics,
            0,
            nullptr,
            &matrix);
        if (bitmapBytes == GDI_ERROR || bitmapBytes == 0
            || metrics.gmBlackBoxX == 0 || metrics.gmBlackBoxY == 0) {
            return false;
        }
        std::vector<std::byte> source(bitmapBytes);
        if (GetGlyphOutlineW(
                gdi.context,
                static_cast<UINT>(codepoint),
                GGO_GRAY8_BITMAP,
                &metrics,
                bitmapBytes,
                source.data(),
                &matrix) == GDI_ERROR) {
            return false;
        }
        const std::uint32_t width = metrics.gmBlackBoxX;
        const std::uint32_t height = metrics.gmBlackBoxY;
        const std::uint32_t sourcePitch = (width + 3U) & ~3U;
        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height);
        for (std::uint32_t row = 0; row < height; ++row) {
            for (std::uint32_t column = 0; column < width; ++column) {
                const auto coverage = static_cast<unsigned char>(source[
                    static_cast<std::size_t>(row) * sourcePitch + column]);
                pixels[static_cast<std::size_t>(row) * width + column] = static_cast<std::byte>(
                    std::min(coverage, static_cast<unsigned char>(64U)) * 255U / 64U);
            }
        }
        WCHAR character = static_cast<WCHAR>(codepoint);
        WORD glyphIndex = 0;
        if (GetGlyphIndicesW(gdi.context, &character, 1, &glyphIndex, GGI_MARK_NONEXISTING_GLYPHS)
                == GDI_ERROR
            || glyphIndex == 0xFFFFU) {
            return false;
        }
        pixelStorage.push_back(std::move(pixels));
        rasterized.push_back({
            .codepoint = codepoint,
            .glyphId = glyphIndex,
            .width = width,
            .height = height,
            .rowPitch = width,
            .bearing = {
                static_cast<float>(metrics.gmptGlyphOrigin.x) * inverseMetricsScale,
                static_cast<float>(metrics.gmptGlyphOrigin.y) * inverseMetricsScale,
            },
            .advance = static_cast<float>(metrics.gmCellIncX) * inverseMetricsScale,
            .pixels = pixelStorage.back(),
            .logicalSize = {
                static_cast<float>(width) * inverseMetricsScale,
                static_cast<float>(height) * inverseMetricsScale,
            },
        });
    }
    return atlas.add(rasterized);
}

Win32FontScaleCache::Win32FontScaleCache(
    TextureStore& textures,
    FontStore& fonts,
    const Win32ScaledFontRequest& request)
    : mTextures(&textures),
      mFonts(&fonts),
      mFamily(request.family),
      mLogicalPixelHeight(request.logicalPixelHeight),
      mAtlasWidth(request.atlasWidth),
      mAtlasHeight(request.atlasHeight),
      mRanges(request.ranges.begin(), request.ranges.end()) {}

FontHandle Win32FontScaleCache::select(float dpiScale) {
    if (!std::isfinite(dpiScale) || dpiScale <= 0.0F
        || !std::isfinite(mLogicalPixelHeight) || mLogicalPixelHeight <= 0.0F
        || mFamily.empty() || mAtlasWidth == 0 || mAtlasHeight == 0 || mRanges.empty()) {
        return {};
    }
    const double requestedHeight = static_cast<double>(mLogicalPixelHeight) * dpiScale;
    if (requestedHeight < 1.0 || requestedHeight > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    const auto pixelHeight = static_cast<std::uint32_t>(std::round(requestedHeight));
    for (const Variant& variant : mVariants) {
        if (variant.pixelHeight == pixelHeight && mFonts->find(variant.font) != nullptr) {
            ++mCacheHits;
            return variant.font;
        }
    }

    ++mCacheMisses;
    const float metricsScale = static_cast<float>(pixelHeight) / mLogicalPixelHeight;
    const auto scaledDimension = [metricsScale](std::uint32_t dimension) {
        const double scaled = std::ceil(static_cast<double>(dimension) * metricsScale);
        if (scaled < 1.0 || scaled > std::numeric_limits<std::uint32_t>::max()) {
            return std::uint32_t{0};
        }
        return static_cast<std::uint32_t>(scaled);
    };
    const std::uint32_t atlasWidth = scaledDimension(mAtlasWidth);
    const std::uint32_t atlasHeight = scaledDimension(mAtlasHeight);
    if (atlasWidth == 0 || atlasHeight == 0) return {};

    const FontHandle font = Win32FontLoader::load(
        *mTextures,
        *mFonts,
        {
            .family = mFamily,
            .pixelHeight = pixelHeight,
            .atlasWidth = atlasWidth,
            .atlasHeight = atlasHeight,
            .ranges = mRanges,
            .metricsScale = metricsScale,
        });
    if (!font.valid()) return {};
    mVariants.push_back({pixelHeight, font});
    return font;
}

FontHandle Win32FontScaleCache::selectForDpi(std::uint32_t dpi) {
    if (dpi == 0) return {};
    return select(static_cast<float>(dpi) / 96.0F);
}

Win32FontScaleCacheStatistics Win32FontScaleCache::statistics() const noexcept {
    return {
        .variants = mVariants.size(),
        .cacheHits = mCacheHits,
        .cacheMisses = mCacheMisses,
    };
}

} // namespace henia::ui
