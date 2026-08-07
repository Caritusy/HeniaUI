#pragma once

#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace henia::ui {

// Rasterizers remain platform/host supplied. The core atlas only owns packing,
// stable page allocation, texture updates, and FontStore revision publication.
struct RasterizedGlyph final {
    char32_t codepoint = U'\0';
    std::uint32_t glyphId = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    Vec2 bearing{};
    float advance = 0.0F;
    std::span<const std::byte> pixels{};
    // Optional logical dimensions when the bitmap is rasterized above 1x.
    // Zero selects the physical width/height for backward compatibility.
    Vec2 logicalSize{};
};

struct DynamicGlyphAtlasOptions final {
    std::uint32_t pageWidth = 512;
    std::uint32_t pageHeight = 512;
    std::uint32_t padding = 1;
    std::size_t maximumPages = 8;
};

struct DynamicGlyphAtlasStatistics final {
    std::size_t pages = 0;
    std::size_t glyphsAdded = 0;
    std::size_t fullPageAllocations = 0;
    std::size_t failedAdditions = 0;
    std::size_t uploadedBytes = 0;
};

// Pages never resize, so already-published normalized UVs and immutable render
// packets remain valid. Growth allocates another texture-table-compatible page.
class DynamicGlyphAtlas final {
public:
    DynamicGlyphAtlas(
        TextureStore& textures,
        FontStore& fonts,
        FontHandle font,
        DynamicGlyphAtlasOptions options = {}) noexcept;

    // Allocates stable empty atlas pages ahead of the interactive loop.
    // Existing pages are retained; count may not exceed maximumPages.
    [[nodiscard]] bool reservePages(std::size_t count);
    [[nodiscard]] bool add(const RasterizedGlyph& glyph);
    [[nodiscard]] bool add(std::span<const RasterizedGlyph> glyphs);
    // Explicitly removes glyph records published by this atlas and retires all
    // of its page textures. The destructor intentionally does not do this so a
    // host can choose whether published resources outlive the packing helper.
    [[nodiscard]] bool releaseResources();

    [[nodiscard]] FontHandle font() const noexcept;
    [[nodiscard]] std::span<const TextureHandle> pages() const noexcept;
    [[nodiscard]] DynamicGlyphAtlasStatistics statistics() const noexcept;

private:
    struct Page final {
        TextureHandle texture{};
        std::uint32_t penX = 0;
        std::uint32_t penY = 0;
        std::uint32_t shelfHeight = 0;
    };

    struct Placement final {
        std::size_t page = 0;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
    };

    [[nodiscard]] bool valid(const RasterizedGlyph& glyph) const noexcept;
    [[nodiscard]] bool allocatePage();

    TextureStore* mTextures = nullptr;
    FontStore* mFonts = nullptr;
    FontHandle mFont{};
    DynamicGlyphAtlasOptions mOptions{};
    std::vector<Page> mPageState;
    std::vector<TextureHandle> mPageHandles;
    std::vector<char32_t> mPublishedCodepoints;
    DynamicGlyphAtlasStatistics mStatistics{};
};

} // namespace henia::ui
