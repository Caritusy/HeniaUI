#include "henia/ui/platform/win32/Win32AsyncFont.h"

#include "henia/CheckedArithmetic.h"
#include "henia/ui/platform/win32/Win32FontLoader.h"
#include "henia/ui/text/Utf8.h"

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
    char32_t codepoint = U'\0';
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
        };
    }
};

struct BakeResult final {
    std::uint8_t face = 0;
    BakeStatus status = BakeStatus::Failed;
    OwnedGlyph glyph;
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
    std::uint32_t pixelHeight,
    float metricsScale) {
    BakeResult result{.face = job.face};
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

    const float logicalScale = static_cast<float>(pixelHeight)
        / static_cast<float>(fontMetrics.designUnitsPerEm) / metricsScale;
    const float advance = static_cast<float>(glyphMetrics.advanceWidth) * logicalScale;
    if (!std::isfinite(advance) || advance < 0.0F) return result;

    const DWRITE_GLYPH_RUN run{
        .fontFace = &face,
        .fontEmSize = static_cast<float>(pixelHeight),
        .glyphCount = 1,
        .glyphIndices = &glyphIndex,
        .glyphAdvances = nullptr,
        .glyphOffsets = nullptr,
        .isSideways = FALSE,
        .bidiLevel = 0,
    };
    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    if (FAILED(factory.CreateGlyphRunAnalysis(
            &run,
            1.0F,
            nullptr,
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
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
    result.glyph.codepoint = job.codepoint;
    result.glyph.glyphId = glyphIndex;
    result.glyph.advance = advance;
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
        const auto red = static_cast<unsigned char>(clearType[index * 3U]);
        const auto green = static_cast<unsigned char>(clearType[index * 3U + 1U]);
        const auto blue = static_cast<unsigned char>(clearType[index * 3U + 2U]);
        result.glyph.pixels[index] = static_cast<std::byte>(std::max({red, green, blue}));
    }

    result.glyph.width = static_cast<std::uint32_t>(width);
    result.glyph.height = static_cast<std::uint32_t>(height);
    result.glyph.rowPitch = result.glyph.width;
    result.glyph.bearing = {
        static_cast<float>(bounds.left) / metricsScale,
        -static_cast<float>(bounds.top) / metricsScale,
    };
    result.glyph.logicalSize = {
        static_cast<float>(width) / metricsScale,
        static_cast<float>(height) / metricsScale,
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
          mRequests(normalizedCapacity(configuration.requestQueueCapacity)),
          mResults(normalizedCapacity(configuration.resultQueueCapacity)),
          mResultSpace(static_cast<std::ptrdiff_t>(
              normalizedCapacity(configuration.resultQueueCapacity))) {
        if (!initialize()) return;
        for (auto& batch : mCommitBatches) {
            batch.reserve(normalizedCapacity(configuration.resultQueueCapacity));
        }
        for (auto& views : mRasterizedViews) {
            views.reserve(normalizedCapacity(configuration.resultQueueCapacity));
        }
        mWorker = std::jthread([this](std::stop_token stop) noexcept { workerMain(stop); });
    }

    ~Impl() {
        if (mWorker.joinable()) {
            mWorker.request_stop();
            mWorkAvailable.release();
            mResultSpace.release();
            mWorker.join();
        }
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
        std::size_t queued = 0;
        for (char32_t codepoint : codepoints) queued += requestOne(codepoint);
        return queued;
    }

    [[nodiscard]] std::size_t requestUtf8(std::string_view text) {
        if (!ownerThread()) return 0;
        std::size_t queued = 0;
        for (std::size_t offset = 0; offset < text.size();) {
            const Utf8Codepoint decoded = decodeUtf8(text, offset);
            if (decoded.bytes == 0) break;
            offset += decoded.bytes;
            queued += requestOne(decoded.valid ? decoded.value : U'\uFFFD');
        }
        return queued;
    }

    [[nodiscard]] std::size_t commitReady(std::size_t maximumResults) {
        if (!ownerThread() || maximumResults == 0) return 0;
        for (auto& batch : mCommitBatches) batch.clear();

        std::size_t consumed = 0;
        BakeResult result{};
        while (consumed < maximumResults && mResults.pop(result)) {
            mResultSpace.release();
            ++consumed;
            if (result.status == BakeStatus::Ready
                && result.face < mCommitBatches.size()) {
                mCommitBatches[result.face].push_back(std::move(result.glyph));
            }
        }

        std::size_t committed = 0;
        for (std::size_t index = 0; index < mFaces.size(); ++index) {
            auto& batch = mCommitBatches[index];
            if (batch.empty()) continue;
            auto& views = mRasterizedViews[index];
            views.clear();
            for (const OwnedGlyph& glyph : batch) views.push_back(glyph.view());
            Face& face = mFaces[index];
            if (face.atlas != nullptr && face.atlas->add(views)) {
                committed += batch.size();
                mCommittedGlyphs += batch.size();
            } else {
                mCommitFailures += batch.size();
            }
        }
        return committed;
    }

    [[nodiscard]] bool idle() const noexcept {
        return mPendingBakeJobs.load(std::memory_order_acquire) == 0
            && mResults.size() == 0;
    }

    [[nodiscard]] Win32AsyncFontStatistics statistics() const noexcept {
        return {
            .faces = mFaceCount,
            .pendingBakeJobs = mPendingBakeJobs.load(std::memory_order_acquire),
            .readyResults = mResults.size(),
            .uniqueCodepointsRequested = mUniqueCodepointsRequested,
            .bakeJobsQueued = mBakeJobsQueued,
            .deduplicatedBakeJobs = mDeduplicatedBakeJobs,
            .residentGlyphsSkipped = mResidentGlyphsSkipped,
            .unsupportedCodepoints = mUnsupportedCodepoints,
            .requestQueueFull = mRequestQueueFull,
            .rasterizedGlyphs = mRasterizedGlyphs.load(std::memory_order_relaxed),
            .missingGlyphs = mMissingGlyphs.load(std::memory_order_relaxed),
            .rasterizationFailures = mRasterizationFailures.load(std::memory_order_relaxed),
            .committedGlyphs = mCommittedGlyphs,
            .commitFailures = mCommitFailures,
            .wrongThreadCalls = mWrongThreadCalls.load(std::memory_order_relaxed),
        };
    }

private:
    struct Face final {
        std::wstring family;
        FontHandle font{};
        std::unique_ptr<DynamicGlyphAtlas> atlas;
    };

    [[nodiscard]] static std::size_t normalizedCapacity(std::size_t requested) noexcept {
        constexpr std::size_t maximum = 65'536;
        return std::clamp(requested, std::size_t{1}, maximum);
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
        mPixelHeight = static_cast<std::uint32_t>(std::round(physicalHeight));
        mMetricsScale = static_cast<float>(mPixelHeight)
            / mConfiguration.logicalPixelHeight;

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
            if (index == roleIndex(Win32FontRole::Primary)
                && mConfiguration.primaryFont.valid()) {
                face.font = mConfiguration.primaryFont;
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
                        .pixelHeight = mPixelHeight,
                        .atlasWidth = mConfiguration.initialAtlasWidth,
                        .atlasHeight = mConfiguration.initialAtlasHeight,
                        .ranges = ranges,
                        .metricsScale = mMetricsScale,
                    });
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
                face.atlas.reset();
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

    [[nodiscard]] std::size_t requestOne(char32_t codepoint) {
        // Layout text commonly contains line separators; they are not glyph
        // requests and should not inflate unsupported-codepoint telemetry.
        if (codepoint < U' ') return 0;
        if (!validScalar(codepoint)) {
            ++mUnsupportedCodepoints;
            return 0;
        }
        if (mUniqueCodepoints.insert(codepoint).second) ++mUniqueCodepointsRequested;
        const std::uint32_t routes = routeMask(codepoint);
        std::size_t queued = 0;
        for (std::size_t index = 0; index < mFaces.size(); ++index) {
            Face& face = mFaces[index];
            if ((routes & (1U << static_cast<unsigned int>(index))) == 0
                || !face.font.valid() || face.atlas == nullptr) {
                continue;
            }
            const std::uint64_t key = (static_cast<std::uint64_t>(index) << 32U)
                | static_cast<std::uint32_t>(codepoint);
            if (mKnownJobs.contains(key)) {
                ++mDeduplicatedBakeJobs;
                continue;
            }
            const FontFace* fontFace = mFonts->find(face.font);
            if (fontFace != nullptr && fontFace->glyph(codepoint) != nullptr) {
                mKnownJobs.insert(key);
                ++mResidentGlyphsSkipped;
                continue;
            }
            BakeJob job{
                .face = static_cast<std::uint8_t>(index),
                .codepoint = codepoint,
            };
            if (!mRequests.push(std::move(job))) {
                ++mRequestQueueFull;
                continue;
            }
            mKnownJobs.insert(key);
            mPendingBakeJobs.fetch_add(1, std::memory_order_release);
            mWorkAvailable.release();
            ++mBakeJobsQueued;
            ++queued;
        }
        return queued;
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

            BakeResult result{.face = job.face};
            try {
                if (factory != nullptr && job.face < workerFaces.size()
                    && workerFaces[job.face] != nullptr) {
                    result = rasterize(
                        *factory.Get(),
                        *workerFaces[job.face].Get(),
                        job,
                        mPixelHeight,
                        mMetricsScale);
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
    std::array<Face, kRoleCount> mFaces;
    std::array<std::vector<FontHandle>, kLocaleCount> mChains;
    SpscQueue<BakeJob> mRequests;
    SpscQueue<BakeResult> mResults;
    std::counting_semaphore<> mWorkAvailable{0};
    std::counting_semaphore<> mResultSpace;
    std::jthread mWorker;
    std::array<std::vector<OwnedGlyph>, kRoleCount> mCommitBatches;
    std::array<std::vector<RasterizedGlyph>, kRoleCount> mRasterizedViews;
    std::unordered_set<char32_t> mUniqueCodepoints;
    std::unordered_set<std::uint64_t> mKnownJobs;
    std::string mLastError;
    std::size_t mFaceCount = 0;
    std::uint32_t mPixelHeight = 0;
    float mMetricsScale = 1.0F;
    std::atomic_size_t mPendingBakeJobs{0};
    std::atomic_uint64_t mRasterizedGlyphs{0};
    std::atomic_uint64_t mMissingGlyphs{0};
    std::atomic_uint64_t mRasterizationFailures{0};
    mutable std::atomic_uint64_t mWrongThreadCalls{0};
    std::uint64_t mUniqueCodepointsRequested = 0;
    std::uint64_t mBakeJobsQueued = 0;
    std::uint64_t mDeduplicatedBakeJobs = 0;
    std::uint64_t mResidentGlyphsSkipped = 0;
    std::uint64_t mUnsupportedCodepoints = 0;
    std::uint64_t mRequestQueueFull = 0;
    std::uint64_t mCommittedGlyphs = 0;
    std::uint64_t mCommitFailures = 0;
    bool mValid = false;
};

Win32AsyncFontSet::Win32AsyncFontSet(
    TextureStore& textures,
    FontStore& fonts,
    Win32AsyncFontConfiguration configuration)
    : mImpl(std::make_unique<Impl>(textures, fonts, configuration)) {}

Win32AsyncFontSet::~Win32AsyncFontSet() = default;

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
std::size_t Win32AsyncFontSet::requestUtf8(std::string_view text) {
    return mImpl->requestUtf8(text);
}
void Win32AsyncFontSet::requestText(std::string_view text) {
    static_cast<void>(requestUtf8(text));
}
std::size_t Win32AsyncFontSet::commitReady(std::size_t maximumResults) {
    return mImpl->commitReady(maximumResults);
}
bool Win32AsyncFontSet::idle() const noexcept { return mImpl->idle(); }
Win32AsyncFontStatistics Win32AsyncFontSet::statistics() const noexcept {
    return mImpl->statistics();
}

} // namespace henia::ui
