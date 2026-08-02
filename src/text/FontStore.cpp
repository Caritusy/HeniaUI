#include "henia/ui/text/FontStore.h"

#include <algorithm>
#include <cmath>

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

std::uint64_t FontFace::revision() const noexcept { return mRevision; }

const GlyphMetrics* FontFace::glyph(char32_t codepoint) const noexcept {
    const auto iterator = std::lower_bound(
        mGlyphs.begin(),
        mGlyphs.end(),
        codepoint,
        [](const GlyphMetrics& glyphMetrics, char32_t value) { return glyphMetrics.codepoint < value; });
    return iterator != mGlyphs.end() && iterator->codepoint == codepoint ? &*iterator : nullptr;
}

const GlyphMetrics* FontFace::glyphById(std::uint32_t glyphId) const noexcept {
    if (glyphId == 0) return nullptr;
    const auto iterator = std::find_if(
        mGlyphs.begin(),
        mGlyphs.end(),
        [glyphId](const GlyphMetrics& glyphMetrics) {
            return glyphMetrics.glyphId == glyphId;
        });
    return iterator == mGlyphs.end() ? nullptr : &*iterator;
}

TextureHandle FontFace::atlasFor(const GlyphMetrics& glyphMetrics) const noexcept {
    return glyphMetrics.atlas.valid() ? glyphMetrics.atlas : mAtlas;
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

std::size_t FontFace::storageBytes() const noexcept {
    return mGlyphs.capacity() * sizeof(GlyphMetrics)
        + mKerning.capacity() * sizeof(KerningPair);
}

bool FontFace::appendGlyphs(std::span<const GlyphMetrics> glyphs) {
    if (glyphs.empty() || mRevision == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    std::vector<GlyphMetrics> replacement = mGlyphs;
    for (const GlyphMetrics& glyphMetrics : glyphs) {
        const bool advanceOnly = glyphMetrics.size.x == 0.0F
            && glyphMetrics.size.y == 0.0F;
        const bool drawable = glyphMetrics.size.x > 0.0F
            && glyphMetrics.size.y > 0.0F && glyphMetrics.uv.valid()
            && (glyphMetrics.atlas.valid() || mAtlas.valid());
        if (glyphMetrics.codepoint == U'\0' || glyphMetrics.codepoint > 0x10FFFFU
            || (glyphMetrics.codepoint >= 0xD800U && glyphMetrics.codepoint <= 0xDFFFU)
            || (!advanceOnly && !drawable)
            || !std::isfinite(glyphMetrics.size.x) || !std::isfinite(glyphMetrics.size.y)
            || !std::isfinite(glyphMetrics.bearing.x) || !std::isfinite(glyphMetrics.bearing.y)
            || !std::isfinite(glyphMetrics.advance) || glyphMetrics.advance < 0.0F) {
            return false;
        }
        const auto iterator = std::lower_bound(
            replacement.begin(),
            replacement.end(),
            glyphMetrics.codepoint,
            [](const GlyphMetrics& existing, char32_t value) {
                return existing.codepoint < value;
            });
        if (iterator != replacement.end() && iterator->codepoint == glyphMetrics.codepoint) {
            *iterator = glyphMetrics;
        } else {
            replacement.insert(iterator, glyphMetrics);
        }
    }
    mGlyphs = std::move(replacement);
    ++mRevision;
    return true;
}

std::uint64_t FontFace::kerningKey(char32_t left, char32_t right) noexcept {
    return (static_cast<std::uint64_t>(left) << 32U) | static_cast<std::uint32_t>(right);
}

FontHandle FontStore::add(FontDefinition definition) {
    if (!definition.atlas.valid() || definition.pixelSize <= 0.0F || definition.glyphs.empty()) {
        return {};
    }
    constexpr std::uint32_t invalidSlot = std::numeric_limits<std::uint32_t>::max();
    if (mFreeHead != invalidSlot) {
        const std::uint32_t slot = mFreeHead;
        Entry& entry = mFonts[slot];
        entry.face.emplace(std::move(definition));
        mFreeHead = entry.nextFree;
        entry.nextFree = invalidSlot;
        ++mActiveFonts;
        return FontHandle{slot + 1U, entry.generation};
    }
    if (mFonts.size() >= FontHandle::kMaxValue) return {};
    Entry entry{};
    entry.face.emplace(std::move(definition));
    mFonts.push_back(std::move(entry));
    ++mActiveFonts;
    return FontHandle{static_cast<std::uint32_t>(mFonts.size()), 1};
}

bool FontStore::addGlyphs(
    FontHandle handle,
    std::span<const GlyphMetrics> glyphs) {
    Entry* entry = findEntry(handle);
    return entry != nullptr && entry->face->appendGlyphs(glyphs);
}

bool FontStore::destroy(FontHandle handle) noexcept {
    Entry* entry = findEntry(handle);
    if (entry == nullptr) return false;
    entry->face.reset();
    --mActiveFonts;
    constexpr std::uint32_t invalidSlot = std::numeric_limits<std::uint32_t>::max();
    if (entry->generation != FontHandle::kMaxGeneration) {
        ++entry->generation;
        entry->nextFree = mFreeHead;
        mFreeHead = static_cast<std::uint32_t>(handle.value() - 1U);
    } else {
        entry->generation = 0;
        entry->nextFree = invalidSlot;
    }
    return true;
}

const FontFace* FontStore::find(FontHandle handle) const noexcept {
    const Entry* entry = findEntry(handle);
    return entry == nullptr ? nullptr : &*entry->face;
}

FontHandle FontStore::handleAt(std::size_t slotIndex) const noexcept {
    if (slotIndex >= mFonts.size() || !mFonts[slotIndex].face.has_value()) return {};
    return FontHandle{
        static_cast<std::uint32_t>(slotIndex + 1U),
        mFonts[slotIndex].generation,
    };
}

std::size_t FontStore::size() const noexcept { return mActiveFonts; }
std::size_t FontStore::slotCount() const noexcept { return mFonts.size(); }

std::size_t FontStore::storageBytes() const noexcept {
    std::size_t result = mFonts.capacity() * sizeof(Entry);
    for (const Entry& entry : mFonts) {
        if (entry.face.has_value()) result += entry.face->storageBytes();
    }
    return result;
}

FontStore::Entry* FontStore::findEntry(FontHandle handle) noexcept {
    if (!handle.valid() || handle.value() > mFonts.size()) return nullptr;
    Entry& entry = mFonts[handle.value() - 1U];
    return entry.face.has_value() && entry.generation == handle.generation() ? &entry : nullptr;
}

const FontStore::Entry* FontStore::findEntry(FontHandle handle) const noexcept {
    if (!handle.valid() || handle.value() > mFonts.size()) return nullptr;
    const Entry& entry = mFonts[handle.value() - 1U];
    return entry.face.has_value() && entry.generation == handle.generation() ? &entry : nullptr;
}

} // namespace henia::ui
