#pragma once

#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/DynamicGlyphAtlas.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace henia::ui {

enum class Win32FontRole : std::uint8_t {
    Primary,
    SimplifiedChinese,
    TraditionalChinese,
    Japanese,
    Korean,
    Symbols,
    Emoji,
};

enum class Win32FontLocale : std::uint8_t {
    Default,
    SimplifiedChinese,
    TraditionalChinese,
    Japanese,
    Korean,
};

struct Win32MultilingualFontFamilies final {
    std::wstring_view primary = L"Segoe UI";
    std::wstring_view simplifiedChinese = L"Microsoft YaHei UI";
    std::wstring_view traditionalChinese = L"Microsoft JhengHei UI";
    std::wstring_view japanese = L"Yu Gothic UI";
    std::wstring_view korean = L"Malgun Gothic";
    std::wstring_view symbols = L"Segoe UI Symbol";
    std::wstring_view emoji = L"Segoe UI Emoji";
};

struct Win32AsyncFontConfiguration final {
    Win32MultilingualFontFamilies families{};
    // An existing primary face may be extended instead of creating another
    // synchronous ASCII seed atlas. Its metrics must match this request.
    FontHandle primaryFont{};
    // Optional external physical-size cache for the primary face. Returned
    // fonts remain externally owned but become publishable async targets here.
    // Fallback faces create lightweight size-specific dynamic variants here.
    TextFontRasterResolver* primaryRasterResolver = nullptr;
    float logicalPixelHeight = 18.0F;
    float dpiScale = 1.0F;
    std::uint32_t initialAtlasWidth = 512;
    std::uint32_t initialAtlasHeight = 256;
    DynamicGlyphAtlasOptions dynamicAtlas{
        .pageWidth = 512,
        .pageHeight = 512,
        .padding = 1,
        .maximumPages = 8,
    };
    // Preallocation happens during construction, before the interactive loop.
    // Increase this up to maximumPages when runtime page allocation is forbidden.
    std::size_t preallocatedPagesPerFace = 1;
    std::size_t requestQueueCapacity = 4096;
    std::size_t resultQueueCapacity = 512;
    // Fixed-point physical-size buckets are shared by every role. Once the cap
    // is reached the closest retained bucket is reused, bounding long-lived
    // atlas and FontStore growth under arbitrary text-size input.
    std::size_t maximumRasterSizeBuckets = 16;
    std::uint32_t physicalSizeStepsPerPixel = 8;
    // Zero disables cumulative per-glyph pixel alignment.
    float pixelAlignedMaximumPhysicalHeight = 20.0F;
    // Deterministic fault injection for transactional/retry tests. Production
    // configurations leave both counters at zero.
    std::size_t injectedRasterizationFailures = 0;
    std::size_t injectedCommitFailures = 0;
};

struct Win32AsyncFontStatistics final {
    std::size_t faces = 0;
    std::size_t rasterSizeBuckets = 0;
    std::size_t rasterVariants = 0;
    std::size_t pendingBakeJobs = 0;
    std::size_t readyResults = 0;
    std::uint64_t uniqueCodepointsRequested = 0;
    std::uint64_t bakeJobsQueued = 0;
    std::uint64_t deduplicatedBakeJobs = 0;
    std::uint64_t deduplicatedInFlight = 0;
    std::uint64_t residentGlyphsSkipped = 0;
    std::uint64_t unsupportedCodepoints = 0;
    std::uint64_t permanentMissingGlyphs = 0;
    std::uint64_t retryableFailures = 0;
    std::uint64_t retriesQueued = 0;
    std::uint64_t requestQueueFull = 0;
    std::uint64_t candidateFaceProbes = 0;
    std::uint64_t avoidedFanoutJobs = 0;
    std::uint64_t rasterVariantLimitFallbacks = 0;
    std::uint64_t rasterizedGlyphs = 0;
    std::uint64_t missingGlyphs = 0;
    std::uint64_t rasterizationFailures = 0;
    std::uint64_t committedGlyphs = 0;
    std::uint64_t commitFailures = 0;
    std::uint64_t wrongThreadCalls = 0;
};

// DirectWrite rasterization runs on one private worker. Requests and completed
// glyphs cross bounded single-producer/single-consumer queues without locking.
// FontStore, TextureStore, and DynamicGlyphAtlas remain owner-thread objects:
// commitReady() is the only operation that publishes worker results to them.
class Win32AsyncFontSet final : public TextGlyphRequestBackend, public TextFontRasterResolver {
public:
    Win32AsyncFontSet(
        TextureStore& textures,
        FontStore& fonts,
        Win32AsyncFontConfiguration configuration = {});
    ~Win32AsyncFontSet() noexcept;

    Win32AsyncFontSet(const Win32AsyncFontSet&) = delete;
    Win32AsyncFontSet& operator=(const Win32AsyncFontSet&) = delete;
    Win32AsyncFontSet(Win32AsyncFontSet&&) = delete;
    Win32AsyncFontSet& operator=(Win32AsyncFontSet&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string_view lastError() const noexcept;
    [[nodiscard]] FontHandle font(Win32FontRole role) const noexcept;
    // Returned chains stay valid until releaseResources(). The default chain
    // prefers Simplified Chinese Han forms; locale chains reorder CJK faces.
    [[nodiscard]] std::span<const FontHandle> fontChain(
        Win32FontLocale locale = Win32FontLocale::Default) const noexcept;

    // Owner-thread, nonblocking requests. The return value is the number of
    // bake jobs accepted. Each missing scalar targets only the first eligible
    // chain face and advances after a confirmed miss.
    [[nodiscard]] std::size_t request(std::span<const char32_t> codepoints);
    [[nodiscard]] std::size_t request(
        std::span<const FontHandle> fontChain,
        std::span<const char32_t> codepoints);
    [[nodiscard]] std::size_t requestUtf8(std::string_view text);
    [[nodiscard]] std::size_t requestUtf8(
        std::span<const FontHandle> fontChain,
        std::string_view text);
    void requestText(std::string_view text) override;
    void requestText(
        std::span<const FontHandle> fontChain,
        float logicalPixelSize,
        std::string_view text) override;
    [[nodiscard]] FontHandle resolveFont(
        FontHandle font,
        float logicalPixelSize) override;
    [[nodiscard]] bool prewarmTextSizes(std::span<const float> logicalPixelSizes);
    [[nodiscard]] bool setDpiScale(float dpiScale) noexcept;
    // Owner-thread, bounded publication. Missing/failed results consume budget
    // but only successfully published glyphs contribute to the return value.
    [[nodiscard]] std::size_t commitReady(std::size_t maximumResults = 64);

    // Stops the worker, removes glyphs/pages published by this helper, and
    // destroys internally created fonts plus their seed atlases. Published
    // handles owned by this helper do not outlive this call. A borrowed primary
    // font and its seed atlas are never destroyed. The owner-thread call is
    // non-throwing and reports allocation/cleanup failure as false. Destruction
    // attempts the same cleanup, suppresses failure, and always stops the worker.
    [[nodiscard]] bool releaseResources() noexcept;

    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] Win32AsyncFontStatistics statistics() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace henia::ui
