#include "henia/ui/text/TextLayout.h"

#include "henia/ui/text/Utf8.h"

#include <algorithm>
#include <bit>

namespace henia::ui {

TextRunCache::TextRunCache(const FontStore& fonts) noexcept : mFonts(&fonts) {}

void TextRunCache::reserve(std::size_t entries, std::size_t glyphsPerEntry) {
    mEntries.reserve(entries);
    mIndex.reserve(entries);
    mGlyphReserve = glyphsPerEntry;
}

void TextRunCache::setMaximumEntries(std::size_t maximumEntries) {
    mMaximumEntries = maximumEntries;
    if (mMaximumEntries == 0) {
        clear();
        return;
    }
    while (mEntries.size() > mMaximumEntries) {
        const std::size_t last = mEntries.size() - 1;
        removeIndex(keyHash(mEntries[last].font, mEntries[last].size, mEntries[last].text), last);
        mEntries.pop_back();
    }
    mEvictionCursor = mEntries.empty() ? 0 : mEvictionCursor % mEntries.size();
}

const TextRun* TextRunCache::layout(FontHandle font, float size, std::string_view text) {
    if (size <= 0.0F || text.empty()) {
        return nullptr;
    }
    const FontFace* face = mFonts->find(font);
    if (face == nullptr || face->pixelSize() <= 0.0F || !face->atlas().valid()) {
        return nullptr;
    }
    const std::uint64_t hash = keyHash(font, size, text);
    const auto [begin, end] = mIndex.equal_range(hash);
    for (auto iterator = begin; iterator != end; ++iterator) {
        Entry& entry = mEntries[iterator->second];
        if (keyMatches(entry, font, size, text)) {
            ++mHits;
            return &entry.run;
        }
    }

    if (mMaximumEntries == 0) {
        return nullptr;
    }

    ++mMisses;
    if (mEntries.size() < mMaximumEntries) {
        Entry entry{};
        entry.font = font;
        entry.size = size;
        entry.text.assign(text);
        entry.run.glyphs.reserve(std::max(mGlyphReserve, text.size()));
        if (!buildRun(*face, size, text, entry.run)) {
            return nullptr;
        }
        mEntries.push_back(std::move(entry));
        mIndex.emplace(hash, mEntries.size() - 1);
        return &mEntries.back().run;
    }

    const std::size_t index = mEvictionCursor;
    Entry& replaced = mEntries[index];
    removeIndex(keyHash(replaced.font, replaced.size, replaced.text), index);
    replaced.font = font;
    replaced.size = size;
    replaced.text.assign(text);
    replaced.run.atlas = {};
    replaced.run.glyphs.clear();
    replaced.run.glyphs.reserve(std::max(mGlyphReserve, text.size()));
    replaced.run.metrics = {};
    if (!buildRun(*face, size, text, replaced.run)) {
        return nullptr;
    }
    mIndex.emplace(hash, index);
    mEvictionCursor = (mEvictionCursor + 1) % mEntries.size();
    return &replaced.run;
}

void TextRunCache::clear() noexcept {
    mEntries.clear();
    mIndex.clear();
    mEvictionCursor = 0;
}

std::size_t TextRunCache::size() const noexcept { return mEntries.size(); }

std::uint64_t TextRunCache::hits() const noexcept { return mHits; }

std::uint64_t TextRunCache::misses() const noexcept { return mMisses; }

void TextRunCache::removeIndex(std::uint64_t hash, std::size_t entryIndex) {
    const auto [begin, end] = mIndex.equal_range(hash);
    for (auto iterator = begin; iterator != end; ++iterator) {
        if (iterator->second == entryIndex) {
            mIndex.erase(iterator);
            return;
        }
    }
}

bool TextRunCache::keyMatches(
    const Entry& entry,
    FontHandle font,
    float size,
    std::string_view text) noexcept {
    return entry.font == font && entry.size == size && entry.text == text;
}

std::uint64_t TextRunCache::keyHash(
    FontHandle font,
    float size,
    std::string_view text) noexcept {
    constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offsetBasis;
    const auto mix = [&hash](std::uint8_t value) {
        hash ^= value;
        hash *= prime;
    };
    const std::uint32_t fontValue = font.packed();
    const std::uint32_t sizeBits = std::bit_cast<std::uint32_t>(size);
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        mix(static_cast<std::uint8_t>(fontValue >> shift));
    }
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        mix(static_cast<std::uint8_t>(sizeBits >> shift));
    }
    for (const unsigned char value : text) {
        mix(value);
    }
    return hash;
}

bool TextRunCache::buildRun(
    const FontFace& face,
    float size,
    std::string_view text,
    TextRun& output) const {
    if (face.pixelSize() <= 0.0F || !face.atlas().valid()) {
        return false;
    }

    const float scale = size / face.pixelSize();
    const float lineHeight = face.lineHeight() * scale;
    const float baseline = face.ascent() * scale;
    Vec2 cursor{0.0F, baseline};
    float maximumWidth = 0.0F;
    float totalHeight = lineHeight;
    char32_t previous = U'\0';

    for (std::size_t offset = 0; offset < text.size();) {
        const Utf8Codepoint decoded = decodeUtf8(text, offset);
        if (decoded.bytes == 0) {
            break;
        }
        offset += decoded.bytes;
        char32_t codepoint = decoded.valid ? decoded.value : U'\uFFFD';

        if (codepoint == U'\n') {
            maximumWidth = std::max(maximumWidth, cursor.x);
            cursor.x = 0.0F;
            cursor.y += lineHeight;
            totalHeight += lineHeight;
            previous = U'\0';
            continue;
        }

        const GlyphMetrics* glyph = face.glyph(codepoint);
        if (glyph == nullptr) {
            glyph = face.glyph(U'\uFFFD');
        }
        if (glyph == nullptr) {
            glyph = face.glyph(U'?');
        }
        if (glyph == nullptr) {
            previous = U'\0';
            continue;
        }

        cursor.x += face.kerning(previous, codepoint) * scale;
        const Vec2 glyphMin{
            cursor.x + glyph->bearing.x * scale,
            cursor.y - glyph->bearing.y * scale,
        };
        const Vec2 glyphMax{
            glyphMin.x + glyph->size.x * scale,
            glyphMin.y + glyph->size.y * scale,
        };
        if (glyph->size.x > 0.0F && glyph->size.y > 0.0F) {
            output.glyphs.push_back({{glyphMin, glyphMax}, glyph->uv});
        }
        cursor.x += glyph->advance * scale;
        previous = codepoint;
    }

    maximumWidth = std::max(maximumWidth, cursor.x);
    output.atlas = face.atlas();
    output.metrics = {maximumWidth, totalHeight};
    return true;
}

TextPainter::TextPainter(TextRunCache& cache) noexcept : mCache(&cache) {}

TextMetrics TextPainter::measure(FontHandle font, float size, std::string_view text) {
    const TextRun* run = mCache->layout(font, size, text);
    return run == nullptr ? TextMetrics{} : run->metrics;
}

void TextPainter::draw(
    Canvas& canvas,
    FontHandle font,
    float size,
    Vec2 origin,
    Color color,
    std::string_view text) {
    const TextRun* run = mCache->layout(font, size, text);
    if (run != nullptr) {
        canvas.glyphs(run->atlas, origin, run->glyphs, color);
    }
}

} // namespace henia::ui
