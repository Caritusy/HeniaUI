#pragma once

#include "henia/ui/ResourceHandle.h"
#include "henia/ui/Types.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace henia::ui {

struct GlyphMetrics final {
    char32_t codepoint = U'\0';
    Rect uv{};
    Vec2 size{};
    Vec2 bearing{};
    float advance = 0.0F;
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
    [[nodiscard]] const GlyphMetrics* glyph(char32_t codepoint) const noexcept;
    [[nodiscard]] float kerning(char32_t left, char32_t right) const noexcept;
    [[nodiscard]] std::size_t storageBytes() const noexcept;

private:
    [[nodiscard]] static std::uint64_t kerningKey(char32_t left, char32_t right) noexcept;

    TextureHandle mAtlas{};
    float mPixelSize = 0.0F;
    float mAscent = 0.0F;
    float mDescent = 0.0F;
    float mLineGap = 0.0F;
    std::vector<GlyphMetrics> mGlyphs;
    std::vector<KerningPair> mKerning;
};

class FontStore final {
public:
    [[nodiscard]] FontHandle add(FontDefinition definition);
    [[nodiscard]] bool destroy(FontHandle handle) noexcept;
    [[nodiscard]] const FontFace* find(FontHandle handle) const noexcept;
    [[nodiscard]] FontHandle handleAt(std::size_t slotIndex) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t slotCount() const noexcept;
    [[nodiscard]] std::size_t storageBytes() const noexcept;

private:
    struct Entry final {
        std::optional<FontFace> face;
        std::uint32_t generation = 1;
        std::uint32_t nextFree = std::numeric_limits<std::uint32_t>::max();
    };

    [[nodiscard]] Entry* findEntry(FontHandle handle) noexcept;
    [[nodiscard]] const Entry* findEntry(FontHandle handle) const noexcept;

    std::vector<Entry> mFonts;
    std::uint32_t mFreeHead = std::numeric_limits<std::uint32_t>::max();
    std::size_t mActiveFonts = 0;
};

} // namespace henia::ui
