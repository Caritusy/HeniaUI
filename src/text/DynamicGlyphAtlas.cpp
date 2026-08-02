#include "henia/ui/text/DynamicGlyphAtlas.h"

#include "henia/CheckedArithmetic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace henia::ui {

DynamicGlyphAtlas::DynamicGlyphAtlas(
    TextureStore& textures,
    FontStore& fonts,
    FontHandle font,
    DynamicGlyphAtlasOptions options) noexcept
    : mTextures(&textures), mFonts(&fonts), mFont(font), mOptions(options) {}

bool DynamicGlyphAtlas::add(const RasterizedGlyph& glyph) {
    return add(std::span<const RasterizedGlyph>(&glyph, 1));
}

bool DynamicGlyphAtlas::add(std::span<const RasterizedGlyph> glyphs) {
    const FontFace* face = mFonts->find(mFont);
    if (glyphs.empty() || face == nullptr) {
        ++mStatistics.failedAdditions;
        return false;
    }
    for (const RasterizedGlyph& glyph : glyphs) {
        if (!valid(glyph)) {
            ++mStatistics.failedAdditions;
            return false;
        }
    }

    std::vector<GlyphMetrics> metrics;
    metrics.reserve(glyphs.size());
    for (const RasterizedGlyph& glyph : glyphs) {
        GlyphMetrics metric{
            .codepoint = glyph.codepoint,
            .size = {static_cast<float>(glyph.width), static_cast<float>(glyph.height)},
            .bearing = glyph.bearing,
            .advance = glyph.advance,
            .glyphId = glyph.glyphId,
            .atlas = face->atlas(),
        };
        if (glyph.width != 0) {
            Placement placement{};
            if (!place(glyph.width, glyph.height, placement)) {
                ++mStatistics.failedAdditions;
                return false;
            }
            Page& page = mPageState[placement.page];
            if (!mTextures->updateRegion(
                    page.texture,
                    {placement.x, placement.y, glyph.width, glyph.height},
                    glyph.rowPitch,
                    glyph.pixels)) {
                ++mStatistics.failedAdditions;
                return false;
            }
            metric.uv = {
                {
                    static_cast<float>(placement.x) / static_cast<float>(mOptions.pageWidth),
                    static_cast<float>(placement.y) / static_cast<float>(mOptions.pageHeight),
                },
                {
                    static_cast<float>(placement.x + glyph.width)
                        / static_cast<float>(mOptions.pageWidth),
                    static_cast<float>(placement.y + glyph.height)
                        / static_cast<float>(mOptions.pageHeight),
                },
            };
            metric.atlas = page.texture;
        }
        metrics.push_back(metric);
        mStatistics.uploadedBytes += glyph.pixels.size();
    }
    if (!mFonts->addGlyphs(mFont, metrics)) {
        ++mStatistics.failedAdditions;
        return false;
    }
    mStatistics.glyphsAdded += glyphs.size();
    return true;
}

FontHandle DynamicGlyphAtlas::font() const noexcept { return mFont; }

std::span<const TextureHandle> DynamicGlyphAtlas::pages() const noexcept {
    return mPageHandles;
}

DynamicGlyphAtlasStatistics DynamicGlyphAtlas::statistics() const noexcept {
    DynamicGlyphAtlasStatistics result = mStatistics;
    result.pages = mPageState.size();
    return result;
}

bool DynamicGlyphAtlas::valid(const RasterizedGlyph& glyph) const noexcept {
    const bool advanceOnly = glyph.width == 0 && glyph.height == 0
        && glyph.rowPitch == 0 && glyph.pixels.empty();
    std::size_t required = 0;
    const bool drawable = mOptions.pageWidth > 0 && mOptions.pageHeight > 0
        && mOptions.maximumPages > 0
        && mOptions.padding <= mOptions.pageWidth / 2U
        && mOptions.padding <= mOptions.pageHeight / 2U
        && glyph.width > 0 && glyph.height > 0
        && glyph.width <= mOptions.pageWidth - mOptions.padding * 2U
        && glyph.height <= mOptions.pageHeight - mOptions.padding * 2U
        && glyph.rowPitch >= glyph.width
        && checkedMultiply(
            static_cast<std::size_t>(glyph.rowPitch),
            static_cast<std::size_t>(glyph.height),
            required)
        && required == glyph.pixels.size();
    return glyph.codepoint != U'\0' && glyph.codepoint <= 0x10FFFFU
        && !(glyph.codepoint >= 0xD800U && glyph.codepoint <= 0xDFFFU)
        && (advanceOnly || drawable)
        && std::isfinite(glyph.bearing.x) && std::isfinite(glyph.bearing.y)
        && std::isfinite(glyph.advance) && glyph.advance >= 0.0F;
}

bool DynamicGlyphAtlas::place(
    std::uint32_t width,
    std::uint32_t height,
    Placement& placement) {
    const std::uint32_t padding = mOptions.padding;
    const auto tryPage = [&](std::size_t index, Placement& result) {
        Page& page = mPageState[index];
        if (page.penX > mOptions.pageWidth - width - padding) {
            page.penX = padding;
            if (page.penY > mOptions.pageHeight - page.shelfHeight - padding) {
                return false;
            }
            page.penY += page.shelfHeight + padding;
            page.shelfHeight = 0;
        }
        if (page.penY > mOptions.pageHeight - height - padding) {
            return false;
        }
        result = {.page = index, .x = page.penX, .y = page.penY};
        page.penX += width + padding;
        page.shelfHeight = std::max(page.shelfHeight, height);
        return true;
    };

    if (!mPageState.empty() && tryPage(mPageState.size() - 1U, placement)) {
        return true;
    }
    if (!allocatePage()) return false;
    return tryPage(mPageState.size() - 1U, placement);
}

bool DynamicGlyphAtlas::allocatePage() {
    if (mPageState.size() >= mOptions.maximumPages) return false;
    std::size_t bytes = 0;
    if (!checkedMultiply(
            static_cast<std::size_t>(mOptions.pageWidth),
            static_cast<std::size_t>(mOptions.pageHeight),
            bytes)) {
        return false;
    }
    std::vector<std::byte> pixels(bytes, std::byte{0});
    const TextureHandle texture = mTextures->create(
        TextureFormat::Alpha8,
        mOptions.pageWidth,
        mOptions.pageHeight,
        mOptions.pageWidth,
        pixels);
    if (!texture.valid()) return false;
    mPageState.push_back({
        .texture = texture,
        .penX = mOptions.padding,
        .penY = mOptions.padding,
    });
    mPageHandles.push_back(texture);
    ++mStatistics.fullPageAllocations;
    return true;
}

} // namespace henia::ui
