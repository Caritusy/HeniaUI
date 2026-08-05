#include "henia/ui/text/TextLayout.h"

#include "henia/ui/text/Utf8.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace henia::ui {
namespace {

constexpr std::uint64_t kHashOffset = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;

void mixHash(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kHashPrime;
}

void mixHash(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        mixHash(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void mixHash(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        mixHash(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

[[nodiscard]] const GlyphMetrics* resolveGlyph(
    const FontFace& face,
    const TextShapingGlyph& shaped) noexcept {
    if (shaped.glyphId != 0) {
        if (const GlyphMetrics* glyph = face.glyphById(shaped.glyphId)) return glyph;
    }
    return face.glyph(shaped.codepoint);
}

[[nodiscard]] const GlyphMetrics* resolveGlyph(
    const FontFace& face,
    const TextLayoutGlyph& layout) noexcept {
    if (layout.glyphId != 0) {
        if (const GlyphMetrics* glyph = face.glyphById(layout.glyphId)) return glyph;
    }
    return face.glyph(layout.codepoint);
}

} // namespace

TextLayoutCache::TextLayoutCache(
    const FontStore& fonts,
    const TextShapingBackend* shaping) noexcept
    : mFonts(&fonts), mShaping(shaping) {}

void TextLayoutCache::reserve(std::size_t entries, std::size_t glyphsPerEntry) {
    mEntries.reserve(entries);
    mIndex.reserve(entries);
    mGlyphReserve = glyphsPerEntry;
}

void TextLayoutCache::setMaximumEntries(std::size_t maximumEntries) {
    mMaximumEntries = maximumEntries;
    if (maximumEntries == 0) {
        clear();
        return;
    }
    while (mEntries.size() > maximumEntries) {
        const std::size_t last = mEntries.size() - 1U;
        removeIndex(keyHash(mEntries[last].fonts, mEntries[last].size, mEntries[last].text), last);
        mEntries.pop_back();
    }
    mEvictionCursor = mEntries.empty() ? 0 : mEvictionCursor % mEntries.size();
}

const TextLayoutResult* TextLayoutCache::layout(
    std::span<const FontHandle> fontChain,
    float size,
    std::string_view text) {
    if (!std::isfinite(size) || size <= 0.0F || fontChain.empty()) return nullptr;
    const std::uint64_t revisions = revisionHash(fontChain);
    if (revisions == 0) return nullptr;
    const std::uint64_t hash = keyHash(fontChain, size, text);
    const auto [begin, end] = mIndex.equal_range(hash);
    for (auto iterator = begin; iterator != end; ++iterator) {
        Entry& entry = mEntries[iterator->second];
        if (keyMatches(entry, fontChain, size, text, revisions)) {
            ++mHits;
            return &entry.result;
        }
    }
    if (mMaximumEntries == 0) return nullptr;

    Entry candidate{};
    candidate.fonts.assign(fontChain.begin(), fontChain.end());
    candidate.size = size;
    candidate.text.assign(text);
    candidate.result.glyphs.reserve(std::max(mGlyphReserve, text.size()));
    candidate.result.caretStops.reserve(std::max<std::size_t>(2U, text.size() + 1U));
    if (!build(fontChain, size, text, candidate.result)) return nullptr;
    candidate.result.identity = mNextIdentity++;
    if (candidate.result.identity == 0) {
        clear();
        mNextIdentity = 2;
        candidate.result.identity = 1;
    }
    ++mMisses;

    if (mEntries.size() < mMaximumEntries) {
        mEntries.push_back(std::move(candidate));
        mIndex.emplace(hash, mEntries.size() - 1U);
        return &mEntries.back().result;
    }

    const std::size_t index = mEvictionCursor;
    removeIndex(keyHash(mEntries[index].fonts, mEntries[index].size, mEntries[index].text), index);
    mEntries[index] = std::move(candidate);
    mIndex.emplace(hash, index);
    mEvictionCursor = (mEvictionCursor + 1U) % mEntries.size();
    return &mEntries[index].result;
}

void TextLayoutCache::clear() noexcept {
    mEntries.clear();
    mIndex.clear();
    mEvictionCursor = 0;
}

std::size_t TextLayoutCache::size() const noexcept { return mEntries.size(); }
std::uint64_t TextLayoutCache::hits() const noexcept { return mHits; }
std::uint64_t TextLayoutCache::misses() const noexcept { return mMisses; }

bool TextLayoutCache::build(
    std::span<const FontHandle> fontChain,
    float size,
    std::string_view text,
    TextLayoutResult& output) const {
    float lineHeight = 0.0F;
    float ascent = 0.0F;
    for (FontHandle handle : fontChain) {
        const FontFace* face = mFonts->find(handle);
        if (face == nullptr || face->pixelSize() <= 0.0F || !face->atlas().valid()) return false;
        const float scale = size / face->pixelSize();
        lineHeight = std::max(lineHeight, face->lineHeight() * scale);
        ascent = std::max(ascent, face->ascent() * scale);
    }
    if (!std::isfinite(lineHeight) || lineHeight <= 0.0F) lineHeight = size;
    if (!std::isfinite(ascent) || ascent < 0.0F) ascent = size;

    output.fontRevisionHash = revisionHash(fontChain);
    output.fonts.assign(fontChain.begin(), fontChain.end());
    output.glyphs.clear();
    output.caretStops.clear();
    float maximumWidth = 0.0F;
    std::uint32_t line = 0;
    std::size_t lineBegin = 0;
    std::vector<TextShapingGlyph> shaped;
    shaped.reserve(std::max(mGlyphReserve, text.size()));

    while (true) {
        const std::size_t newline = text.find('\n', lineBegin);
        const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view lineText = text.substr(lineBegin, lineEnd - lineBegin);
        const float lineY = static_cast<float>(line) * lineHeight;
        output.caretStops.push_back({lineBegin, {0.0F, lineY}, lineHeight, line});

        shaped.clear();
        const TextShapingRequest request{
            .fonts = mFonts,
            .fontChain = fontChain,
            .size = size,
            .text = lineText,
            .textByteOffset = lineBegin,
        };
        const bool shapedSuccessfully = mShaping != nullptr
            ? mShaping->shape(request, shaped)
            : shapeSimple(request, shaped);
        if (!shapedSuccessfully) return false;

        float cursor = 0.0F;
        for (const TextShapingGlyph& item : shaped) {
            if (item.byteBegin < lineBegin || item.byteEnd <= item.byteBegin
                || item.byteEnd > lineEnd || !std::isfinite(item.advance)
                || item.advance < 0.0F || !std::isfinite(item.offset.x)
                || !std::isfinite(item.offset.y)) {
                return false;
            }
            const FontFace* face = mFonts->find(item.font);
            if (face == nullptr || std::find(fontChain.begin(), fontChain.end(), item.font)
                    == fontChain.end()) {
                return false;
            }
            const GlyphMetrics* glyph = resolveGlyph(*face, item);
            if (glyph == nullptr) return false;
            const float scale = size / face->pixelSize();
            const Vec2 glyphMin{
                cursor + item.offset.x + glyph->bearing.x * scale,
                lineY + ascent + item.offset.y - glyph->bearing.y * scale,
            };
            const Vec2 glyphMax{
                glyphMin.x + glyph->size.x * scale,
                glyphMin.y + glyph->size.y * scale,
            };
            output.caretStops.push_back({item.byteBegin, {cursor, lineY}, lineHeight, line});
            if (glyph->size.x > 0.0F && glyph->size.y > 0.0F) {
                output.glyphs.push_back({
                    .font = item.font,
                    .glyphId = item.glyphId,
                    .codepoint = item.codepoint,
                    .byteBegin = item.byteBegin,
                    .byteEnd = item.byteEnd,
                    .bounds = {glyphMin, glyphMax},
                    .line = line,
                });
            }
            cursor += item.advance;
            output.caretStops.push_back({item.byteEnd, {cursor, lineY}, lineHeight, line});
        }
        maximumWidth = std::max(maximumWidth, cursor);
        output.caretStops.push_back({lineEnd, {cursor, lineY}, lineHeight, line});

        if (newline == std::string_view::npos) break;
        lineBegin = newline + 1U;
        ++line;
    }

    output.metrics = {
        maximumWidth,
        static_cast<float>(line + 1U) * lineHeight,
    };
    return true;
}

bool TextLayoutCache::shapeSimple(
    const TextShapingRequest& request,
    std::vector<TextShapingGlyph>& output) const {
    FontHandle previousFont{};
    char32_t previousCodepoint = U'\0';
    for (std::size_t offset = 0; offset < request.text.size();) {
        const Utf8Codepoint decoded = decodeUtf8(request.text, offset);
        if (decoded.bytes == 0) break;
        const std::size_t begin = request.textByteOffset + offset;
        offset += decoded.bytes;
        const char32_t requested = decoded.valid ? decoded.value : U'\uFFFD';

        FontHandle selectedFont{};
        const GlyphMetrics* selectedGlyph = nullptr;
        const FontFace* selectedFace = nullptr;
        char32_t selectedCodepoint = requested;
        for (FontHandle handle : request.fontChain) {
            const FontFace* face = request.fonts->find(handle);
            if (face != nullptr) {
                if (const GlyphMetrics* glyph = face->glyph(requested)) {
                    selectedFont = handle;
                    selectedGlyph = glyph;
                    selectedFace = face;
                    break;
                }
            }
        }
        if (selectedGlyph == nullptr) {
            for (char32_t fallback : {U'\uFFFD', U'?'}) {
                for (FontHandle handle : request.fontChain) {
                    const FontFace* face = request.fonts->find(handle);
                    if (face != nullptr) {
                        if (const GlyphMetrics* glyph = face->glyph(fallback)) {
                            selectedFont = handle;
                            selectedGlyph = glyph;
                            selectedFace = face;
                            selectedCodepoint = fallback;
                            break;
                        }
                    }
                }
                if (selectedGlyph != nullptr) break;
            }
        }
        if (selectedGlyph == nullptr || selectedFace == nullptr) {
            previousFont = {};
            previousCodepoint = U'\0';
            continue;
        }

        const float scale = request.size / selectedFace->pixelSize();
        const float kerning = selectedFont == previousFont
            ? selectedFace->kerning(previousCodepoint, selectedCodepoint) * scale
            : 0.0F;
        output.push_back({
            .font = selectedFont,
            .glyphId = selectedGlyph->glyphId,
            .codepoint = selectedCodepoint,
            .byteBegin = begin,
            .byteEnd = request.textByteOffset + offset,
            .offset = {kerning, 0.0F},
            .advance = kerning + selectedGlyph->advance * scale,
        });
        previousFont = selectedFont;
        previousCodepoint = selectedCodepoint;
    }
    return true;
}

std::uint64_t TextLayoutCache::revisionHash(
    std::span<const FontHandle> fonts) const noexcept {
    std::uint64_t hash = kHashOffset;
    for (FontHandle handle : fonts) {
        const FontFace* face = mFonts->find(handle);
        if (face == nullptr) return 0;
        mixHash(hash, handle.packed());
        mixHash(hash, face->revision());
    }
    return hash == 0 ? 1 : hash;
}

bool TextLayoutCache::keyMatches(
    const Entry& entry,
    std::span<const FontHandle> fonts,
    float size,
    std::string_view text,
    std::uint64_t revisions) noexcept {
    return entry.size == size && entry.text == text
        && entry.result.fontRevisionHash == revisions
        && entry.fonts.size() == fonts.size()
        && std::equal(entry.fonts.begin(), entry.fonts.end(), fonts.begin());
}

std::uint64_t TextLayoutCache::keyHash(
    std::span<const FontHandle> fonts,
    float size,
    std::string_view text) noexcept {
    std::uint64_t hash = kHashOffset;
    for (FontHandle handle : fonts) mixHash(hash, handle.packed());
    mixHash(hash, std::bit_cast<std::uint32_t>(size));
    for (unsigned char value : text) mixHash(hash, value);
    return hash;
}

void TextLayoutCache::removeIndex(std::uint64_t hash, std::size_t entryIndex) {
    const auto [begin, end] = mIndex.equal_range(hash);
    for (auto iterator = begin; iterator != end; ++iterator) {
        if (iterator->second == entryIndex) {
            mIndex.erase(iterator);
            return;
        }
    }
}

TextRenderCache::TextRenderCache(const FontStore& fonts) noexcept : mFonts(&fonts) {}

void TextRenderCache::reserve(std::size_t entries, std::size_t glyphsPerEntry) {
    mEntries.reserve(entries);
    mIndex.reserve(entries);
    mGlyphReserve = glyphsPerEntry;
}

void TextRenderCache::setMaximumEntries(std::size_t maximumEntries) {
    mMaximumEntries = maximumEntries;
    if (maximumEntries == 0) {
        clear();
        return;
    }
    while (mEntries.size() > maximumEntries) {
        mIndex.erase(mEntries.back().layoutIdentity);
        mEntries.pop_back();
    }
    mEvictionCursor = mEntries.empty() ? 0 : mEvictionCursor % mEntries.size();
}

const TextRun* TextRenderCache::render(const TextLayoutResult& layout) {
    if (layout.identity == 0 || revisionHash(layout.fonts) != layout.fontRevisionHash) {
        return nullptr;
    }
    if (const auto iterator = mIndex.find(layout.identity); iterator != mIndex.end()) {
        Entry& entry = mEntries[iterator->second];
        if (entry.fontRevisionHash == layout.fontRevisionHash) {
            ++mHits;
            return &entry.run;
        }
    }
    if (mMaximumEntries == 0) return nullptr;

    Entry candidate{
        .layoutIdentity = layout.identity,
        .fontRevisionHash = layout.fontRevisionHash,
    };
    if (!build(layout, candidate.run)) return nullptr;
    ++mMisses;
    if (mEntries.size() < mMaximumEntries) {
        mEntries.push_back(std::move(candidate));
        mIndex[mEntries.back().layoutIdentity] = mEntries.size() - 1U;
        return &mEntries.back().run;
    }

    const std::size_t index = mEvictionCursor;
    mIndex.erase(mEntries[index].layoutIdentity);
    mEntries[index] = std::move(candidate);
    mIndex[mEntries[index].layoutIdentity] = index;
    mEvictionCursor = (mEvictionCursor + 1U) % mEntries.size();
    return &mEntries[index].run;
}

void TextRenderCache::clear() noexcept {
    mEntries.clear();
    mIndex.clear();
    mEvictionCursor = 0;
}

std::size_t TextRenderCache::size() const noexcept { return mEntries.size(); }
std::uint64_t TextRenderCache::hits() const noexcept { return mHits; }
std::uint64_t TextRenderCache::misses() const noexcept { return mMisses; }

bool TextRenderCache::build(const TextLayoutResult& layout, TextRun& output) const {
    output.segments.clear();
    output.metrics = layout.metrics;
    for (const TextLayoutGlyph& item : layout.glyphs) {
        const FontFace* face = mFonts->find(item.font);
        if (face == nullptr) return false;
        const GlyphMetrics* glyph = resolveGlyph(*face, item);
        if (glyph == nullptr) return false;
        const TextureHandle atlas = face->atlasFor(*glyph);
        if (!atlas.valid()) return false;
        if (output.segments.empty() || output.segments.back().atlas != atlas) {
            TextRenderSegment segment{.atlas = atlas};
            segment.glyphs.reserve(mGlyphReserve);
            output.segments.push_back(std::move(segment));
        }
        output.segments.back().glyphs.push_back({item.bounds, glyph->uv});
    }
    return true;
}

std::uint64_t TextRenderCache::revisionHash(
    std::span<const FontHandle> fonts) const noexcept {
    std::uint64_t hash = kHashOffset;
    for (FontHandle handle : fonts) {
        const FontFace* face = mFonts->find(handle);
        if (face == nullptr) return 0;
        mixHash(hash, handle.packed());
        mixHash(hash, face->revision());
    }
    return hash == 0 ? 1 : hash;
}

TextRunCache::TextRunCache(
    const FontStore& fonts,
    const TextShapingBackend* shaping) noexcept
    : mLayouts(fonts, shaping), mRendering(fonts) {}

void TextRunCache::reserve(std::size_t entries, std::size_t glyphsPerEntry) {
    mLayouts.reserve(entries, glyphsPerEntry);
    mRendering.reserve(entries, glyphsPerEntry);
}

void TextRunCache::setMaximumEntries(std::size_t maximumEntries) {
    mLayouts.setMaximumEntries(maximumEntries);
    mRendering.setMaximumEntries(maximumEntries);
}

const TextRun* TextRunCache::layout(
    FontHandle font,
    float size,
    std::string_view text) {
    const std::array fonts{font};
    return layout(fonts, size, text);
}

const TextRun* TextRunCache::layout(
    std::span<const FontHandle> fontChain,
    float size,
    std::string_view text) {
    if (text.empty()) return nullptr;
    const TextLayoutResult* result = mLayouts.layout(fontChain, size, text);
    return result == nullptr ? nullptr : mRendering.render(*result);
}

const TextLayoutResult* TextRunCache::layoutResult(
    std::span<const FontHandle> fontChain,
    float size,
    std::string_view text) {
    return mLayouts.layout(fontChain, size, text);
}

const TextRun* TextRunCache::renderLayout(const TextLayoutResult& layout) {
    return mRendering.render(layout);
}

void TextRunCache::clear() noexcept {
    mLayouts.clear();
    mRendering.clear();
}

std::size_t TextRunCache::size() const noexcept { return mLayouts.size(); }
std::uint64_t TextRunCache::hits() const noexcept { return mLayouts.hits(); }
std::uint64_t TextRunCache::misses() const noexcept { return mLayouts.misses(); }
const TextLayoutCache& TextRunCache::layoutCache() const noexcept { return mLayouts; }
const TextRenderCache& TextRunCache::renderCache() const noexcept { return mRendering; }

TextPainter::TextPainter(TextRunCache& cache) noexcept : mCache(&cache) {}

void TextPainter::setFallbackFonts(std::span<const FontHandle> fonts) {
    std::vector<FontHandle> filtered;
    filtered.reserve(fonts.size());
    for (FontHandle font : fonts) {
        if (font.valid() && std::find(filtered.begin(), filtered.end(), font) == filtered.end()) {
            filtered.push_back(font);
        }
    }
    if (filtered == mFallbackFonts) return;
    mFallbackFonts = std::move(filtered);
    mResolvedFonts.reserve(mFallbackFonts.size() + 1U);
    mCache->clear();
}

std::span<const FontHandle> TextPainter::fallbackFonts() const noexcept {
    return mFallbackFonts;
}

void TextPainter::setGlyphRequestBackend(TextGlyphRequestBackend* backend) noexcept {
    mGlyphRequests = backend;
}

TextGlyphRequestBackend* TextPainter::glyphRequestBackend() const noexcept {
    return mGlyphRequests;
}

std::span<const FontHandle> TextPainter::resolvedFonts(FontHandle primary) {
    if (mFallbackFonts.empty()) {
        mResolvedPrimary = primary;
        return primary.valid()
            ? std::span<const FontHandle>(&mResolvedPrimary, 1)
            : std::span<const FontHandle>{};
    }
    mResolvedFonts.clear();
    if (primary.valid()) mResolvedFonts.push_back(primary);
    for (FontHandle fallback : mFallbackFonts) {
        if (fallback != primary) mResolvedFonts.push_back(fallback);
    }
    return mResolvedFonts;
}

TextMetrics TextPainter::measure(FontHandle font, float size, std::string_view text) {
    if (text.empty()) return {};
    return measure(resolvedFonts(font), size, text);
}

TextMetrics TextPainter::measure(
    std::span<const FontHandle> fontChain,
    float size,
    std::string_view text) {
    if (text.empty()) return {};
    if (mGlyphRequests != nullptr) mGlyphRequests->requestText(text);
    const TextLayoutResult* result = mCache->layoutResult(fontChain, size, text);
    return result == nullptr ? TextMetrics{} : result->metrics;
}

const TextLayoutResult* TextPainter::layout(
    FontHandle font,
    float size,
    std::string_view text) {
    return layout(resolvedFonts(font), size, text);
}

const TextLayoutResult* TextPainter::layout(
    std::span<const FontHandle> fontChain,
    float size,
    std::string_view text) {
    if (!text.empty() && mGlyphRequests != nullptr) mGlyphRequests->requestText(text);
    return mCache->layoutResult(fontChain, size, text);
}

void TextPainter::draw(
    Canvas& canvas,
    FontHandle font,
    float size,
    Vec2 origin,
    Color color,
    std::string_view text) {
    draw(canvas, resolvedFonts(font), size, origin, color, text);
}

void TextPainter::draw(
    Canvas& canvas,
    std::span<const FontHandle> fontChain,
    float size,
    Vec2 origin,
    Color color,
    std::string_view text) {
    if (text.empty()) return;
    if (mGlyphRequests != nullptr) mGlyphRequests->requestText(text);
    const TextLayoutResult* result = mCache->layoutResult(fontChain, size, text);
    if (result != nullptr) drawLayout(canvas, *result, origin, color);
}

void TextPainter::drawLayout(
    Canvas& canvas,
    const TextLayoutResult& layout,
    Vec2 origin,
    Color color) {
    const TextRun* run = mCache->renderLayout(layout);
    if (run == nullptr) return;
    for (const TextRenderSegment& segment : run->segments) {
        canvas.glyphs(segment.atlas, origin, segment.glyphs, color);
    }
}

std::size_t TextPainter::hitTest(
    const TextLayoutResult& layout,
    Vec2 position) noexcept {
    if (layout.caretStops.empty()) return 0;
    std::uint32_t bestLine = layout.caretStops.front().line;
    float bestLineDistance = std::numeric_limits<float>::max();
    for (const TextCaretStop& stop : layout.caretStops) {
        const float centerY = stop.position.y + stop.lineHeight * 0.5F;
        const float distance = std::abs(position.y - centerY);
        if (distance < bestLineDistance) {
            bestLineDistance = distance;
            bestLine = stop.line;
        }
    }
    const TextCaretStop* best = &layout.caretStops.front();
    float bestDistance = std::numeric_limits<float>::max();
    for (const TextCaretStop& stop : layout.caretStops) {
        if (stop.line != bestLine) continue;
        const float distance = std::abs(position.x - stop.position.x);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &stop;
        }
    }
    return best->byteOffset;
}

Rect TextPainter::visualBounds(const TextLayoutResult& layout) noexcept {
    if (layout.glyphs.empty()) {
        return {{0.0F, 0.0F}, {layout.metrics.width, layout.metrics.height}};
    }
    Rect result = layout.glyphs.front().bounds;
    for (const TextLayoutGlyph& glyph : layout.glyphs) {
        result.min.x = std::min(result.min.x, glyph.bounds.min.x);
        result.min.y = std::min(result.min.y, glyph.bounds.min.y);
        result.max.x = std::max(result.max.x, glyph.bounds.max.x);
        result.max.y = std::max(result.max.y, glyph.bounds.max.y);
    }
    return result;
}

Vec2 TextPainter::centeredVisualOrigin(
    const TextLayoutResult& layout,
    Rect bounds) noexcept {
    const Rect visual = visualBounds(layout);
    return {
        (bounds.min.x + bounds.max.x - visual.min.x - visual.max.x) * 0.5F,
        (bounds.min.y + bounds.max.y - visual.min.y - visual.max.y) * 0.5F,
    };
}

Vec2 TextPainter::caretPosition(
    const TextLayoutResult& layout,
    std::size_t byteOffset) noexcept {
    if (layout.caretStops.empty()) return {};
    const TextCaretStop* best = &layout.caretStops.front();
    std::size_t bestDistance = std::numeric_limits<std::size_t>::max();
    for (const TextCaretStop& stop : layout.caretStops) {
        const std::size_t distance = stop.byteOffset > byteOffset
            ? stop.byteOffset - byteOffset
            : byteOffset - stop.byteOffset;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &stop;
        }
    }
    return best->position;
}

std::vector<Rect> TextPainter::selectionRects(
    const TextLayoutResult& layout,
    std::size_t byteBegin,
    std::size_t byteEnd) {
    if (byteBegin > byteEnd) std::swap(byteBegin, byteEnd);
    std::vector<Rect> result;
    if (byteBegin == byteEnd) return result;
    std::uint32_t maximumLine = 0;
    for (const TextCaretStop& stop : layout.caretStops) maximumLine = std::max(maximumLine, stop.line);
    for (std::uint32_t line = 0; line <= maximumLine; ++line) {
        const TextCaretStop* first = nullptr;
        const TextCaretStop* last = nullptr;
        const TextCaretStop* beginStop = nullptr;
        const TextCaretStop* endStop = nullptr;
        std::size_t beginDistance = std::numeric_limits<std::size_t>::max();
        std::size_t endDistance = std::numeric_limits<std::size_t>::max();
        for (const TextCaretStop& stop : layout.caretStops) {
            if (stop.line != line) continue;
            if (first == nullptr || stop.byteOffset < first->byteOffset) first = &stop;
            if (last == nullptr || stop.byteOffset > last->byteOffset) last = &stop;
            const std::size_t distanceBegin = stop.byteOffset > byteBegin
                ? stop.byteOffset - byteBegin
                : byteBegin - stop.byteOffset;
            const std::size_t distanceEnd = stop.byteOffset > byteEnd
                ? stop.byteOffset - byteEnd
                : byteEnd - stop.byteOffset;
            if (distanceBegin < beginDistance) {
                beginDistance = distanceBegin;
                beginStop = &stop;
            }
            if (distanceEnd < endDistance) {
                endDistance = distanceEnd;
                endStop = &stop;
            }
        }
        if (first == nullptr || last == nullptr || byteEnd <= first->byteOffset
            || byteBegin >= last->byteOffset) {
            continue;
        }
        if (byteBegin <= first->byteOffset) beginStop = first;
        if (byteEnd >= last->byteOffset) endStop = last;
        if (beginStop == nullptr || endStop == nullptr) continue;
        const float minimumX = std::min(beginStop->position.x, endStop->position.x);
        const float maximumX = std::max(beginStop->position.x, endStop->position.x);
        if (maximumX > minimumX) {
            result.push_back({
                {minimumX, first->position.y},
                {maximumX, first->position.y + first->lineHeight},
            });
        }
    }
    return result;
}

} // namespace henia::ui
