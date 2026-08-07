#include "henia/ui/text/FontStore.h"

#include <algorithm>
#include <cmath>

namespace henia::ui {
namespace {

[[nodiscard]] bool validScalar(char32_t value) noexcept {
    return value != U'\0' && value <= 0x10FFFFU
        && !(value >= 0xD800U && value <= 0xDFFFU);
}

[[nodiscard]] bool finiteRect(Rect value) noexcept {
    return std::isfinite(value.min.x) && std::isfinite(value.min.y)
        && std::isfinite(value.max.x) && std::isfinite(value.max.y);
}

[[nodiscard]] bool validGlyph(
    const GlyphMetrics& glyph,
    TextureHandle defaultAtlas) noexcept {
    const bool advanceOnly = glyph.size.x == 0.0F && glyph.size.y == 0.0F;
    const bool orderedUv = glyph.uv.min.x <= glyph.uv.max.x
        && glyph.uv.min.y <= glyph.uv.max.y;
    const bool drawable = glyph.size.x > 0.0F && glyph.size.y > 0.0F
        && (glyph.atlas.valid() || defaultAtlas.valid());
    return validScalar(glyph.codepoint)
        && (advanceOnly || drawable)
        && finiteRect(glyph.uv) && orderedUv
        && (advanceOnly || glyph.uv.valid())
        && std::isfinite(glyph.size.x) && std::isfinite(glyph.size.y)
        && glyph.size.x >= 0.0F && glyph.size.y >= 0.0F
        && std::isfinite(glyph.bearing.x) && std::isfinite(glyph.bearing.y)
        && std::isfinite(glyph.advance) && glyph.advance >= 0.0F
        && (glyph.rasterPlacement == GlyphRasterPlacement::Smooth
            || glyph.rasterPlacement == GlyphRasterPlacement::PixelAligned);
}

[[nodiscard]] bool validKerning(const KerningPair& pair) noexcept {
    return validScalar(pair.left) && validScalar(pair.right)
        && std::isfinite(pair.adjustment);
}

[[nodiscard]] bool validDefinition(const FontDefinition& definition) noexcept {
    if (!definition.atlas.valid()
        || !std::isfinite(definition.pixelSize) || definition.pixelSize <= 0.0F
        || !std::isfinite(definition.ascent) || definition.ascent < 0.0F
        || !std::isfinite(definition.descent) || definition.descent < 0.0F
        || !std::isfinite(definition.lineGap) || definition.lineGap < 0.0F
        || !std::isfinite(definition.ascent + definition.descent + definition.lineGap)
        || definition.ascent + definition.descent + definition.lineGap <= 0.0F
        || definition.glyphs.empty()) {
        return false;
    }
    return std::all_of(
               definition.glyphs.begin(), definition.glyphs.end(),
               [&definition](const GlyphMetrics& glyph) {
                   return validGlyph(glyph, definition.atlas);
               })
        && std::all_of(
            definition.kerning.begin(), definition.kerning.end(), validKerning);
}

} // namespace

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
    mGlyphIdIndex = buildGlyphIdIndex(mGlyphs);
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
    const auto iterator = std::lower_bound(
        mGlyphIdIndex.begin(),
        mGlyphIdIndex.end(),
        glyphId,
        [](const auto& entry, std::uint32_t value) { return entry.first < value; });
    return iterator == mGlyphIdIndex.end() || iterator->first != glyphId
        ? nullptr : &mGlyphs[iterator->second];
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
        + mGlyphIdIndex.capacity() * sizeof(decltype(mGlyphIdIndex)::value_type)
        + mKerning.capacity() * sizeof(KerningPair);
}

bool FontFace::appendGlyphs(std::span<const GlyphMetrics> glyphs) {
    if (glyphs.empty() || mRevision == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    std::vector<GlyphMetrics> replacement = mGlyphs;
    for (const GlyphMetrics& glyphMetrics : glyphs) {
        if (!validGlyph(glyphMetrics, mAtlas)) return false;
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
    std::vector<std::pair<std::uint32_t, std::size_t>> replacementIndex =
        buildGlyphIdIndex(replacement);
    mGlyphs = std::move(replacement);
    mGlyphIdIndex = std::move(replacementIndex);
    ++mRevision;
    return true;
}

std::uint64_t FontFace::kerningKey(char32_t left, char32_t right) noexcept {
    return (static_cast<std::uint64_t>(left) << 32U) | static_cast<std::uint32_t>(right);
}

std::vector<std::pair<std::uint32_t, std::size_t>> FontFace::buildGlyphIdIndex(
    std::span<const GlyphMetrics> glyphs) {
    std::vector<std::pair<std::uint32_t, std::size_t>> result;
    result.reserve(glyphs.size());
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        if (glyphs[index].glyphId != 0) result.emplace_back(glyphs[index].glyphId, index);
    }
    std::sort(result.begin(), result.end());
    return result;
}

FontHandle FontStore::add(FontDefinition definition) {
    if (!validDefinition(definition)) return {};
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
    PreparedGlyphUpdate prepared = prepareGlyphs(handle, glyphs);
    return prepared.valid() && commit(std::move(prepared));
}

PreparedGlyphUpdate FontStore::prepareGlyphs(
    FontHandle handle,
    std::span<const GlyphMetrics> glyphs) const {
    PreparedGlyphUpdate result;
    const Entry* entry = findEntry(handle);
    if (entry == nullptr || glyphs.empty()
        || entry->face->mRevision == std::numeric_limits<std::uint64_t>::max()) {
        return result;
    }
    result.mGlyphs = entry->face->mGlyphs;
    for (const GlyphMetrics& glyph : glyphs) {
        if (!validGlyph(glyph, entry->face->mAtlas)) return {};
        const auto iterator = std::lower_bound(
            result.mGlyphs.begin(), result.mGlyphs.end(), glyph.codepoint,
            [](const GlyphMetrics& existing, char32_t value) {
                return existing.codepoint < value;
            });
        if (iterator != result.mGlyphs.end() && iterator->codepoint == glyph.codepoint) {
            *iterator = glyph;
        } else {
            result.mGlyphs.insert(iterator, glyph);
        }
    }
    result.mGlyphIdIndex = FontFace::buildGlyphIdIndex(result.mGlyphs);
    result.mHandle = handle;
    result.mExpectedRevision = entry->face->mRevision;
    result.mReady = true;
    return result;
}

bool FontStore::commit(PreparedGlyphUpdate&& update) noexcept {
    Entry* entry = findEntry(update.mHandle);
    if (!update.mReady || entry == nullptr
        || entry->face->mRevision != update.mExpectedRevision) {
        return false;
    }
    entry->face->mGlyphs = std::move(update.mGlyphs);
    entry->face->mGlyphIdIndex = std::move(update.mGlyphIdIndex);
    ++entry->face->mRevision;
    update.mReady = false;
    return true;
}

bool FontStore::removeGlyphs(
    FontHandle handle,
    std::span<const char32_t> codepoints) {
    Entry* entry = findEntry(handle);
    if (entry == nullptr || codepoints.empty()
        || entry->face->mRevision == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    PreparedGlyphUpdate prepared;
    prepared.mGlyphs.reserve(entry->face->mGlyphs.size());
    for (const GlyphMetrics& glyph : entry->face->mGlyphs) {
        if (std::find(codepoints.begin(), codepoints.end(), glyph.codepoint)
            == codepoints.end()) {
            prepared.mGlyphs.push_back(glyph);
        }
    }
    if (prepared.mGlyphs.size() == entry->face->mGlyphs.size()) return true;
    prepared.mGlyphIdIndex = FontFace::buildGlyphIdIndex(prepared.mGlyphs);
    prepared.mHandle = handle;
    prepared.mExpectedRevision = entry->face->mRevision;
    prepared.mReady = true;
    return commit(std::move(prepared));
}

bool FontStore::restoreGlyphs(
    FontHandle handle,
    std::span<const GlyphMetrics> restored,
    std::span<const char32_t> removed) {
    Entry* entry = findEntry(handle);
    if (entry == nullptr
        || (restored.empty() && removed.empty())
        || entry->face->mRevision == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    PreparedGlyphUpdate prepared;
    prepared.mGlyphs = entry->face->mGlyphs;
    for (char32_t codepoint : removed) {
        const auto iterator = std::lower_bound(
            prepared.mGlyphs.begin(),
            prepared.mGlyphs.end(),
            codepoint,
            [](const GlyphMetrics& existing, char32_t value) {
                return existing.codepoint < value;
            });
        if (iterator != prepared.mGlyphs.end() && iterator->codepoint == codepoint) {
            prepared.mGlyphs.erase(iterator);
        }
    }
    for (const GlyphMetrics& glyph : restored) {
        if (!validGlyph(glyph, entry->face->mAtlas)) return false;
        const auto iterator = std::lower_bound(
            prepared.mGlyphs.begin(),
            prepared.mGlyphs.end(),
            glyph.codepoint,
            [](const GlyphMetrics& existing, char32_t value) {
                return existing.codepoint < value;
            });
        if (iterator != prepared.mGlyphs.end() && iterator->codepoint == glyph.codepoint) {
            *iterator = glyph;
        } else {
            prepared.mGlyphs.insert(iterator, glyph);
        }
    }
    prepared.mGlyphIdIndex = FontFace::buildGlyphIdIndex(prepared.mGlyphs);
    prepared.mHandle = handle;
    prepared.mExpectedRevision = entry->face->mRevision;
    prepared.mReady = true;
    return commit(std::move(prepared));
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
