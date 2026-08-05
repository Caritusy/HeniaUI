#pragma once

#include "henia/ui/Canvas.h"
#include "henia/ui/text/FontStore.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace henia::ui {

struct TextShapingGlyph final {
    FontHandle font{};
    std::uint32_t glyphId = 0;
    char32_t codepoint = U'\0';
    std::size_t byteBegin = 0;
    std::size_t byteEnd = 0;
    Vec2 offset{};
    float advance = 0.0F;
};

struct TextShapingRequest final {
    const FontStore* fonts = nullptr;
    std::span<const FontHandle> fontChain{};
    float size = 0.0F;
    std::string_view text{};
    std::size_t textByteOffset = 0;
};

// Optional shaping integration point. Minimal/ASCII builds use the built-in
// codepoint + kerning path and link no external text dependency. A host adapter
// (for example HarfBuzz) may return visual-order glyphs and cluster byte ranges.
class TextShapingBackend {
public:
    virtual ~TextShapingBackend() = default;
    [[nodiscard]] virtual bool shape(
        const TextShapingRequest& request,
        std::vector<TextShapingGlyph>& output) const = 0;
};

// Optional non-owning bridge for on-demand glyph systems. TextPainter calls it
// before layout so platform adapters can enqueue work without blocking.
class TextGlyphRequestBackend {
public:
    virtual ~TextGlyphRequestBackend() = default;
    virtual void requestText(std::string_view text) = 0;
};

struct TextLayoutGlyph final {
    FontHandle font{};
    std::uint32_t glyphId = 0;
    char32_t codepoint = U'\0';
    std::size_t byteBegin = 0;
    std::size_t byteEnd = 0;
    Rect bounds{};
    std::uint32_t line = 0;
};

struct TextCaretStop final {
    std::size_t byteOffset = 0;
    Vec2 position{};
    float lineHeight = 0.0F;
    std::uint32_t line = 0;
};

// Layout data contains no atlas UVs or texture grouping. It remains reusable
// until a face revision changes, independently from render-run cache eviction.
struct TextLayoutResult final {
    std::uint64_t identity = 0;
    std::uint64_t fontRevisionHash = 0;
    TextMetrics metrics{};
    std::vector<FontHandle> fonts;
    std::vector<TextLayoutGlyph> glyphs;
    std::vector<TextCaretStop> caretStops;
};

struct TextRenderSegment final {
    TextureHandle atlas{};
    std::vector<GlyphQuad> glyphs;
};

struct TextRun final {
    std::vector<TextRenderSegment> segments;
    TextMetrics metrics{};
};

class TextLayoutCache final {
public:
    explicit TextLayoutCache(
        const FontStore& fonts,
        const TextShapingBackend* shaping = nullptr) noexcept;

    void reserve(std::size_t entries, std::size_t glyphsPerEntry);
    void setMaximumEntries(std::size_t maximumEntries);
    [[nodiscard]] const TextLayoutResult* layout(
        std::span<const FontHandle> fontChain,
        float size,
        std::string_view text);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t hits() const noexcept;
    [[nodiscard]] std::uint64_t misses() const noexcept;

private:
    struct Entry final {
        std::vector<FontHandle> fonts;
        float size = 0.0F;
        std::string text;
        TextLayoutResult result;
    };

    [[nodiscard]] bool build(
        std::span<const FontHandle> fontChain,
        float size,
        std::string_view text,
        TextLayoutResult& output) const;
    [[nodiscard]] bool shapeSimple(
        const TextShapingRequest& request,
        std::vector<TextShapingGlyph>& output) const;
    [[nodiscard]] std::uint64_t revisionHash(std::span<const FontHandle> fonts) const noexcept;
    [[nodiscard]] static bool keyMatches(
        const Entry& entry,
        std::span<const FontHandle> fonts,
        float size,
        std::string_view text,
        std::uint64_t revisionHash) noexcept;
    [[nodiscard]] static std::uint64_t keyHash(
        std::span<const FontHandle> fonts,
        float size,
        std::string_view text) noexcept;
    void removeIndex(std::uint64_t hash, std::size_t entryIndex);

    const FontStore* mFonts = nullptr;
    const TextShapingBackend* mShaping = nullptr;
    std::vector<Entry> mEntries;
    std::unordered_multimap<std::uint64_t, std::size_t> mIndex;
    std::size_t mGlyphReserve = 0;
    std::size_t mMaximumEntries = 1024;
    std::size_t mEvictionCursor = 0;
    std::uint64_t mNextIdentity = 1;
    std::uint64_t mHits = 0;
    std::uint64_t mMisses = 0;
};

class TextRenderCache final {
public:
    explicit TextRenderCache(const FontStore& fonts) noexcept;

    void reserve(std::size_t entries, std::size_t glyphsPerEntry);
    void setMaximumEntries(std::size_t maximumEntries);
    [[nodiscard]] const TextRun* render(const TextLayoutResult& layout);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t hits() const noexcept;
    [[nodiscard]] std::uint64_t misses() const noexcept;

private:
    struct Entry final {
        std::uint64_t layoutIdentity = 0;
        std::uint64_t fontRevisionHash = 0;
        TextRun run;
    };

    [[nodiscard]] bool build(const TextLayoutResult& layout, TextRun& output) const;
    [[nodiscard]] std::uint64_t revisionHash(
        std::span<const FontHandle> fonts) const noexcept;

    const FontStore* mFonts = nullptr;
    std::vector<Entry> mEntries;
    std::unordered_map<std::uint64_t, std::size_t> mIndex;
    std::size_t mGlyphReserve = 0;
    std::size_t mMaximumEntries = 1024;
    std::size_t mEvictionCursor = 0;
    std::uint64_t mHits = 0;
    std::uint64_t mMisses = 0;
};

// Backward-compatible facade. The two owned caches have distinct keys and
// lifetimes even though legacy callers still ask for one renderable TextRun.
class TextRunCache final {
public:
    explicit TextRunCache(
        const FontStore& fonts,
        const TextShapingBackend* shaping = nullptr) noexcept;

    void reserve(std::size_t entries, std::size_t glyphsPerEntry);
    void setMaximumEntries(std::size_t maximumEntries);
    [[nodiscard]] const TextRun* layout(FontHandle font, float size, std::string_view text);
    [[nodiscard]] const TextRun* layout(
        std::span<const FontHandle> fontChain,
        float size,
        std::string_view text);
    [[nodiscard]] const TextLayoutResult* layoutResult(
        std::span<const FontHandle> fontChain,
        float size,
        std::string_view text);
    [[nodiscard]] const TextRun* renderLayout(const TextLayoutResult& layout);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t hits() const noexcept;
    [[nodiscard]] std::uint64_t misses() const noexcept;
    [[nodiscard]] const TextLayoutCache& layoutCache() const noexcept;
    [[nodiscard]] const TextRenderCache& renderCache() const noexcept;

private:
    TextLayoutCache mLayouts;
    TextRenderCache mRendering;
};

class TextPainter final {
public:
    explicit TextPainter(TextRunCache& cache) noexcept;

    // Single-font operations append this ordered fallback chain. Explicit span
    // overloads remain exact, which lets individual controls override it.
    void setFallbackFonts(std::span<const FontHandle> fonts);
    [[nodiscard]] std::span<const FontHandle> fallbackFonts() const noexcept;
    void setGlyphRequestBackend(TextGlyphRequestBackend* backend) noexcept;
    [[nodiscard]] TextGlyphRequestBackend* glyphRequestBackend() const noexcept;

    [[nodiscard]] TextMetrics measure(FontHandle font, float size, std::string_view text);
    [[nodiscard]] TextMetrics measure(
        std::span<const FontHandle> fontChain,
        float size,
        std::string_view text);
    [[nodiscard]] const TextLayoutResult* layout(
        FontHandle font,
        float size,
        std::string_view text);
    [[nodiscard]] const TextLayoutResult* layout(
        std::span<const FontHandle> fontChain,
        float size,
        std::string_view text);
    void draw(
        Canvas& canvas,
        FontHandle font,
        float size,
        Vec2 origin,
        Color color,
        std::string_view text);
    void draw(
        Canvas& canvas,
        std::span<const FontHandle> fontChain,
        float size,
        Vec2 origin,
        Color color,
        std::string_view text);
    void drawLayout(
        Canvas& canvas,
        const TextLayoutResult& layout,
        Vec2 origin,
        Color color);

    [[nodiscard]] static std::size_t hitTest(
        const TextLayoutResult& layout,
        Vec2 position) noexcept;
    [[nodiscard]] static Rect visualBounds(
        const TextLayoutResult& layout) noexcept;
    [[nodiscard]] static Vec2 centeredVisualOrigin(
        const TextLayoutResult& layout,
        Rect bounds) noexcept;
    [[nodiscard]] static Vec2 caretPosition(
        const TextLayoutResult& layout,
        std::size_t byteOffset) noexcept;
    [[nodiscard]] static std::vector<Rect> selectionRects(
        const TextLayoutResult& layout,
        std::size_t byteBegin,
        std::size_t byteEnd);

private:
    [[nodiscard]] std::span<const FontHandle> resolvedFonts(FontHandle primary);

    TextRunCache* mCache = nullptr;
    TextGlyphRequestBackend* mGlyphRequests = nullptr;
    FontHandle mResolvedPrimary{};
    std::vector<FontHandle> mFallbackFonts;
    std::vector<FontHandle> mResolvedFonts;
};

} // namespace henia::ui
