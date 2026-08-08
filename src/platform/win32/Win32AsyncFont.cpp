#include "henia/ui/platform/win32/Win32AsyncFont.h"

#include "henia/CheckedArithmetic.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/text/Utf8.h"
#include "DirectWriteGlyphRasterizer.h"

#define NOMINMAX
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <semaphore>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace henia::ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kRoleCount = 7;
constexpr std::size_t kLocaleCount = 5;

[[nodiscard]] constexpr std::size_t roleIndex(Win32FontRole role) noexcept {
    return static_cast<std::size_t>(role);
}

[[nodiscard]] constexpr std::size_t localeIndex(Win32FontLocale locale) noexcept {
    const std::size_t index = static_cast<std::size_t>(locale);
    return index < kLocaleCount ? index : 0;
}

[[nodiscard]] constexpr std::uint32_t roleBit(Win32FontRole role) noexcept {
    return 1U << static_cast<unsigned int>(role);
}

[[nodiscard]] constexpr bool between(
    char32_t value,
    char32_t first,
    char32_t last) noexcept {
    return value >= first && value <= last;
}

[[nodiscard]] constexpr bool validScalar(char32_t value) noexcept {
    return value >= U' ' && value <= 0x10FFFFU
        && !(value >= 0xD800U && value <= 0xDFFFU)
        && !(value >= 0x7FU && value <= 0x9FU);
}

[[nodiscard]] constexpr bool isHan(char32_t value) noexcept {
    return between(value, 0x3400U, 0x4DBFU)
        || between(value, 0x4E00U, 0x9FFFU)
        || between(value, 0xF900U, 0xFAFFU)
        || between(value, 0x20000U, 0x2FA1FU)
        || between(value, 0x30000U, 0x3134FU);
}

[[nodiscard]] constexpr bool isEmoji(char32_t value) noexcept {
    return between(value, 0x1F000U, 0x1FAFFU)
        || between(value, 0x2600U, 0x27BFU)
        || value == 0xFE0FU;
}

[[nodiscard]] constexpr std::uint32_t routeMask(char32_t value) noexcept {
    constexpr std::uint32_t primary = roleBit(Win32FontRole::Primary);
    constexpr std::uint32_t simplified = roleBit(Win32FontRole::SimplifiedChinese);
    constexpr std::uint32_t traditional = roleBit(Win32FontRole::TraditionalChinese);
    constexpr std::uint32_t japanese = roleBit(Win32FontRole::Japanese);
    constexpr std::uint32_t korean = roleBit(Win32FontRole::Korean);
    constexpr std::uint32_t symbols = roleBit(Win32FontRole::Symbols);
    constexpr std::uint32_t emoji = roleBit(Win32FontRole::Emoji);
    constexpr std::uint32_t allCjk = simplified | traditional | japanese | korean;

    if (isHan(value)) return allCjk;
    if (between(value, 0x3040U, 0x30FFU)
        || between(value, 0x31F0U, 0x31FFU)) {
        return japanese;
    }
    if (between(value, 0x1100U, 0x11FFU)
        || between(value, 0x3130U, 0x318FU)
        || between(value, 0xA960U, 0xA97FU)
        || between(value, 0xAC00U, 0xD7AFU)
        || between(value, 0xD7B0U, 0xD7FFU)) {
        return korean;
    }
    if (between(value, 0x3100U, 0x312FU)
        || between(value, 0x31A0U, 0x31BFU)) {
        return simplified | traditional;
    }
    if (between(value, 0x3000U, 0x303FU)
        || between(value, 0xFE30U, 0xFE4FU)
        || between(value, 0xFF00U, 0xFFEFU)) {
        return allCjk | symbols;
    }
    if (isEmoji(value)) return emoji | symbols;
    if (between(value, 0x2000U, 0x2BFFU)
        || between(value, 0x1D400U, 0x1D7FFU)) {
        return symbols | primary;
    }
    return primary | symbols;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
template <typename Value>
class SpscQueue final {
public:
    explicit SpscQueue(std::size_t capacity)
        : mSlots(capacity + 1U) {}

    [[nodiscard]] bool push(Value&& value) noexcept {
        const std::size_t tail = mTail.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == mHead.load(std::memory_order_acquire)) return false;
        mSlots[tail].emplace(std::move(value));
        mTail.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(Value& value) noexcept {
        const std::size_t head = mHead.load(std::memory_order_relaxed);
        if (head == mTail.load(std::memory_order_acquire)) return false;
        value = std::move(*mSlots[head]);
        mSlots[head].reset();
        mHead.store(increment(head), std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t head = mHead.load(std::memory_order_acquire);
        const std::size_t tail = mTail.load(std::memory_order_acquire);
        return tail >= head ? tail - head : mSlots.size() - head + tail;
    }

private:
    [[nodiscard]] std::size_t increment(std::size_t value) const noexcept {
        ++value;
        return value == mSlots.size() ? 0 : value;
    }

    std::vector<std::optional<Value>> mSlots;
    alignas(64) std::atomic_size_t mHead{0};
    alignas(64) std::atomic_size_t mTail{0};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

enum class BakeStatus : std::uint8_t {
    Ready,
    Missing,
    Failed,
};

struct BakeJob final {
    std::uint8_t face = 0;
    std::uint8_t variant = 0;
    char32_t codepoint = U'\0';
    float physicalPixelHeight = 0.0F;
    float pixelsPerLogicalUnit = 1.0F;
    float pixelAlignedMaximumPhysicalHeight = 0.0F;
};

struct OwnedGlyph final {
    char32_t codepoint = U'\0';
    std::uint32_t glyphId = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    Vec2 bearing{};
    float advance = 0.0F;
    Vec2 logicalSize{};
    GlyphRasterPlacement rasterPlacement = GlyphRasterPlacement::Smooth;
    std::vector<std::byte> pixels;

    [[nodiscard]] RasterizedGlyph view() const noexcept {
        return {
            .codepoint = codepoint,
            .glyphId = glyphId,
            .width = width,
            .height = height,
            .rowPitch = rowPitch,
            .bearing = bearing,
            .advance = advance,
            .pixels = pixels,
            .logicalSize = logicalSize,
            .rasterPlacement = rasterPlacement,
        };
    }
};

struct BakeResult final {
    std::uint8_t face = 0;
    std::uint8_t variant = 0;
    BakeStatus status = BakeStatus::Failed;
    OwnedGlyph glyph;
};

enum class GlyphJobState : std::uint8_t {
    InFlight,
    Missing,
    Resident,
    Retryable,
};

struct GlyphJobRecord final {
    GlyphJobState state = GlyphJobState::InFlight;
    std::uint8_t failures = 0;
    std::uint64_t retryAfterRequest = 0;
};

[[nodiscard]] bool makeDirectWriteFace(
    IDWriteFontCollection& collection,
    std::wstring_view familyName,
    ComPtr<IDWriteFontFace>& output) noexcept {
    if (familyName.empty()) return false;
    const std::wstring family(familyName);
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    if (FAILED(collection.FindFamilyName(family.c_str(), &familyIndex, &exists))
        || exists == FALSE) {
        return false;
    }
    ComPtr<IDWriteFontFamily> fontFamily;
    if (FAILED(collection.GetFontFamily(familyIndex, &fontFamily))) return false;
    ComPtr<IDWriteFont> font;
    if (FAILED(fontFamily->GetFirstMatchingFont(
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            &font))) {
        return false;
    }
    return SUCCEEDED(font->CreateFontFace(&output));
}

[[nodiscard]] BakeResult rasterize(
    IDWriteFactory& factory,
    IDWriteFontFace& face,
    BakeJob job,
    float physicalPixelHeight,
    float pixelsPerLogicalUnit,
    float pixelAlignedMaximumPhysicalHeight) {
    BakeResult result{.face = job.face, .variant = job.variant};
    result.glyph.codepoint = job.codepoint;
    const UINT32 codepoint = static_cast<UINT32>(job.codepoint);
    UINT16 glyphIndex = 0;
    if (FAILED(face.GetGlyphIndices(&codepoint, 1, &glyphIndex))) return result;
    if (glyphIndex == 0) {
        result.status = BakeStatus::Missing;
        return result;
    }

    DWRITE_FONT_METRICS fontMetrics{};
    face.GetMetrics(&fontMetrics);
    if (fontMetrics.designUnitsPerEm == 0) return result;
    DWRITE_GLYPH_METRICS glyphMetrics{};
    if (FAILED(face.GetDesignGlyphMetrics(&glyphIndex, 1, &glyphMetrics, FALSE))) {
        return result;
    }

    const float logicalScale = physicalPixelHeight
        / static_cast<float>(fontMetrics.designUnitsPerEm) / pixelsPerLogicalUnit;
    const float advance = static_cast<float>(glyphMetrics.advanceWidth) * logicalScale;
    if (!std::isfinite(advance) || advance < 0.0F) return result;

    const DWRITE_GLYPH_RUN run{
        .fontFace = &face,
        .fontEmSize = physicalPixelHeight,
        .glyphCount = 1,
        .glyphIndices = &glyphIndex,
        .glyphAdvances = nullptr,
        .glyphOffsets = nullptr,
        .isSideways = FALSE,
        .bidiLevel = 0,
    };
    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    const bool pixelAligned = pixelAlignedMaximumPhysicalHeight > 0.0F
        && physicalPixelHeight <= pixelAlignedMaximumPhysicalHeight;
    if (FAILED(factory.CreateGlyphRunAnalysis(
            &run,
            1.0F,
            nullptr,
            pixelAligned ? DWRITE_RENDERING_MODE_NATURAL
                         : DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            DWRITE_MEASURING_MODE_NATURAL,
            0.0F,
            0.0F,
            &analysis))) {
        return result;
    }

    RECT bounds{};
    if (FAILED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds))) {
        return result;
    }
    result.glyph.glyphId = glyphIndex;
    result.glyph.advance = advance;
    result.glyph.rasterPlacement = pixelAligned
        ? GlyphRasterPlacement::PixelAligned
        : GlyphRasterPlacement::Smooth;
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) {
        result.status = BakeStatus::Ready;
        return result;
    }

    const auto width = static_cast<std::uint64_t>(bounds.right - bounds.left);
    const auto height = static_cast<std::uint64_t>(bounds.bottom - bounds.top);
    if (width > std::numeric_limits<std::uint32_t>::max()
        || height > std::numeric_limits<std::uint32_t>::max()) {
        return result;
    }
    std::size_t pixelCount = 0;
    std::size_t clearTypeBytes = 0;
    if (!checkedMultiply(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            pixelCount)
        || !checkedMultiply(pixelCount, std::size_t{3}, clearTypeBytes)
        || clearTypeBytes > std::numeric_limits<UINT32>::max()) {
        return result;
    }

    std::vector<std::byte> clearType(clearTypeBytes);
    if (FAILED(analysis->CreateAlphaTexture(
            DWRITE_TEXTURE_CLEARTYPE_3x1,
            &bounds,
            reinterpret_cast<BYTE*>(clearType.data()),
            static_cast<UINT32>(clearType.size())))) {
        return result;
    }
    result.glyph.pixels.resize(pixelCount);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const auto red = static_cast<unsigned int>(
            static_cast<unsigned char>(clearType[index * 3U]));
        const auto green = static_cast<unsigned int>(
            static_cast<unsigned char>(clearType[index * 3U + 1U]));
        const auto blue = static_cast<unsigned int>(
            static_cast<unsigned char>(clearType[index * 3U + 2U]));
        // Alpha8 is grayscale coverage. Averaging the three ClearType samples
        // preserves aggregate coverage without the stroke-weight bias caused
        // by selecting the strongest subpixel channel.
        result.glyph.pixels[index] = static_cast<std::byte>((red + green + blue + 1U) / 3U);
    }

    result.glyph.width = static_cast<std::uint32_t>(width);
    result.glyph.height = static_cast<std::uint32_t>(height);
    result.glyph.rowPitch = result.glyph.width;
    result.glyph.bearing = {
        static_cast<float>(bounds.left) / pixelsPerLogicalUnit,
        -static_cast<float>(bounds.top) / pixelsPerLogicalUnit,
    };
    result.glyph.logicalSize = {
        static_cast<float>(width) / pixelsPerLogicalUnit,
        static_cast<float>(height) / pixelsPerLogicalUnit,
    };
    result.status = BakeStatus::Ready;
    return result;
}

} // namespace

class Win32AsyncFontSet::Impl final {
public:
    Impl(
        TextureStore& textures,
        FontStore& fonts,
        const Win32AsyncFontConfiguration& configuration)
        : mTextures(&textures),
          mFonts(&fonts),
          mOwnerThread(std::this_thread::get_id()),
          mConfiguration(configuration),
          mBucketCapacity(normalizedBucketCapacity(configuration.maximumRasterSizeBuckets)),
          mRequests(normalizedCapacity(configuration.requestQueueCapacity)),
          mResults(normalizedCapacity(configuration.resultQueueCapacity)),
          mResultSpace(static_cast<std::ptrdiff_t>(
              normalizedCapacity(configuration.resultQueueCapacity))),
          mInjectedRasterizationFailures(configuration.injectedRasterizationFailures),
          mInjectedCommitFailures(configuration.injectedCommitFailures) {
        try {
            if (!initialize()) {
                static_cast<void>(releaseResources());
                return;
            }
            const std::size_t targetCapacity = kRoleCount * mBucketCapacity;
            mCommitBatches.resize(targetCapacity);
            mRasterizedViews.resize(targetCapacity);
            for (auto& batch : mCommitBatches) {
                batch.reserve(normalizedCapacity(configuration.resultQueueCapacity));
            }
            for (auto& views : mRasterizedViews) {
                views.reserve(normalizedCapacity(configuration.resultQueueCapacity));
            }
            mWorker = std::jthread([this](std::stop_token stop) noexcept { workerMain(stop); });
        } catch (...) {
            static_cast<void>(releaseResources());
            try {
                mLastError = "Win32 asynchronous font initialization exhausted CPU storage";
            } catch (...) {}
        }
    }

    ~Impl() noexcept {
        if (!releaseResources()) stopWorker();
    }

    [[nodiscard]] bool valid() const noexcept { return mValid; }
    [[nodiscard]] std::string_view lastError() const noexcept { return mLastError; }

    [[nodiscard]] FontHandle font(Win32FontRole role) const noexcept {
        const std::size_t index = roleIndex(role);
        return index < mFaces.size() ? mFaces[index].font : FontHandle{};
    }

    [[nodiscard]] std::span<const FontHandle> fontChain(
        Win32FontLocale locale) const noexcept {
        return mChains[localeIndex(locale)];
    }

    [[nodiscard]] std::size_t request(std::span<const char32_t> codepoints) {
        if (!ownerThread()) return 0;
        ++mRequestEpoch;
        std::size_t queued = 0;
        for (char32_t codepoint : codepoints) {
            queued += requestOne(
                codepoint,
                mChains[localeIndex(Win32FontLocale::Default)]).queued;
        }
        return queued;
    }

    [[nodiscard]] std::size_t request(
        std::span<const FontHandle> fontChain,
        std::span<const char32_t> codepoints) {
        if (!ownerThread()) return 0;
        ++mRequestEpoch;
        std::size_t queued = 0;
        for (char32_t codepoint : codepoints) {
            queued += requestOne(codepoint, fontChain).queued;
        }
        return queued;
    }

    [[nodiscard]] std::size_t requestUtf8(std::string_view text) {
        return requestUtf8(mChains[localeIndex(Win32FontLocale::Default)], text);
    }

    [[nodiscard]] std::size_t requestUtf8(
        std::span<const FontHandle> fontChain,
        std::string_view text) {
        if (!ownerThread()) return 0;
        ++mRequestEpoch;
        std::size_t queued = 0;
        for (std::size_t offset = 0; offset < text.size();) {
            const Utf8Codepoint decoded = decodeUtf8(text, offset);
            if (decoded.bytes == 0) break;
            offset += decoded.bytes;
            queued += requestOne(
                decoded.valid ? decoded.value : U'\uFFFD',
                fontChain).queued;
        }
        return queued;
    }

    [[nodiscard]] TextPreparationStatus prepareText(
        std::span<const FontHandle> fontChain,
        float logicalPixelSize,
        std::string_view text) {
        static_cast<void>(logicalPixelSize);
        if (!ownerThread()) return TextPreparationStatus::Pending;
        ++mRequestEpoch;
        TextPreparationStatus status = TextPreparationStatus::Ready;
        for (std::size_t offset = 0; offset < text.size();) {
            const Utf8Codepoint decoded = decodeUtf8(text, offset);
            if (decoded.bytes == 0) break;
            offset += decoded.bytes;
            if (requestOne(decoded.valid ? decoded.value : U'\uFFFD', fontChain).status
                == TextPreparationStatus::Pending) {
                status = TextPreparationStatus::Pending;
            }
        }
        return status;
    }

    [[nodiscard]] FontHandle resolveFont(
        FontHandle font,
        float logicalPixelSize) {
        if (!ownerThread() || !mValid || !font.valid()
            || !std::isfinite(logicalPixelSize) || logicalPixelSize <= 0.0F) {
            return font;
        }
        const auto located = locateTarget(font);
        if (!located.has_value()) return font;
        Face& face = mFaces[located->face];
        const double physical = static_cast<double>(logicalPixelSize) * mDpiScale;
        if (physical < 1.0 || physical > static_cast<double>(std::numeric_limits<int>::max())) {
            return font;
        }
        const std::uint64_t physicalKey = detail::physicalSizeKey(physical, mPhysicalSizeSteps);
        if (physicalKey == 0) return font;
        const float physicalPixelHeight = detail::physicalSizeFromKey(
            physicalKey, mPhysicalSizeSteps);

        if (face.physicalSizeKey == physicalKey) return face.font;
        for (const RasterVariant& variant : face.variants) {
            if (variant.physicalSizeKey == physicalKey
                && mFonts->find(variant.font) != nullptr) {
                return variant.font;
            }
        }
        if (!rememberRasterBucket(physicalKey)) {
            ++mRasterVariantLimitFallbacks;
            return closestVariant(face, physicalKey);
        }
        if (located->face == roleIndex(Win32FontRole::Primary)
            && mConfiguration.primaryRasterResolver != nullptr) {
            const FontHandle resolved = mConfiguration.primaryRasterResolver->resolveFont(
                face.font, logicalPixelSize);
            if (resolved.valid()) {
                if (resolved == face.font) return face.font;
                const auto retained = std::find_if(
                    face.variants.begin(),
                    face.variants.end(),
                    [resolved](const RasterVariant& variant) {
                        return variant.font == resolved;
                    });
                if (retained != face.variants.end()) return retained->font;
                if (mFonts->find(resolved) == nullptr
                    || face.variants.size() + 1U >= mBucketCapacity) {
                    return closestVariant(face, physicalKey);
                }
                try {
                    auto atlas = std::make_unique<DynamicGlyphAtlas>(
                        *mTextures, *mFonts, resolved, mConfiguration.dynamicAtlas);
                    face.variants.push_back({
                        .font = resolved,
                        .atlas = std::move(atlas),
                        .physicalSizeKey = physicalKey,
                        .physicalPixelHeight = physicalPixelHeight,
                        .logicalPixelHeight = logicalPixelSize,
                        .pixelsPerLogicalUnit = mDpiScale,
                    });
                    ++mRasterVariantCount;
                    return resolved;
                } catch (...) {
                    return closestVariant(face, physicalKey);
                }
            }
        }
        const FontHandle created = createVariant(
            static_cast<std::uint8_t>(located->face),
            logicalPixelSize,
            physicalKey,
            physicalPixelHeight);
        return created.valid() ? created : closestVariant(face, physicalKey);
    }

    [[nodiscard]] bool prewarmTextSizes(std::span<const float> logicalPixelSizes) {
        if (!ownerThread() || logicalPixelSizes.empty()) return false;
        for (float size : logicalPixelSizes) {
            for (const Face& face : mFaces) {
                if (face.font.valid() && !resolveFont(face.font, size).valid()) return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool setDpiScale(float dpiScale) noexcept {
        if (!ownerThread() || !std::isfinite(dpiScale) || dpiScale <= 0.0F) return false;
        mDpiScale = dpiScale;
        return true;
    }

    [[nodiscard]] std::size_t commitReady(std::size_t maximumResults) {
        if (!ownerThread() || maximumResults == 0) return 0;
        for (auto& batch : mCommitBatches) batch.clear();

        std::size_t consumed = 0;
        BakeResult result{};
        while (consumed < maximumResults && mResults.pop(result)) {
            mResultSpace.release();
            ++consumed;
            const std::uint16_t target = targetSlot(result.face, result.variant);
            if (target >= mCommitBatches.size()) continue;
            const std::uint64_t key = jobKey(target, result.glyph.codepoint);
            if (result.status == BakeStatus::Ready) {
                mCommitBatches[target].push_back(std::move(result.glyph));
            } else if (result.status == BakeStatus::Missing) {
                mJobStates[key] = {.state = GlyphJobState::Missing};
                ++mPermanentMissingGlyphs;
            } else {
                markRetryable(key);
            }
        }

        std::size_t committed = 0;
        for (std::size_t target = 0; target < mCommitBatches.size(); ++target) {
            auto& batch = mCommitBatches[target];
            if (batch.empty()) continue;
            auto& views = mRasterizedViews[target];
            views.clear();
            for (const OwnedGlyph& glyph : batch) views.push_back(glyph.view());
            const std::size_t faceIndex = target / mBucketCapacity;
            const std::size_t variantIndex = target % mBucketCapacity;
            DynamicGlyphAtlas* atlas = nullptr;
            if (faceIndex < mFaces.size()) {
                Face& face = mFaces[faceIndex];
                if (variantIndex == 0) {
                    atlas = face.atlas.get();
                } else if (variantIndex - 1U < face.variants.size()) {
                    atlas = face.variants[variantIndex - 1U].atlas.get();
                }
            }
            const bool injectedFailure = mInjectedCommitFailures != 0;
            if (injectedFailure) --mInjectedCommitFailures;
            if (!injectedFailure && atlas != nullptr && atlas->add(views)) {
                committed += batch.size();
                mCommittedGlyphs += batch.size();
                for (const OwnedGlyph& glyph : batch) {
                    mJobStates[jobKey(static_cast<std::uint16_t>(target), glyph.codepoint)] = {
                        .state = GlyphJobState::Resident,
                    };
                }
            } else {
                mCommitFailures += batch.size();
                for (const OwnedGlyph& glyph : batch) {
                    markRetryable(jobKey(static_cast<std::uint16_t>(target), glyph.codepoint));
                }
            }
        }
        return committed;
    }

    [[nodiscard]] bool releaseResources() noexcept {
        if (mReleased) return true;
        if (!ownerThread()) return false;
        stopWorker();

        bool released = true;
        for (Face& face : mFaces) {
            for (RasterVariant& variant : face.variants) {
                if (variant.atlas != nullptr) {
                    released = variant.atlas->releaseResources() && released;
                    variant.atlas.reset();
                }
                if (variant.ownsFont && variant.font.valid()) {
                    TextureHandle seedAtlas{};
                    if (variant.ownsSeedAtlas) {
                        if (const FontFace* stored = mFonts->find(variant.font); stored != nullptr) {
                            seedAtlas = stored->atlas();
                        }
                    }
                    if (mFonts->find(variant.font) != nullptr) {
                        released = mFonts->destroy(variant.font) && released;
                    }
                    if (seedAtlas.valid() && mTextures->view(seedAtlas).handle.valid()) {
                        released = mTextures->destroy(seedAtlas) && released;
                    }
                }
            }
            face.variants.clear();
            if (face.atlas != nullptr) {
                released = face.atlas->releaseResources() && released;
                face.atlas.reset();
            }
            if (face.ownsFont && face.font.valid()) {
                TextureHandle seedAtlas{};
                if (face.ownsSeedAtlas) {
                    if (const FontFace* stored = mFonts->find(face.font); stored != nullptr) {
                        seedAtlas = stored->atlas();
                    }
                }
                if (mFonts->find(face.font) != nullptr) {
                    released = mFonts->destroy(face.font) && released;
                }
                if (seedAtlas.valid() && mTextures->view(seedAtlas).handle.valid()) {
                    released = mTextures->destroy(seedAtlas) && released;
                }
            }
            face.font = {};
            face.family.clear();
            face.ownsFont = false;
            face.ownsSeedAtlas = false;
        }
        if (!released) return false;
        for (auto& chain : mChains) chain.clear();
        mJobStates.clear();
        mValid = false;
        mReleased = true;
        mFaceCount = 0;
        return true;
    }

    [[nodiscard]] bool idle() const noexcept {
        return mPendingBakeJobs.load(std::memory_order_acquire) == 0
            && mResults.size() == 0;
    }

    [[nodiscard]] Win32AsyncFontStatistics statistics() const noexcept {
        return {
            .faces = mFaceCount,
            .rasterSizeBuckets = mRasterBuckets.size(),
            .rasterVariants = mRasterVariantCount,
            .pendingBakeJobs = mPendingBakeJobs.load(std::memory_order_acquire),
            .readyResults = mResults.size(),
            .uniqueCodepointsRequested = mUniqueCodepointsRequested,
            .bakeJobsQueued = mBakeJobsQueued,
            .deduplicatedBakeJobs = mDeduplicatedBakeJobs,
            .deduplicatedInFlight = mDeduplicatedInFlight,
            .residentGlyphsSkipped = mResidentGlyphsSkipped,
            .unsupportedCodepoints = mUnsupportedCodepoints,
            .permanentMissingGlyphs = mPermanentMissingGlyphs,
            .retryableFailures = mRetryableFailures,
            .retriesQueued = mRetriesQueued,
            .requestQueueFull = mRequestQueueFull,
            .candidateFaceProbes = mCandidateFaceProbes,
            .avoidedFanoutJobs = mAvoidedFanoutJobs,
            .rasterVariantLimitFallbacks = mRasterVariantLimitFallbacks,
            .rasterizedGlyphs = mRasterizedGlyphs.load(std::memory_order_relaxed),
            .missingGlyphs = mMissingGlyphs.load(std::memory_order_relaxed),
            .rasterizationFailures = mRasterizationFailures.load(std::memory_order_relaxed),
            .committedGlyphs = mCommittedGlyphs,
            .commitFailures = mCommitFailures,
            .wrongThreadCalls = mWrongThreadCalls.load(std::memory_order_relaxed),
        };
    }

private:
    struct RasterVariant final {
        FontHandle font{};
        std::unique_ptr<DynamicGlyphAtlas> atlas;
        std::uint64_t physicalSizeKey = 0;
        float physicalPixelHeight = 0.0F;
        float logicalPixelHeight = 0.0F;
        float pixelsPerLogicalUnit = 1.0F;
        bool ownsFont = false;
        bool ownsSeedAtlas = false;
    };

    struct Face final {
        std::wstring family;
        FontHandle font{};
        std::unique_ptr<DynamicGlyphAtlas> atlas;
        std::vector<RasterVariant> variants;
        std::uint64_t physicalSizeKey = 0;
        float physicalPixelHeight = 0.0F;
        float logicalPixelHeight = 0.0F;
        float pixelsPerLogicalUnit = 1.0F;
        bool ownsFont = false;
        bool ownsSeedAtlas = false;
    };

    [[nodiscard]] std::uint16_t targetSlot(
        std::uint8_t face,
        std::uint8_t variant) const noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::size_t>(face) * mBucketCapacity + variant);
    }

    [[nodiscard]] static std::uint64_t jobKey(
        std::uint16_t target,
        char32_t codepoint) noexcept {
        return (static_cast<std::uint64_t>(target) << 32U)
            | static_cast<std::uint32_t>(codepoint);
    }

    [[nodiscard]] RasterVariant* variantAt(
        std::uint8_t faceIndex,
        std::uint8_t variantIndex) noexcept {
        if (faceIndex >= mFaces.size() || variantIndex == 0) return nullptr;
        Face& face = mFaces[faceIndex];
        const std::size_t index = static_cast<std::size_t>(variantIndex - 1U);
        return index < face.variants.size() ? &face.variants[index] : nullptr;
    }

    struct Target final {
        std::uint8_t face = 0;
        std::uint8_t variant = 0;
        FontHandle font{};
        DynamicGlyphAtlas* atlas = nullptr;
        float physicalPixelHeight = 0.0F;
        float pixelsPerLogicalUnit = 1.0F;
    };

    [[nodiscard]] std::optional<Target> locateTarget(FontHandle handle) noexcept {
        for (std::size_t faceIndex = 0; faceIndex < mFaces.size(); ++faceIndex) {
            Face& face = mFaces[faceIndex];
            if (face.font == handle) {
                return Target{
                    .face = static_cast<std::uint8_t>(faceIndex),
                    .font = face.font,
                    .atlas = face.atlas.get(),
                    .physicalPixelHeight = face.physicalPixelHeight,
                    .pixelsPerLogicalUnit = face.pixelsPerLogicalUnit,
                };
            }
            for (std::size_t variantIndex = 0;
                 variantIndex < face.variants.size(); ++variantIndex) {
                RasterVariant& variant = face.variants[variantIndex];
                if (variant.font == handle) {
                    return Target{
                        .face = static_cast<std::uint8_t>(faceIndex),
                        .variant = static_cast<std::uint8_t>(variantIndex + 1U),
                        .font = variant.font,
                        .atlas = variant.atlas.get(),
                        .physicalPixelHeight = variant.physicalPixelHeight,
                        .pixelsPerLogicalUnit = variant.pixelsPerLogicalUnit,
                    };
                }
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool rememberRasterBucket(std::uint64_t physicalSizeKey) {
        if (std::find(mRasterBuckets.begin(), mRasterBuckets.end(), physicalSizeKey)
            != mRasterBuckets.end()) {
            return true;
        }
        if (mRasterBuckets.size() >= mBucketCapacity) return false;
        mRasterBuckets.push_back(physicalSizeKey);
        return true;
    }

    [[nodiscard]] FontHandle closestVariant(
        const Face& face,
        std::uint64_t physicalSizeKey) const noexcept {
        FontHandle closest = face.font;
        std::uint64_t distance = face.physicalSizeKey > physicalSizeKey
            ? static_cast<std::uint64_t>(face.physicalSizeKey - physicalSizeKey)
            : static_cast<std::uint64_t>(physicalSizeKey - face.physicalSizeKey);
        for (const RasterVariant& variant : face.variants) {
            const std::uint64_t candidateDistance = variant.physicalSizeKey > physicalSizeKey
                ? static_cast<std::uint64_t>(variant.physicalSizeKey - physicalSizeKey)
                : static_cast<std::uint64_t>(physicalSizeKey - variant.physicalSizeKey);
            if (candidateDistance < distance && mFonts->find(variant.font) != nullptr) {
                closest = variant.font;
                distance = candidateDistance;
            }
        }
        return closest;
    }

    [[nodiscard]] FontHandle createVariant(
        std::uint8_t faceIndex,
        float logicalPixelHeight,
        std::uint64_t physicalSizeKey,
        float physicalPixelHeight) {
        if (faceIndex >= mFaces.size()) return {};
        Face& face = mFaces[faceIndex];
        if (face.variants.size() + 1U >= mBucketCapacity) return {};

        FontHandle variantFont{};
        bool ownsSeedAtlas = false;
        if (faceIndex == roleIndex(Win32FontRole::Primary)) {
            constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
            const double atlasScale = static_cast<double>(physicalPixelHeight)
                / static_cast<double>(std::max(face.physicalPixelHeight, 1.0F));
            const auto scaledDimension = [atlasScale](std::uint32_t dimension) {
                const double scaled = std::ceil(static_cast<double>(dimension) * atlasScale);
                return scaled >= 1.0
                    && scaled <= static_cast<double>(std::numeric_limits<std::uint32_t>::max())
                    ? static_cast<std::uint32_t>(scaled) : std::uint32_t{0};
            };
            const std::uint32_t atlasWidth = scaledDimension(mConfiguration.initialAtlasWidth);
            const std::uint32_t atlasHeight = scaledDimension(mConfiguration.initialAtlasHeight);
            if (atlasWidth == 0 || atlasHeight == 0) return {};
            variantFont = Win32FontLoader::load(
                *mTextures,
                *mFonts,
                {
                    .family = face.family,
                    .pixelHeight = static_cast<std::uint32_t>(std::ceil(physicalPixelHeight)),
                    .atlasWidth = atlasWidth,
                    .atlasHeight = atlasHeight,
                    .ranges = ranges,
                    .metricsScale = mDpiScale,
                    .physicalPixelHeight = physicalPixelHeight,
                    .logicalPixelHeight = logicalPixelHeight,
                    .pixelAlignedMaximumPhysicalHeight =
                        mConfiguration.pixelAlignedMaximumPhysicalHeight,
                });
            ownsSeedAtlas = variantFont.valid();
        } else {
            const FontFace* base = mFonts->find(face.font);
            if (base == nullptr || base->pixelSize() <= 0.0F) return {};
            const float metricScale = logicalPixelHeight / base->pixelSize();
            const GlyphMetrics* seed = base->glyph(U' ');
            if (seed == nullptr) seed = base->glyph(U'A');
            if (seed == nullptr) return {};
            GlyphMetrics scaledSeed = *seed;
            scaledSeed.size = {
                scaledSeed.size.x * metricScale,
                scaledSeed.size.y * metricScale,
            };
            scaledSeed.bearing = {
                scaledSeed.bearing.x * metricScale,
                scaledSeed.bearing.y * metricScale,
            };
            scaledSeed.advance *= metricScale;
            variantFont = mFonts->add({
                .atlas = base->atlas(),
                .pixelSize = logicalPixelHeight,
                .ascent = base->ascent() * metricScale,
                .descent = base->descent() * metricScale,
                .lineGap = base->lineGap() * metricScale,
                .glyphs = {scaledSeed},
            });
        }
        if (!variantFont.valid()) return {};

        std::unique_ptr<DynamicGlyphAtlas> atlas;
        try {
            atlas = std::make_unique<DynamicGlyphAtlas>(
                *mTextures, *mFonts, variantFont, mConfiguration.dynamicAtlas);
        } catch (...) {
            TextureHandle seedAtlas{};
            if (ownsSeedAtlas) {
                if (const FontFace* stored = mFonts->find(variantFont); stored != nullptr) {
                    seedAtlas = stored->atlas();
                }
            }
            static_cast<void>(mFonts->destroy(variantFont));
            if (seedAtlas.valid()) static_cast<void>(mTextures->destroy(seedAtlas));
            return {};
        }
        face.variants.push_back({
            .font = variantFont,
            .atlas = std::move(atlas),
            .physicalSizeKey = physicalSizeKey,
            .physicalPixelHeight = physicalPixelHeight,
            .logicalPixelHeight = logicalPixelHeight,
            .pixelsPerLogicalUnit = mDpiScale,
            .ownsFont = true,
            .ownsSeedAtlas = ownsSeedAtlas,
        });
        ++mRasterVariantCount;
        return variantFont;
    }

    void stopWorker() noexcept {
        if (!mWorker.joinable()) return;
        mWorker.request_stop();
        mWorkAvailable.release();
        mResultSpace.release();
        mWorker.join();
    }

    void markRetryable(std::uint64_t key) {
        GlyphJobRecord& record = mJobStates[key];
        record.state = GlyphJobState::Retryable;
        record.failures = static_cast<std::uint8_t>(
            std::min<unsigned int>(record.failures + 1U, 63U));
        const std::uint8_t shift = std::min<std::uint8_t>(record.failures - 1U, 6U);
        record.retryAfterRequest = mRequestEpoch + (std::uint64_t{1} << shift);
        ++mRetryableFailures;
    }

    [[nodiscard]] static std::size_t normalizedCapacity(std::size_t requested) noexcept {
        constexpr std::size_t maximum = 65'536;
        return std::clamp(requested, std::size_t{1}, maximum);
    }

    [[nodiscard]] static std::size_t normalizedBucketCapacity(
        std::size_t requested) noexcept {
        return std::clamp(requested, std::size_t{1}, std::size_t{64});
    }

    [[nodiscard]] bool initialize() {
        if (!std::isfinite(mConfiguration.logicalPixelHeight)
            || mConfiguration.logicalPixelHeight <= 0.0F
            || !std::isfinite(mConfiguration.dpiScale)
            || mConfiguration.dpiScale <= 0.0F
            || mConfiguration.initialAtlasWidth == 0
            || mConfiguration.initialAtlasHeight == 0
            || mConfiguration.dynamicAtlas.pageWidth == 0
            || mConfiguration.dynamicAtlas.pageHeight == 0
            || mConfiguration.dynamicAtlas.maximumPages == 0
            || mConfiguration.maximumRasterSizeBuckets == 0
            || !std::isfinite(mConfiguration.pixelAlignedMaximumPhysicalHeight)
            || mConfiguration.pixelAlignedMaximumPhysicalHeight < 0.0F
            || mConfiguration.preallocatedPagesPerFace
                > mConfiguration.dynamicAtlas.maximumPages) {
            mLastError = "Invalid asynchronous font configuration";
            return false;
        }
        const double physicalHeight = static_cast<double>(mConfiguration.logicalPixelHeight)
            * static_cast<double>(mConfiguration.dpiScale);
        if (physicalHeight < 1.0
            || physicalHeight > std::numeric_limits<std::uint32_t>::max()) {
            mLastError = "Asynchronous font pixel height is out of range";
            return false;
        }
        mPhysicalSizeSteps = detail::normalizedPhysicalSizeSteps(
            mConfiguration.physicalSizeStepsPerPixel);
        mPhysicalSizeKey = detail::physicalSizeKey(physicalHeight, mPhysicalSizeSteps);
        if (mPhysicalSizeKey == 0) {
            mLastError = "Asynchronous font pixel height is out of range";
            return false;
        }
        mPhysicalPixelHeight = detail::physicalSizeFromKey(
            mPhysicalSizeKey, mPhysicalSizeSteps);
        mDpiScale = mConfiguration.dpiScale;
        mRasterBuckets.reserve(mBucketCapacity);
        mRasterBuckets.push_back(mPhysicalSizeKey);

        const std::array families{
            mConfiguration.families.primary,
            mConfiguration.families.simplifiedChinese,
            mConfiguration.families.traditionalChinese,
            mConfiguration.families.japanese,
            mConfiguration.families.korean,
            mConfiguration.families.symbols,
            mConfiguration.families.emoji,
        };
        for (std::size_t index = 0; index < families.size(); ++index) {
            if (families[index].empty()) continue;
            Face& face = mFaces[index];
            face.family.assign(families[index]);
            face.variants.reserve(mBucketCapacity > 0 ? mBucketCapacity - 1U : 0U);
            face.physicalSizeKey = mPhysicalSizeKey;
            face.physicalPixelHeight = mPhysicalPixelHeight;
            face.logicalPixelHeight = mConfiguration.logicalPixelHeight;
            face.pixelsPerLogicalUnit = mDpiScale;
            if (index == roleIndex(Win32FontRole::Primary)
                && mConfiguration.primaryFont.valid()) {
                face.font = mConfiguration.primaryFont;
                face.ownsFont = false;
                face.ownsSeedAtlas = false;
                if (mFonts->find(face.font) == nullptr) {
                    mLastError = "The supplied primary FontHandle is not in the FontStore";
                    return false;
                }
            } else {
                constexpr std::array ranges{UnicodeRange{U' ', U'~'}};
                face.font = Win32FontLoader::load(
                    *mTextures,
                    *mFonts,
                    {
                        .family = face.family,
                        .pixelHeight = static_cast<std::uint32_t>(
                            std::ceil(mPhysicalPixelHeight)),
                        .atlasWidth = mConfiguration.initialAtlasWidth,
                        .atlasHeight = mConfiguration.initialAtlasHeight,
                        .ranges = ranges,
                        .metricsScale = mDpiScale,
                        .physicalPixelHeight = mPhysicalPixelHeight,
                        .logicalPixelHeight = mConfiguration.logicalPixelHeight,
                        .pixelAlignedMaximumPhysicalHeight =
                            mConfiguration.pixelAlignedMaximumPhysicalHeight,
                    });
                face.ownsFont = face.font.valid();
                face.ownsSeedAtlas = face.font.valid();
            }
            if (!face.font.valid()) {
                face.family.clear();
                continue;
            }
            face.atlas = std::make_unique<DynamicGlyphAtlas>(
                *mTextures,
                *mFonts,
                face.font,
                mConfiguration.dynamicAtlas);
            if (mConfiguration.preallocatedPagesPerFace != 0
                && !face.atlas->reservePages(mConfiguration.preallocatedPagesPerFace)) {
                if (index == roleIndex(Win32FontRole::Primary)) {
                    mLastError = "Unable to preallocate the primary dynamic glyph atlas";
                    return false;
                }
                static_cast<void>(face.atlas->releaseResources());
                face.atlas.reset();
                if (face.ownsFont) {
                    TextureHandle seedAtlas{};
                    if (const FontFace* stored = mFonts->find(face.font); stored != nullptr) {
                        seedAtlas = stored->atlas();
                    }
                    static_cast<void>(mFonts->destroy(face.font));
                    if (seedAtlas.valid()) static_cast<void>(mTextures->destroy(seedAtlas));
                }
                face.font = {};
                face.ownsFont = false;
                face.ownsSeedAtlas = false;
                face.family.clear();
                continue;
            }
            ++mFaceCount;
        }
        if (!mFaces[roleIndex(Win32FontRole::Primary)].font.valid()
            || mFaces[roleIndex(Win32FontRole::Primary)].atlas == nullptr) {
            mLastError = "No usable primary font face was created";
            return false;
        }
        buildChains();
        mValid = true;
        return true;
    }

    void buildChains() {
        const auto build = [this](
                               Win32FontLocale locale,
                               std::initializer_list<Win32FontRole> order) {
            std::vector<FontHandle>& chain = mChains[localeIndex(locale)];
            chain.clear();
            chain.reserve(kRoleCount);
            for (Win32FontRole role : order) {
                const FontHandle handle = mFaces[roleIndex(role)].font;
                if (handle.valid()
                    && std::find(chain.begin(), chain.end(), handle) == chain.end()) {
                    chain.push_back(handle);
                }
            }
        };
        build(Win32FontLocale::Default, {
            Win32FontRole::Primary,
            Win32FontRole::SimplifiedChinese,
            Win32FontRole::TraditionalChinese,
            Win32FontRole::Japanese,
            Win32FontRole::Korean,
            Win32FontRole::Symbols,
            Win32FontRole::Emoji,
        });
        build(Win32FontLocale::SimplifiedChinese, {
            Win32FontRole::Primary,
            Win32FontRole::SimplifiedChinese,
            Win32FontRole::TraditionalChinese,
            Win32FontRole::Japanese,
            Win32FontRole::Korean,
            Win32FontRole::Symbols,
            Win32FontRole::Emoji,
        });
        build(Win32FontLocale::TraditionalChinese, {
            Win32FontRole::Primary,
            Win32FontRole::TraditionalChinese,
            Win32FontRole::SimplifiedChinese,
            Win32FontRole::Japanese,
            Win32FontRole::Korean,
            Win32FontRole::Symbols,
            Win32FontRole::Emoji,
        });
        build(Win32FontLocale::Japanese, {
            Win32FontRole::Primary,
            Win32FontRole::Japanese,
            Win32FontRole::SimplifiedChinese,
            Win32FontRole::TraditionalChinese,
            Win32FontRole::Korean,
            Win32FontRole::Symbols,
            Win32FontRole::Emoji,
        });
        build(Win32FontLocale::Korean, {
            Win32FontRole::Primary,
            Win32FontRole::Korean,
            Win32FontRole::SimplifiedChinese,
            Win32FontRole::TraditionalChinese,
            Win32FontRole::Japanese,
            Win32FontRole::Symbols,
            Win32FontRole::Emoji,
        });
    }

    [[nodiscard]] bool ownerThread() const noexcept {
        if (std::this_thread::get_id() == mOwnerThread) return true;
        mWrongThreadCalls.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    struct RequestOutcome final {
        std::size_t queued = 0;
        TextPreparationStatus status = TextPreparationStatus::Ready;
    };

    [[nodiscard]] RequestOutcome requestOne(
        char32_t codepoint,
        std::span<const FontHandle> fontChain) {
        // Layout text commonly contains line separators; they are not glyph
        // requests and should not inflate unsupported-codepoint telemetry.
        if (codepoint < U' ') return {};
        if (!validScalar(codepoint)) {
            ++mUnsupportedCodepoints;
            return {};
        }
        if (mUniqueCodepoints.insert(codepoint).second) ++mUniqueCodepointsRequested;
        const std::uint32_t routes = routeMask(codepoint);
        for (std::size_t chainIndex = 0; chainIndex < fontChain.size(); ++chainIndex) {
            const FontHandle candidate = fontChain[chainIndex];
            const auto target = locateTarget(candidate);
            if (!target.has_value()) continue;
            ++mCandidateFaceProbes;
            if ((routes & (1U << static_cast<unsigned int>(target->face))) == 0
                || target->atlas == nullptr) {
                continue;
            }
            const std::uint16_t slot = targetSlot(target->face, target->variant);
            const std::uint64_t key = jobKey(slot, codepoint);
            const FontFace* fontFace = mFonts->find(target->font);
            if (fontFace != nullptr && fontFace->glyph(codepoint) != nullptr) {
                mJobStates[key] = {.state = GlyphJobState::Resident};
                ++mResidentGlyphsSkipped;
                return {};
            }

            bool retry = false;
            if (const auto state = mJobStates.find(key); state != mJobStates.end()) {
                switch (state->second.state) {
                    case GlyphJobState::InFlight:
                        ++mDeduplicatedBakeJobs;
                        ++mDeduplicatedInFlight;
                        return {.status = TextPreparationStatus::Pending};
                    case GlyphJobState::Missing:
                        continue;
                    case GlyphJobState::Resident:
                        mJobStates.erase(state);
                        break;
                    case GlyphJobState::Retryable:
                        if (mRequestEpoch < state->second.retryAfterRequest) {
                            ++mDeduplicatedBakeJobs;
                            return {.status = TextPreparationStatus::Pending};
                        }
                        retry = true;
                        break;
                }
            }
            BakeJob job{
                .face = target->face,
                .variant = target->variant,
                .codepoint = codepoint,
                .physicalPixelHeight = target->physicalPixelHeight,
                .pixelsPerLogicalUnit = target->pixelsPerLogicalUnit,
                .pixelAlignedMaximumPhysicalHeight =
                    mConfiguration.pixelAlignedMaximumPhysicalHeight,
            };
            if (!mRequests.push(std::move(job))) {
                ++mRequestQueueFull;
                return {.status = TextPreparationStatus::Pending};
            }
            GlyphJobRecord& record = mJobStates[key];
            record.state = GlyphJobState::InFlight;
            mPendingBakeJobs.fetch_add(1, std::memory_order_release);
            mWorkAvailable.release();
            ++mBakeJobsQueued;
            if (retry) ++mRetriesQueued;

            std::size_t laterCandidates = 0;
            for (std::size_t later = chainIndex + 1U; later < fontChain.size(); ++later) {
                const auto laterTarget = locateTarget(fontChain[later]);
                if (laterTarget.has_value()
                    && (routes & (1U << static_cast<unsigned int>(laterTarget->face))) != 0
                    && laterTarget->atlas != nullptr) {
                    ++laterCandidates;
                }
            }
            mAvoidedFanoutJobs += laterCandidates;
            return {.queued = 1, .status = TextPreparationStatus::Pending};
        }
        return {};
    }

    void workerMain(std::stop_token stop) noexcept {
        ComPtr<IDWriteFactory> factory;
        const HRESULT factoryResult = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
        ComPtr<IDWriteFontCollection> collection;
        if (SUCCEEDED(factoryResult)) {
            static_cast<void>(factory->GetSystemFontCollection(&collection, FALSE));
        }
        std::array<ComPtr<IDWriteFontFace>, kRoleCount> workerFaces;
        if (collection != nullptr) {
            for (std::size_t index = 0; index < mFaces.size(); ++index) {
                static_cast<void>(makeDirectWriteFace(
                    *collection.Get(),
                    mFaces[index].family,
                    workerFaces[index]));
            }
        }

        while (!stop.stop_requested()) {
            mWorkAvailable.acquire();
            if (stop.stop_requested()) break;
            BakeJob job{};
            if (!mRequests.pop(job)) continue;

            BakeResult result{.face = job.face, .variant = job.variant};
            result.glyph.codepoint = job.codepoint;
            try {
                if (mInjectedRasterizationFailures != 0) {
                    --mInjectedRasterizationFailures;
                    result.status = BakeStatus::Failed;
                } else if (factory != nullptr && job.face < workerFaces.size()
                    && workerFaces[job.face] != nullptr) {
                    result = rasterize(
                        *factory.Get(),
                        *workerFaces[job.face].Get(),
                        job,
                        job.physicalPixelHeight,
                        job.pixelsPerLogicalUnit,
                        job.pixelAlignedMaximumPhysicalHeight);
                } else {
                    result.status = BakeStatus::Missing;
                }
            } catch (...) {
                result.status = BakeStatus::Failed;
            }

            switch (result.status) {
                case BakeStatus::Ready:
                    mRasterizedGlyphs.fetch_add(1, std::memory_order_relaxed);
                    break;
                case BakeStatus::Missing:
                    mMissingGlyphs.fetch_add(1, std::memory_order_relaxed);
                    break;
                case BakeStatus::Failed:
                    mRasterizationFailures.fetch_add(1, std::memory_order_relaxed);
                    break;
            }
            mResultSpace.acquire();
            if (stop.stop_requested()) return;
            static_cast<void>(mResults.push(std::move(result)));
            mPendingBakeJobs.fetch_sub(1, std::memory_order_release);
        }
    }

    TextureStore* mTextures = nullptr;
    FontStore* mFonts = nullptr;
    std::thread::id mOwnerThread{};
    Win32AsyncFontConfiguration mConfiguration{};
    std::size_t mBucketCapacity = 1;
    std::array<Face, kRoleCount> mFaces;
    std::array<std::vector<FontHandle>, kLocaleCount> mChains;
    SpscQueue<BakeJob> mRequests;
    SpscQueue<BakeResult> mResults;
    std::counting_semaphore<> mWorkAvailable{0};
    std::counting_semaphore<> mResultSpace;
    std::jthread mWorker;
    std::vector<std::vector<OwnedGlyph>> mCommitBatches;
    std::vector<std::vector<RasterizedGlyph>> mRasterizedViews;
    std::vector<std::uint64_t> mRasterBuckets;
    std::unordered_set<char32_t> mUniqueCodepoints;
    std::unordered_map<std::uint64_t, GlyphJobRecord> mJobStates;
    std::string mLastError;
    std::size_t mFaceCount = 0;
    std::uint32_t mPhysicalSizeSteps = 8;
    std::uint64_t mPhysicalSizeKey = 0;
    float mPhysicalPixelHeight = 0.0F;
    float mDpiScale = 1.0F;
    std::atomic_size_t mPendingBakeJobs{0};
    std::atomic_uint64_t mRasterizedGlyphs{0};
    std::atomic_uint64_t mMissingGlyphs{0};
    std::atomic_uint64_t mRasterizationFailures{0};
    mutable std::atomic_uint64_t mWrongThreadCalls{0};
    std::uint64_t mUniqueCodepointsRequested = 0;
    std::uint64_t mBakeJobsQueued = 0;
    std::uint64_t mDeduplicatedBakeJobs = 0;
    std::uint64_t mDeduplicatedInFlight = 0;
    std::uint64_t mResidentGlyphsSkipped = 0;
    std::uint64_t mUnsupportedCodepoints = 0;
    std::uint64_t mPermanentMissingGlyphs = 0;
    std::uint64_t mRetryableFailures = 0;
    std::uint64_t mRetriesQueued = 0;
    std::uint64_t mRequestQueueFull = 0;
    std::uint64_t mCandidateFaceProbes = 0;
    std::uint64_t mAvoidedFanoutJobs = 0;
    std::uint64_t mRasterVariantLimitFallbacks = 0;
    std::size_t mRasterVariantCount = 0;
    std::uint64_t mCommittedGlyphs = 0;
    std::uint64_t mCommitFailures = 0;
    std::uint64_t mRequestEpoch = 0;
    std::size_t mInjectedRasterizationFailures = 0;
    std::size_t mInjectedCommitFailures = 0;
    bool mValid = false;
    bool mReleased = false;
};

Win32AsyncFontSet::Win32AsyncFontSet(
    TextureStore& textures,
    FontStore& fonts,
    Win32AsyncFontConfiguration configuration)
    : mImpl(std::make_unique<Impl>(textures, fonts, configuration)) {}

Win32AsyncFontSet::~Win32AsyncFontSet() noexcept = default;

bool Win32AsyncFontSet::valid() const noexcept { return mImpl->valid(); }
std::string_view Win32AsyncFontSet::lastError() const noexcept { return mImpl->lastError(); }
FontHandle Win32AsyncFontSet::font(Win32FontRole role) const noexcept {
    return mImpl->font(role);
}
std::span<const FontHandle> Win32AsyncFontSet::fontChain(
    Win32FontLocale locale) const noexcept {
    return mImpl->fontChain(locale);
}
std::size_t Win32AsyncFontSet::request(std::span<const char32_t> codepoints) {
    return mImpl->request(codepoints);
}
std::size_t Win32AsyncFontSet::request(
    std::span<const FontHandle> fontChain,
    std::span<const char32_t> codepoints) {
    return mImpl->request(fontChain, codepoints);
}
std::size_t Win32AsyncFontSet::requestUtf8(std::string_view text) {
    return mImpl->requestUtf8(text);
}
std::size_t Win32AsyncFontSet::requestUtf8(
    std::span<const FontHandle> fontChain,
    std::string_view text) {
    return mImpl->requestUtf8(fontChain, text);
}
void Win32AsyncFontSet::requestText(std::string_view text) {
    static_cast<void>(requestUtf8(text));
}
void Win32AsyncFontSet::requestText(
    std::span<const FontHandle> fontChain,
    float logicalPixelSize,
    std::string_view text) {
    static_cast<void>(logicalPixelSize);
    static_cast<void>(requestUtf8(fontChain, text));
}
TextPreparationStatus Win32AsyncFontSet::prepareText(
    std::span<const FontHandle> fontChain,
    float logicalPixelSize,
    std::string_view text) {
    return mImpl->prepareText(fontChain, logicalPixelSize, text);
}
FontHandle Win32AsyncFontSet::resolveFont(
    FontHandle font,
    float logicalPixelSize) {
    return mImpl->resolveFont(font, logicalPixelSize);
}
bool Win32AsyncFontSet::prewarmTextSizes(std::span<const float> logicalPixelSizes) {
    return mImpl->prewarmTextSizes(logicalPixelSizes);
}
bool Win32AsyncFontSet::setDpiScale(float dpiScale) noexcept {
    return mImpl->setDpiScale(dpiScale);
}
std::size_t Win32AsyncFontSet::commitReady(std::size_t maximumResults) {
    return mImpl->commitReady(maximumResults);
}
bool Win32AsyncFontSet::releaseResources() noexcept { return mImpl->releaseResources(); }
bool Win32AsyncFontSet::idle() const noexcept { return mImpl->idle(); }
Win32AsyncFontStatistics Win32AsyncFontSet::statistics() const noexcept {
    return mImpl->statistics();
}

} // namespace henia::ui
