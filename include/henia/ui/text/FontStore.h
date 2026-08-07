#pragma once

#include "henia/ui/ResourceHandle.h"
#include "henia/ui/Types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace henia::ui {

struct GlyphMetrics final {
    char32_t codepoint = U'\0';
    Rect uv{};
    Vec2 size{};
    Vec2 bearing{};
    float advance = 0.0F;
    // Optional backend glyph identifier. Zero keeps the lightweight
    // codepoint-addressed path used by minimal builds.
    std::uint32_t glyphId = 0;
    // Dynamic/paged atlases may override the face's default texture per glyph.
    TextureHandle atlas{};
    // Pixel-aligned coverage bitmaps snap each glyph quad independently while
    // retaining fractional advances. Smooth glyphs retain run-origin snapping.
    GlyphRasterPlacement rasterPlacement = GlyphRasterPlacement::Smooth;
};

struct KerningPair final {
    char32_t left = U'\0';
    char32_t right = U'\0';
    float adjustment = 0.0F;
};

struct FontDefinition final {
    TextureHandle atlas{};
    float pixelSize = 0.0F;
    float ascent = 0.0F;
    float descent = 0.0F;
    float lineGap = 0.0F;
    std::vector<GlyphMetrics> glyphs;
    std::vector<KerningPair> kerning;
};

class PreparedGlyphUpdate final {
public:
    PreparedGlyphUpdate() = default;
    [[nodiscard]] bool valid() const noexcept { return mReady; }

private:
    friend class FontStore;
    FontHandle mHandle{};
    std::uint64_t mExpectedRevision = 0;
    std::vector<GlyphMetrics> mGlyphs;
    std::vector<std::pair<std::uint32_t, std::size_t>> mGlyphIdIndex;
    bool mReady = false;
};

class FontFace final {
public:
    FontFace() = default;
    explicit FontFace(FontDefinition definition);

    [[nodiscard]] TextureHandle atlas() const noexcept;
    [[nodiscard]] float pixelSize() const noexcept;
    [[nodiscard]] float ascent() const noexcept;
    [[nodiscard]] float descent() const noexcept;
    [[nodiscard]] float lineGap() const noexcept;
    [[nodiscard]] float lineHeight() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const GlyphMetrics* glyph(char32_t codepoint) const noexcept;
    [[nodiscard]] const GlyphMetrics* glyphById(std::uint32_t glyphId) const noexcept;
    [[nodiscard]] TextureHandle atlasFor(const GlyphMetrics& glyph) const noexcept;
    [[nodiscard]] float kerning(char32_t left, char32_t right) const noexcept;
    [[nodiscard]] std::size_t storageBytes() const noexcept;

private:
    friend class FontStore;

    [[nodiscard]] static std::uint64_t kerningKey(char32_t left, char32_t right) noexcept;
    [[nodiscard]] static std::vector<std::pair<std::uint32_t, std::size_t>> buildGlyphIdIndex(
        std::span<const GlyphMetrics> glyphs);
    [[nodiscard]] bool appendGlyphs(std::span<const GlyphMetrics> glyphs);

    TextureHandle mAtlas{};
    float mPixelSize = 0.0F;
    float mAscent = 0.0F;
    float mDescent = 0.0F;
    float mLineGap = 0.0F;
    std::uint64_t mRevision = 1;
    std::vector<GlyphMetrics> mGlyphs;
    // Sorted by (glyphId, codepoint-sorted glyph index). Duplicate non-zero
    // IDs resolve to the smallest codepoint, matching deterministic lookup.
    std::vector<std::pair<std::uint32_t, std::size_t>> mGlyphIdIndex;
    std::vector<KerningPair> mKerning;
};

class FontStore final {
public:
    [[nodiscard]] FontHandle add(FontDefinition definition);
    // Adds or replaces glyph records without invalidating the stable handle.
    // Layout/render caches observe the face revision and rebuild lazily.
    [[nodiscard]] bool addGlyphs(FontHandle handle, std::span<const GlyphMetrics> glyphs);
    [[nodiscard]] PreparedGlyphUpdate prepareGlyphs(
        FontHandle handle,
        std::span<const GlyphMetrics> glyphs) const;
    [[nodiscard]] bool commit(PreparedGlyphUpdate&& update) noexcept;
    // Removes dynamic glyphs owned by a retiring helper while preserving the
    // stable face handle and all unrelated glyphs.
    [[nodiscard]] bool removeGlyphs(
        FontHandle handle,
        std::span<const char32_t> codepoints);
    // Atomically restores prior glyph records and removes records that were
    // introduced by a retiring helper.
    [[nodiscard]] bool restoreGlyphs(
        FontHandle handle,
        std::span<const GlyphMetrics> restored,
        std::span<const char32_t> removed);
    [[nodiscard]] bool destroy(FontHandle handle) noexcept;
    [[nodiscard]] const FontFace* find(FontHandle handle) const noexcept;
    [[nodiscard]] FontHandle handleAt(std::size_t slotIndex) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t slotCount() const noexcept;
    [[nodiscard]] std::size_t storageBytes() const noexcept;

private:
    struct Entry final {
        std::optional<FontFace> face;
        std::uint16_t generation = 1;
        std::uint32_t nextFree = std::numeric_limits<std::uint32_t>::max();
    };

    [[nodiscard]] Entry* findEntry(FontHandle handle) noexcept;
    [[nodiscard]] const Entry* findEntry(FontHandle handle) const noexcept;

    std::vector<Entry> mFonts;
    std::uint32_t mFreeHead = std::numeric_limits<std::uint32_t>::max();
    std::size_t mActiveFonts = 0;
};

} // namespace henia::ui
