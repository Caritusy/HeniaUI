#include "henia/ui/text/DynamicGlyphAtlas.h"

#include "henia/CheckedArithmetic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace henia::ui {
namespace {

struct TextureCreationRollback final {
    TextureStore* textures = nullptr;
    std::vector<TextureHandle>* handles = nullptr;
    bool active = true;

    ~TextureCreationRollback() {
        if (!active || textures == nullptr || handles == nullptr) return;
        for (TextureHandle handle : *handles) {
            static_cast<void>(textures->destroy(handle));
        }
    }
    void release() noexcept { active = false; }
};

} // namespace

DynamicGlyphAtlas::DynamicGlyphAtlas(
    TextureStore& textures,
    FontStore& fonts,
    FontHandle font,
    DynamicGlyphAtlasOptions options) noexcept
    : mTextures(&textures), mFonts(&fonts), mFont(font), mOptions(options) {}

bool DynamicGlyphAtlas::reservePages(std::size_t count) {
    if (count > mOptions.maximumPages || mFonts->find(mFont) == nullptr) return false;
    mPageState.reserve(count);
    mPageHandles.reserve(count);
    while (mPageState.size() < count) {
        if (!allocatePage()) return false;
    }
    return true;
}

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

    std::vector<Page> plannedPages = mPageState;
    std::vector<TextureHandle> plannedHandles = mPageHandles;
    std::vector<TextureHandle> newTextures;
    std::vector<Placement> placements(glyphs.size());
    std::vector<GlyphMetrics> metrics;
    std::vector<TextureRegionUpdate> textureUpdates;
    plannedPages.reserve(mOptions.maximumPages);
    plannedHandles.reserve(mOptions.maximumPages);
    newTextures.reserve(mOptions.maximumPages - plannedPages.size());
    metrics.reserve(glyphs.size());
    textureUpdates.reserve(glyphs.size());
    // Publication tracking is part of resource ownership. Reserve it before
    // creating/committing store resources so an allocation failure cannot make
    // a glyph unreachable from releaseResources().
    mPublishedCodepoints.reserve(mPublishedCodepoints.size() + glyphs.size());

    TextureCreationRollback rollback{mTextures, &newTextures};
    const auto allocatePlannedPage = [&]() {
        if (plannedPages.size() >= mOptions.maximumPages) return false;
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
        newTextures.push_back(texture);
        plannedPages.push_back({
            .texture = texture,
            .penX = mOptions.padding,
            .penY = mOptions.padding,
        });
        plannedHandles.push_back(texture);
        return true;
    };
    const auto placePlanned = [&](std::uint32_t width, std::uint32_t height, Placement& placement) {
        const std::uint32_t padding = mOptions.padding;
        const auto tryPage = [&](std::size_t index, Placement& result) {
            Page& page = plannedPages[index];
            if (page.penX > mOptions.pageWidth - width - padding) {
                page.penX = padding;
                if (page.penY > mOptions.pageHeight - page.shelfHeight - padding) return false;
                page.penY += page.shelfHeight + padding;
                page.shelfHeight = 0;
            }
            if (page.penY > mOptions.pageHeight - height - padding) return false;
            result = {.page = index, .x = page.penX, .y = page.penY};
            page.penX += width + padding;
            page.shelfHeight = std::max(page.shelfHeight, height);
            return true;
        };
        if (!plannedPages.empty() && tryPage(plannedPages.size() - 1U, placement)) return true;
        return allocatePlannedPage() && tryPage(plannedPages.size() - 1U, placement);
    };

    std::size_t uploadedBytes = 0;
    for (std::size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex) {
        const RasterizedGlyph& glyph = glyphs[glyphIndex];
        const bool hasLogicalSize = glyph.logicalSize.x > 0.0F && glyph.logicalSize.y > 0.0F;
        GlyphMetrics metric{
            .codepoint = glyph.codepoint,
            .size = hasLogicalSize
                ? glyph.logicalSize
                : Vec2{static_cast<float>(glyph.width), static_cast<float>(glyph.height)},
            .bearing = glyph.bearing,
            .advance = glyph.advance,
            .glyphId = glyph.glyphId,
            .atlas = face->atlas(),
        };
        if (glyph.width != 0) {
            Placement& placement = placements[glyphIndex];
            if (!placePlanned(glyph.width, glyph.height, placement)) {
                ++mStatistics.failedAdditions;
                return false;
            }
            const Page& page = plannedPages[placement.page];
            textureUpdates.push_back({
                .handle = page.texture,
                .region = {placement.x, placement.y, glyph.width, glyph.height},
                .sourceRowPitch = glyph.rowPitch,
                .pixels = glyph.pixels,
            });
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
        uploadedBytes += glyph.pixels.size();
    }
    PreparedGlyphUpdate preparedGlyphs = mFonts->prepareGlyphs(mFont, metrics);
    if (!preparedGlyphs.valid()) {
        ++mStatistics.failedAdditions;
        return false;
    }
    PreparedTextureUpdate preparedTextures;
    if (!textureUpdates.empty()) {
        preparedTextures = mTextures->prepareRegionUpdates(textureUpdates);
        if (!preparedTextures.valid()) {
            ++mStatistics.failedAdditions;
            return false;
        }
        if (!mTextures->commit(std::move(preparedTextures))) {
            ++mStatistics.failedAdditions;
            return false;
        }
    }
    if (!mFonts->commit(std::move(preparedGlyphs))) {
        // FontStore and TextureStore are owner-thread resources. With both
        // transactions prepared above, this can only happen after an external
        // contract violation that changed the face concurrently.
        ++mStatistics.failedAdditions;
        return false;
    }
    mPageState = std::move(plannedPages);
    mPageHandles = std::move(plannedHandles);
    rollback.release();
    for (const RasterizedGlyph& glyph : glyphs) {
        if (std::find(mPublishedCodepoints.begin(), mPublishedCodepoints.end(), glyph.codepoint)
            == mPublishedCodepoints.end()) {
            mPublishedCodepoints.push_back(glyph.codepoint);
        }
    }
    mStatistics.fullPageAllocations += newTextures.size();
    mStatistics.uploadedBytes += uploadedBytes;
    mStatistics.glyphsAdded += glyphs.size();
    return true;
}

bool DynamicGlyphAtlas::releaseResources() {
    if (!mPublishedCodepoints.empty() && mFonts->find(mFont) != nullptr
        && !mFonts->removeGlyphs(mFont, mPublishedCodepoints)) {
        return false;
    }
    bool released = true;
    for (TextureHandle handle : mPageHandles) {
        if (mTextures->view(handle).handle.valid()
            && !mTextures->destroy(handle)) {
            released = false;
        }
    }
    if (!released) return false;
    mPublishedCodepoints.clear();
    mPageState.clear();
    mPageHandles.clear();
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
    const bool defaultLogicalSize = glyph.logicalSize == Vec2{};
    const bool explicitLogicalSize = glyph.logicalSize.x > 0.0F && glyph.logicalSize.y > 0.0F;
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
        && std::isfinite(glyph.logicalSize.x) && std::isfinite(glyph.logicalSize.y)
        && (defaultLogicalSize || explicitLogicalSize)
        && std::isfinite(glyph.bearing.x) && std::isfinite(glyph.bearing.y)
        && std::isfinite(glyph.advance) && glyph.advance >= 0.0F;
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
