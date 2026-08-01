#include "henia/ui/text/FontStore.h"

#include <algorithm>

namespace henia::ui {

FontFace::FontFace(FontDefinition definition)
    : mAtlas(definition.atlas),
      mPixelSize(definition.pixelSize),
      mAscent(definition.ascent),
      mDescent(definition.descent),
      mLineGap(definition.lineGap),
      mGlyphs(std::move(definition.glyphs)),
      mKerning(std::move(definition.kerning)) {
    std::sort(mGlyphs.begin(), mGlyphs.end(), [](const GlyphMetrics& left, const GlyphMetrics& right) {
        return left.codepoint < right.codepoint;
    });
    std::sort(mKerning.begin(), mKerning.end(), [](const KerningPair& left, const KerningPair& right) {
        return kerningKey(left.left, left.right) < kerningKey(right.left, right.right);
    });
}

TextureHandle FontFace::atlas() const noexcept { return mAtlas; }

float FontFace::pixelSize() const noexcept { return mPixelSize; }

float FontFace::ascent() const noexcept { return mAscent; }

float FontFace::descent() const noexcept { return mDescent; }

float FontFace::lineGap() const noexcept { return mLineGap; }

float FontFace::lineHeight() const noexcept { return mAscent + mDescent + mLineGap; }

const GlyphMetrics* FontFace::glyph(char32_t codepoint) const noexcept {
    const auto iterator = std::lower_bound(
        mGlyphs.begin(),
        mGlyphs.end(),
        codepoint,
        [](const GlyphMetrics& glyphMetrics, char32_t value) { return glyphMetrics.codepoint < value; });
    return iterator != mGlyphs.end() && iterator->codepoint == codepoint ? &*iterator : nullptr;
}

float FontFace::kerning(char32_t left, char32_t right) const noexcept {
    if (left == U'\0' || right == U'\0') {
        return 0.0F;
    }
    const std::uint64_t key = kerningKey(left, right);
    const auto iterator = std::lower_bound(
        mKerning.begin(),
        mKerning.end(),
        key,
        [](const KerningPair& pair, std::uint64_t value) {
            return kerningKey(pair.left, pair.right) < value;
        });
    return iterator != mKerning.end() && kerningKey(iterator->left, iterator->right) == key
        ? iterator->adjustment
        : 0.0F;
}

std::uint64_t FontFace::kerningKey(char32_t left, char32_t right) noexcept {
    return (static_cast<std::uint64_t>(left) << 32U) | static_cast<std::uint32_t>(right);
}

FontHandle FontStore::add(FontDefinition definition) {
    if (!definition.atlas.valid() || definition.pixelSize <= 0.0F || definition.glyphs.empty()) {
        return {};
    }
    mFonts.emplace_back(std::move(definition));
    return FontHandle{static_cast<std::uint32_t>(mFonts.size())};
}

const FontFace* FontStore::find(FontHandle handle) const noexcept {
    if (!handle.valid() || handle.value() > mFonts.size()) {
        return nullptr;
    }
    return &mFonts[handle.value() - 1U];
}

std::size_t FontStore::size() const noexcept { return mFonts.size(); }

} // namespace henia::ui
