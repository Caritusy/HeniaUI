#pragma once

#include "henia/CheckedArithmetic.h"
#include "henia/ui/Types.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace henia::ui::detail {

using Microsoft::WRL::ComPtr;

enum class DirectWriteGlyphStatus : std::uint8_t {
    Ready,
    Missing,
    Failed,
};

struct DirectWriteGlyphBitmap final {
    DirectWriteGlyphStatus status = DirectWriteGlyphStatus::Failed;
    std::uint32_t glyphId = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    Vec2 bearing{};
    float advance = 0.0F;
    Vec2 logicalSize{};
    GlyphRasterPlacement rasterPlacement = GlyphRasterPlacement::Smooth;
    std::vector<std::byte> pixels;
};

[[nodiscard]] inline std::uint32_t normalizedPhysicalSizeSteps(
    std::uint32_t requested) noexcept {
    return std::clamp(requested, 1U, 64U);
}

[[nodiscard]] inline std::uint64_t physicalSizeKey(
    double physicalPixelHeight,
    std::uint32_t stepsPerPixel) noexcept {
    const std::uint32_t steps = normalizedPhysicalSizeSteps(stepsPerPixel);
    const double scaled = std::round(physicalPixelHeight * static_cast<double>(steps));
    return std::isfinite(scaled) && scaled >= static_cast<double>(steps)
            && scaled <= static_cast<double>(std::numeric_limits<std::uint64_t>::max())
        ? static_cast<std::uint64_t>(scaled)
        : 0U;
}

[[nodiscard]] inline float physicalSizeFromKey(
    std::uint64_t key,
    std::uint32_t stepsPerPixel) noexcept {
    return static_cast<float>(key)
        / static_cast<float>(normalizedPhysicalSizeSteps(stepsPerPixel));
}

[[nodiscard]] inline bool makeDirectWriteFace(
    IDWriteFontCollection& collection,
    std::wstring_view familyName,
    ComPtr<IDWriteFontFace>& output) {
    if (familyName.empty()) return false;
    const std::wstring family(familyName);
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    if (FAILED(collection.FindFamilyName(family.c_str(), &familyIndex, &exists))
        || exists == FALSE) {
        return false;
    }
    ComPtr<IDWriteFontFamily> fontFamily;
    if (FAILED(collection.GetFontFamily(familyIndex, &fontFamily))) return false;
    ComPtr<IDWriteFont> font;
    if (FAILED(fontFamily->GetFirstMatchingFont(
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            &font))) {
        return false;
    }
    return SUCCEEDED(font->CreateFontFace(&output));
}

[[nodiscard]] inline DirectWriteGlyphBitmap rasterizeDirectWriteGlyph(
    IDWriteFactory& factory,
    IDWriteFontFace& face,
    char32_t codepoint,
    float physicalPixelHeight,
    float pixelsPerLogicalUnit,
    float pixelAlignedMaximumPhysicalHeight) {
    DirectWriteGlyphBitmap result{};
    if (!std::isfinite(physicalPixelHeight) || physicalPixelHeight < 1.0F
        || !std::isfinite(pixelsPerLogicalUnit) || pixelsPerLogicalUnit <= 0.0F
        || !std::isfinite(pixelAlignedMaximumPhysicalHeight)
        || pixelAlignedMaximumPhysicalHeight < 0.0F) {
        return result;
    }

    const UINT32 scalar = static_cast<UINT32>(codepoint);
    UINT16 glyphIndex = 0;
    if (FAILED(face.GetGlyphIndices(&scalar, 1, &glyphIndex))) return result;
    if (glyphIndex == 0) {
        result.status = DirectWriteGlyphStatus::Missing;
        return result;
    }

    DWRITE_FONT_METRICS fontMetrics{};
    face.GetMetrics(&fontMetrics);
    if (fontMetrics.designUnitsPerEm == 0) return result;
    DWRITE_GLYPH_METRICS glyphMetrics{};
    if (FAILED(face.GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics, FALSE))) {
        return result;
    }

    const float logicalScale = physicalPixelHeight
        / static_cast<float>(fontMetrics.designUnitsPerEm)
        / pixelsPerLogicalUnit;
    result.advance = static_cast<float>(glyphMetrics.advanceWidth) * logicalScale;
    if (!std::isfinite(result.advance) || result.advance < 0.0F) return {};

    const bool pixelAligned = pixelAlignedMaximumPhysicalHeight > 0.0F
        && physicalPixelHeight <= pixelAlignedMaximumPhysicalHeight;
    const DWRITE_GLYPH_RUN run{
        .fontFace = &face,
        .fontEmSize = physicalPixelHeight,
        .glyphCount = 1,
        .glyphIndices = &glyphIndex,
        .glyphAdvances = nullptr,
        .glyphOffsets = nullptr,
        .isSideways = FALSE,
        .bidiLevel = 0,
    };
    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    if (FAILED(factory.CreateGlyphRunAnalysis(
            &run,
            1.0F,
            nullptr,
            pixelAligned ? DWRITE_RENDERING_MODE_NATURAL
                         : DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            DWRITE_MEASURING_MODE_NATURAL,
            0.0F,
            0.0F,
            &analysis))) {
        return {};
    }

    RECT bounds{};
    if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds))) {
        return {};
    }
    result.glyphId = glyphIndex;
    result.rasterPlacement = pixelAligned
        ? GlyphRasterPlacement::PixelAligned
        : GlyphRasterPlacement::Smooth;
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        result.status = DirectWriteGlyphStatus::Ready;
        return result;
    }

    const auto width = static_cast<std::uint64_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint64_t>(bounds.bottom - bounds.top);
    if (width > std::numeric_limits<std::uint32_t>::max()
        || height > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    std::size_t pixelCount = 0;
    std::size_t clearTypeBytes = 0;
    if (!checkedMultiply(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            pixelCount)
        || !checkedMultiply(pixelCount, std::size_t{3}, clearTypeBytes)
        || clearTypeBytes > std::numeric_limits<UINT32>::max()) {
        return {};
    }

    std::vector<std::byte> clearType(clearTypeBytes);
    if (FAILED(analysis->CreateAlphaTexture(
            DWRITE_TEXTURE_CLEARTYPE_3x1,
            &bounds,
            reinterpret_cast<BYTE*>(clearType.data()),
            static_cast<UINT32>(clearType.size())))) {
        return {};
    }
    result.pixels.resize(pixelCount);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const auto red = static_cast<unsigned int>(
            static_cast<unsigned char>(clearType[index * 3U]));
        const auto green = static_cast<unsigned int>(
            static_cast<unsigned char>(clearType[index * 3U + 1U]));
        const auto blue = static_cast<unsigned int>(
            static_cast<unsigned char>(clearType[index * 3U + 2U]));
        result.pixels[index] = static_cast<std::byte>((red + green + blue + 1U) / 3U);
    }

    result.width = static_cast<std::uint32_t>(width);
    result.height = static_cast<std::uint32_t>(height);
    result.rowPitch = result.width;
    result.bearing = {
        static_cast<float>(bounds.left) / pixelsPerLogicalUnit,
        -static_cast<float>(bounds.top) / pixelsPerLogicalUnit,
    };
    result.logicalSize = {
        static_cast<float>(width) / pixelsPerLogicalUnit,
        static_cast<float>(height) / pixelsPerLogicalUnit,
    };
    result.status = DirectWriteGlyphStatus::Ready;
    return result;
}

} // namespace henia::ui::detail
