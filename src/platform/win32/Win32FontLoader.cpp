#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/CheckedArithmetic.h"

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

struct TextureRollback final {
    TextureStore* textures = nullptr;
    TextureHandle handle{};

    ~TextureRollback() {
        if (textures != nullptr && handle.valid()) {
            static_cast<void>(textures->destroy(handle));
        }
    }
    void release() noexcept { handle = {}; }
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
        || request.pixelHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
        || !std::isfinite(request.metricsScale) || request.metricsScale <= 0.0F) {
        return {};
    }
    std::size_t atlasBytes = 0;
    if (!checkedMultiply(
            static_cast<std::size_t>(request.atlasWidth),
            static_cast<std::size_t>(request.atlasHeight),
            atlasBytes)
        || atlasBytes > std::vector<std::byte>{}.max_size()) {
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
            std::uint32_t widthWithPadding = 0;
            std::uint32_t heightWithPadding = 0;
            if (!checkedAdd(packedWidth, std::uint32_t{1}, widthWithPadding)
                || !checkedAdd(packedHeight, std::uint32_t{1}, heightWithPadding)) {
                return {};
            }

            if (penX > request.atlasWidth
                || widthWithPadding > request.atlasWidth - penX) {
                penX = 1;
                std::uint32_t shelfWithPadding = 0;
                if (!checkedAdd(shelfHeight, std::uint32_t{1}, shelfWithPadding)
                    || !checkedAdd(penY, shelfWithPadding, penY)) {
                    return {};
                }
                shelfHeight = 0;
            }
            if (penY > request.atlasHeight
                || heightWithPadding > request.atlasHeight - penY
                || penX > request.atlasWidth
                || widthWithPadding > request.atlasWidth - penX) {
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

                std::uint32_t paddedWidth = 0;
                if (!checkedAdd(glyphWidth, std::uint32_t{3}, paddedWidth)) return {};
                const std::uint32_t sourcePitch = paddedWidth & ~3U;
                std::size_t requiredSourceBytes = 0;
                if (!checkedMultiply(
                        static_cast<std::size_t>(sourcePitch),
                        static_cast<std::size_t>(glyphHeight),
                        requiredSourceBytes)
                    || requiredSourceBytes > bitmap.size()) {
                    return {};
                }
                for (std::uint32_t row = 0; row < glyphHeight; ++row) {
                    for (std::uint32_t column = 0; column < glyphWidth; ++column) {
                        std::size_t sourceOffset = 0;
                        std::size_t destinationRow = 0;
                        std::size_t destinationOffset = 0;
                        if (!checkedMultiply(
                                static_cast<std::size_t>(row),
                                static_cast<std::size_t>(sourcePitch),
                                sourceOffset)
                            || !checkedAdd(sourceOffset, static_cast<std::size_t>(column), sourceOffset)
                            || !checkedMultiply(
                                static_cast<std::size_t>(penY + row),
                                static_cast<std::size_t>(request.atlasWidth),
                                destinationRow)
                            || !checkedAdd(
                                destinationRow,
                                static_cast<std::size_t>(penX + column),
                                destinationOffset)
                            || sourceOffset >= bitmap.size()
                            || destinationOffset >= atlas.size()) {
                            return {};
                        }
                        const auto coverage = static_cast<unsigned char>(bitmap[sourceOffset]);
                        const auto normalized = static_cast<unsigned char>(
                            std::min(coverage, static_cast<unsigned char>(64U)) * 255U / 64U);
                        atlas[destinationOffset] = static_cast<std::byte>(normalized);
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

            if (!checkedAdd(penX, widthWithPadding, penX)) return {};
            shelfHeight = std::max(shelfHeight, packedHeight);
            if (codepoint == std::numeric_limits<char32_t>::max()) {
                break;
            }
        }
    }

    if (glyphs.empty()) {
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

    const TextureHandle atlasHandle = textures.create(
        TextureFormat::Alpha8,
        request.atlasWidth,
        request.atlasHeight,
        request.atlasWidth,
        atlas);
    if (!atlasHandle.valid()) return {};
    TextureRollback rollback{&textures, atlasHandle};
    const FontHandle font = fonts.add({
        .atlas = atlasHandle,
        .pixelSize = static_cast<float>(request.pixelHeight) * inverseMetricsScale,
        .ascent = static_cast<float>(textMetrics.tmAscent) * inverseMetricsScale,
        .descent = static_cast<float>(textMetrics.tmDescent) * inverseMetricsScale,
        .lineGap = static_cast<float>(textMetrics.tmExternalLeading) * inverseMetricsScale,
        .glyphs = std::move(glyphs),
        .kerning = std::move(kerning),
    });
    if (font.valid()) rollback.release();
    return font;
}

bool Win32FontLoader::appendGlyphs(
    DynamicGlyphAtlas& atlas,
    const Win32FontRequest& request,
    std::span<const char32_t> codepoints) {
    if (request.family.empty() || request.pixelHeight == 0 || codepoints.empty()
        || request.pixelHeight > static_cast<std::uint32_t>(std::numeric_limits<int>::max())
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
        std::uint32_t paddedWidth = 0;
        std::size_t pixelBytes = 0;
        std::size_t requiredSourceBytes = 0;
        if (!checkedAdd(width, std::uint32_t{3}, paddedWidth)
            || !checkedMultiply(
                static_cast<std::size_t>(width), static_cast<std::size_t>(height), pixelBytes)) {
            return false;
        }
        const std::uint32_t sourcePitch = paddedWidth & ~3U;
        if (!checkedMultiply(
                static_cast<std::size_t>(sourcePitch),
                static_cast<std::size_t>(height),
                requiredSourceBytes)
            || requiredSourceBytes > source.size()) {
            return false;
        }
        std::vector<std::byte> pixels(pixelBytes);
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
      mRanges(request.ranges.begin(), request.ranges.end()),
      mMaximumVariants(std::clamp(request.maximumVariants, std::size_t{1}, std::size_t{256})) {
    mVariants.reserve(mMaximumVariants);
}

FontHandle Win32FontScaleCache::select(float dpiScale) {
    return selectTextSize(mLogicalPixelHeight, dpiScale);
}

FontHandle Win32FontScaleCache::selectTextSize(
    float logicalPixelHeight,
    float dpiScale) {
    if (!std::isfinite(dpiScale) || dpiScale <= 0.0F
        || !std::isfinite(logicalPixelHeight) || logicalPixelHeight <= 0.0F
        || !std::isfinite(mLogicalPixelHeight) || mLogicalPixelHeight <= 0.0F
        || mFamily.empty() || mAtlasWidth == 0 || mAtlasHeight == 0 || mRanges.empty()) {
        return {};
    }
    const double requestedHeight = static_cast<double>(logicalPixelHeight) * dpiScale;
    if (requestedHeight < 1.0
        || requestedHeight > static_cast<double>(std::numeric_limits<int>::max())) {
        return {};
    }
    const auto pixelHeight = static_cast<std::uint32_t>(std::round(requestedHeight));
    mDpiScale = dpiScale;
    for (const Variant& variant : mVariants) {
        if (variant.pixelHeight == pixelHeight && mFonts->find(variant.font) != nullptr) {
            ++mCacheHits;
            return variant.font;
        }
    }

    if (mVariants.size() >= mMaximumVariants) {
        ++mVariantLimitFallbacks;
        const auto closest = std::min_element(
            mVariants.begin(), mVariants.end(),
            [pixelHeight](const Variant& left, const Variant& right) {
                const std::uint64_t leftDistance = left.pixelHeight > pixelHeight
                    ? left.pixelHeight - pixelHeight : pixelHeight - left.pixelHeight;
                const std::uint64_t rightDistance = right.pixelHeight > pixelHeight
                    ? right.pixelHeight - pixelHeight : pixelHeight - right.pixelHeight;
                return leftDistance < rightDistance;
            });
        return closest != mVariants.end() ? closest->font : FontHandle{};
    }

    ++mCacheMisses;
    const float metricsScale = static_cast<float>(pixelHeight) / logicalPixelHeight;
    const float atlasScale = static_cast<float>(pixelHeight) / mLogicalPixelHeight;
    const auto scaledDimension = [atlasScale](std::uint32_t dimension) {
        const double scaled = std::ceil(static_cast<double>(dimension) * atlasScale);
        if (scaled < 1.0 || scaled > std::numeric_limits<std::uint32_t>::max()) {
            return std::uint32_t{0};
        }
        return static_cast<std::uint32_t>(scaled);
    };
    const std::uint32_t atlasWidth = scaledDimension(mAtlasWidth);
    const std::uint32_t atlasHeight = scaledDimension(mAtlasHeight);
    if (atlasWidth == 0 || atlasHeight == 0) return {};

    // Reserve cache bookkeeping before publishing resources into either store.
    // Variant is trivially movable, so insertion cannot allocate afterwards.
    mVariants.reserve(mVariants.size() + 1U);

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
    mVariants.push_back({pixelHeight, logicalPixelHeight, font});
    return font;
}

FontHandle Win32FontScaleCache::selectForDpi(std::uint32_t dpi) {
    if (dpi == 0) return {};
    return select(static_cast<float>(dpi) / 96.0F);
}

FontHandle Win32FontScaleCache::resolveFont(
    FontHandle font,
    float logicalPixelSize) {
    const bool owned = std::any_of(
        mVariants.begin(), mVariants.end(),
        [font](const Variant& variant) { return variant.font == font; });
    return owned ? selectTextSize(logicalPixelSize, mDpiScale) : font;
}

bool Win32FontScaleCache::prewarmTextSizes(
    std::span<const float> logicalPixelSizes,
    float dpiScale) {
    if (logicalPixelSizes.empty()) return false;
    for (float size : logicalPixelSizes) {
        if (!selectTextSize(size, dpiScale).valid()) return false;
    }
    return true;
}

Win32FontScaleCacheStatistics Win32FontScaleCache::statistics() const noexcept {
    return {
        .variants = mVariants.size(),
        .cacheHits = mCacheHits,
        .cacheMisses = mCacheMisses,
        .variantLimitFallbacks = mVariantLimitFallbacks,
    };
}

} // namespace henia::ui
