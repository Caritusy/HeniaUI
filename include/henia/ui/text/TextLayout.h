#pragma once

#include "henia/ui/Canvas.h"
#include "henia/ui/text/FontStore.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace henia::ui {

struct TextRun final {
    TextureHandle atlas{};
    std::vector<GlyphQuad> glyphs;
    TextMetrics metrics{};
};

class TextRunCache final {
public:
    explicit TextRunCache(const FontStore& fonts) noexcept;

    void reserve(std::size_t entries, std::size_t glyphsPerEntry);
    void setMaximumEntries(std::size_t maximumEntries);
    [[nodiscard]] const TextRun* layout(FontHandle font, float size, std::string_view text);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t hits() const noexcept;
    [[nodiscard]] std::uint64_t misses() const noexcept;

private:
    struct Entry final {
        FontHandle font{};
        float size = 0.0F;
        std::string text;
        TextRun run;
    };

    [[nodiscard]] static bool keyMatches(
        const Entry& entry,
        FontHandle font,
        float size,
        std::string_view text) noexcept;
    [[nodiscard]] static std::uint64_t keyHash(
        FontHandle font,
        float size,
        std::string_view text) noexcept;
    [[nodiscard]] bool buildRun(
        const FontFace& face,
        float size,
        std::string_view text,
        TextRun& output) const;
    void removeIndex(std::uint64_t hash, std::size_t entryIndex);

    const FontStore* mFonts = nullptr;
    std::vector<Entry> mEntries;
    std::unordered_multimap<std::uint64_t, std::size_t> mIndex;
    std::size_t mGlyphReserve = 0;
    std::size_t mMaximumEntries = 1024;
    std::size_t mEvictionCursor = 0;
    std::uint64_t mHits = 0;
    std::uint64_t mMisses = 0;
};

class TextPainter final {
public:
    explicit TextPainter(TextRunCache& cache) noexcept;

    [[nodiscard]] TextMetrics measure(FontHandle font, float size, std::string_view text);
    void draw(
        Canvas& canvas,
        FontHandle font,
        float size,
        Vec2 origin,
        Color color,
        std::string_view text);

private:
    TextRunCache* mCache = nullptr;
};

} // namespace henia::ui
