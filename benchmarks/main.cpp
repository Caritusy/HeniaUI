#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/Validation.h"
#include "henia/gfx/VisibilityList.h"
#include "henia/ui/Frame.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/widget/UiDocument.h"
#include "henia/ui/widget/controls/Label.h"
#include "henia/ui/widget/controls/ListView.h"
#include "henia/ui/widget/controls/Panel.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace allocation_tracking {

struct Header final {
    void* base = nullptr;
    std::size_t size = 0;
    std::uint64_t generation = 0;
};

struct State final {
    std::uint64_t nextGeneration = 1;
    std::uint64_t activeGeneration = 0;
    std::uint64_t allocations = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t liveBytes = 0;
    std::uint64_t peakLiveBytes = 0;
};

thread_local State gState;

[[nodiscard]] void* allocate(std::size_t size, std::size_t alignment) noexcept {
    size = std::max<std::size_t>(size, 1);
    alignment = std::max(alignment, alignof(Header));
    if (!std::has_single_bit(alignment)
        || size > std::numeric_limits<std::size_t>::max() - sizeof(Header) - alignment) {
        return nullptr;
    }
    const std::size_t allocationSize = size + sizeof(Header) + alignment - 1U;
    void* base = std::malloc(allocationSize);
    if (base == nullptr) {
        return nullptr;
    }
    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base) + sizeof(Header);
    const std::uintptr_t aligned = (start + alignment - 1U) & ~(alignment - 1U);
    auto* header = reinterpret_cast<Header*>(aligned - sizeof(Header));
    header->base = base;
    header->size = size;
    header->generation = gState.activeGeneration;
    if (gState.activeGeneration != 0) {
        ++gState.allocations;
        gState.allocatedBytes += size;
        gState.liveBytes += size;
        gState.peakLiveBytes = std::max(gState.peakLiveBytes, gState.liveBytes);
    }
    return reinterpret_cast<void*>(aligned);
}

void release(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const Header header = *reinterpret_cast<const Header*>(address - sizeof(Header));
    if (gState.activeGeneration != 0 && header.generation == gState.activeGeneration) {
        gState.liveBytes = header.size > gState.liveBytes ? 0 : gState.liveBytes - header.size;
    }
    std::free(header.base);
}

struct Sample final {
    std::uint64_t allocations = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t peakLiveBytes = 0;
};

class Scope final {
public:
    Scope() noexcept {
        gState.activeGeneration = gState.nextGeneration++;
        gState.allocations = 0;
        gState.allocatedBytes = 0;
        gState.liveBytes = 0;
        gState.peakLiveBytes = 0;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    [[nodiscard]] Sample finish() noexcept {
        const Sample result{gState.allocations, gState.allocatedBytes, gState.peakLiveBytes};
        gState.activeGeneration = 0;
        return result;
    }
};

} // namespace allocation_tracking

void* operator new(std::size_t size) {
    if (void* pointer = allocation_tracking::allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void* operator new(std::size_t size, std::align_val_t alignment) {
    if (void* pointer = allocation_tracking::allocate(size, static_cast<std::size_t>(alignment))) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocation_tracking::allocate(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__);
}

void* operator new[](std::size_t size, const std::nothrow_t& value) noexcept {
    return ::operator new(size, value);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocation_tracking::allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](
    std::size_t size,
    std::align_val_t alignment,
    const std::nothrow_t& value) noexcept {
    return ::operator new(size, alignment, value);
}

void operator delete(void* pointer) noexcept { allocation_tracking::release(pointer); }
void operator delete[](void* pointer) noexcept { allocation_tracking::release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { allocation_tracking::release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { allocation_tracking::release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { allocation_tracking::release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { allocation_tracking::release(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    allocation_tracking::release(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
    allocation_tracking::release(pointer);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    allocation_tracking::release(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    allocation_tracking::release(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_tracking::release(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_tracking::release(pointer);
}

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kPrimitiveCount = 4096;
constexpr std::size_t kGlyphCount = 4096;
constexpr std::size_t kBoxCount = 32768;
constexpr std::size_t kPagedBoxCount = 100000;
constexpr std::size_t kClusteredBoxEdits = 32;
constexpr std::size_t kSparseBoxEdits = 4;
constexpr std::size_t kRoundedSegments = 32;
constexpr std::uint64_t kTextAtlasBytes = 512U * 512U;

struct Options final {
    std::size_t iterations = 20;
    std::size_t warmup = 5;
    std::string jsonPath;
    bool verify = false;
};

struct IterationPhases final {
    std::uint64_t layoutNanoseconds = 0;
    std::uint64_t paintBuildNanoseconds = 0;
    std::uint64_t packetCompileNanoseconds = 0;
    std::uint64_t checksum = 0;
};

struct ScenarioResult final {
    std::string name;
    std::size_t iterations = 0;
    std::uint64_t totalMedianNanoseconds = 0;
    std::uint64_t totalP95Nanoseconds = 0;
    std::uint64_t layoutMedianNanoseconds = 0;
    std::uint64_t paintBuildMedianNanoseconds = 0;
    std::uint64_t packetCompileMedianNanoseconds = 0;
    std::uint64_t allocationsMedian = 0;
    std::uint64_t allocatedBytesMedian = 0;
    std::uint64_t peakTransientBytes = 0;
    std::uint64_t cacheHits = 0;
    std::uint64_t cacheMisses = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t submittedInstances = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t coldUploadBytes = 0;
    std::uint64_t estimatedFragmentArea = 0;
    std::uint64_t copiedBoxInstances = 0;
    std::uint64_t cpuResidentBytes = 0;
    std::uint64_t gpuBufferBytes = 0;
    std::uint64_t textureBytes = 0;
    std::uint64_t logicalItems = 0;
    std::uint64_t residentItemWidgets = 0;
    std::uint64_t checksum = 0;
    henia::ui::PacketStatistics packetStatistics{};
    bool packetStatisticsAvailable = false;
    bool gpuTimestampAvailable = false;
    std::uint64_t gpuNanoseconds = 0;
};

void capturePacketStatistics(
    ScenarioResult& result,
    const henia::ui::RenderPacket& packet) noexcept {
    result.packetStatistics = packet.statistics();
    result.packetStatisticsAvailable = true;
}

struct TessVertex final {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    std::uint32_t color = 0;
};

struct WidgetScene final {
    std::unique_ptr<henia::ui::Panel> root;
    henia::ui::Label* dynamicLabel = nullptr;
};

struct VirtualListLabels final {
    [[nodiscard]] std::string_view label(std::size_t index) {
        return labels[index % labels.size()];
    }

    std::array<std::string_view, 8> labels{
        "player-alpha", "player-bravo", "player-charlie", "player-delta",
        "player-echo", "player-foxtrot", "player-golf", "player-hotel",
    };
};

class BenchmarkRecycledRow final : public henia::ui::Widget {
public:
    explicit BenchmarkRecycledRow(henia::ui::FontHandle font) noexcept : mFont(font) {}

    void bind(std::string_view label, bool selected) noexcept {
        if (mLabel == label && mSelected == selected) return;
        mLabel = label;
        mSelected = selected;
        markPaintDirty();
    }

protected:
    [[nodiscard]] henia::ui::Vec2 onMeasure(
        henia::ui::TextPainter&,
        henia::ui::Constraints constraints) override {
        return constraints.maximum;
    }

    void onPaint(
        henia::ui::Canvas& canvas,
        henia::ui::TextPainter& text,
        const henia::ui::Theme&) override {
        const henia::ui::Rect row = frame();
        canvas.fillRect(
            {{row.min.x + 4.0F, row.min.y + 2.0F},
             {row.max.x - 4.0F, row.max.y - 2.0F}},
            mSelected
                ? henia::ui::Color{0.12F, 0.42F, 0.60F, 1.0F}
                : henia::ui::Color{0.05F, 0.08F, 0.12F, 1.0F},
            2.0F);
        text.draw(canvas, mFont, 14.0F,
            {row.min.x + 10.0F, row.min.y + 4.0F},
            {0.86F, 0.92F, 0.97F, 1.0F}, mLabel);
    }

private:
    henia::ui::FontHandle mFont{};
    std::string_view mLabel;
    bool mSelected = false;
};

struct RecycledListBenchmarkModel final {
    explicit RecycledListBenchmarkModel(henia::ui::FontHandle fontValue) noexcept
        : font(fontValue) {}

    [[nodiscard]] henia::ui::ListItemKey key(std::size_t index) const noexcept {
        return 1'000'000U + static_cast<henia::ui::ListItemKey>(index);
    }
    [[nodiscard]] float extent(std::size_t index) const noexcept {
        return 18.0F + static_cast<float>(index % 3U) * 4.0F;
    }
    [[nodiscard]] std::unique_ptr<henia::ui::Widget> create() {
        ++creations;
        return std::make_unique<BenchmarkRecycledRow>(font);
    }
    void bind(
        henia::ui::Widget& widget,
        std::size_t index,
        henia::ui::ListItemKey,
        bool selected) {
        static_cast<BenchmarkRecycledRow&>(widget).bind(
            labels.label(index), selected);
        ++binds;
    }

    henia::ui::FontHandle font{};
    VirtualListLabels labels;
    std::uint64_t creations = 0;
    std::uint64_t binds = 0;
};

[[nodiscard]] std::uint64_t nanosecondsSince(Clock::time_point start) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start).count());
}

[[nodiscard]] std::uint64_t percentile(std::vector<std::uint64_t> samples, double value) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const double position = value * static_cast<double>(samples.size() - 1U);
    return samples[static_cast<std::size_t>(std::ceil(position))];
}

template <typename Operation>
[[nodiscard]] ScenarioResult measureScenario(
    std::string name,
    const Options& options,
    Operation&& operation) {
    for (std::size_t iteration = 0; iteration < options.warmup; ++iteration) {
        static_cast<void>(operation(iteration));
    }

    std::vector<std::uint64_t> totals;
    std::vector<std::uint64_t> layouts;
    std::vector<std::uint64_t> paints;
    std::vector<std::uint64_t> packets;
    std::vector<std::uint64_t> allocations;
    std::vector<std::uint64_t> allocatedBytes;
    totals.reserve(options.iterations);
    layouts.reserve(options.iterations);
    paints.reserve(options.iterations);
    packets.reserve(options.iterations);
    allocations.reserve(options.iterations);
    allocatedBytes.reserve(options.iterations);

    ScenarioResult result;
    result.name = std::move(name);
    result.iterations = options.iterations;
    for (std::size_t iteration = 0; iteration < options.iterations; ++iteration) {
        allocation_tracking::Scope allocationScope;
        const auto started = Clock::now();
        const IterationPhases phases = operation(options.warmup + iteration);
        const std::uint64_t elapsed = nanosecondsSince(started);
        const allocation_tracking::Sample allocation = allocationScope.finish();
        totals.push_back(elapsed);
        layouts.push_back(phases.layoutNanoseconds);
        paints.push_back(phases.paintBuildNanoseconds);
        packets.push_back(phases.packetCompileNanoseconds);
        allocations.push_back(allocation.allocations);
        allocatedBytes.push_back(allocation.allocatedBytes);
        result.peakTransientBytes = std::max(result.peakTransientBytes, allocation.peakLiveBytes);
        result.checksum ^= phases.checksum + iteration * 0x9E3779B97F4A7C15ULL;
    }
    result.totalMedianNanoseconds = percentile(totals, 0.5);
    result.totalP95Nanoseconds = percentile(totals, 0.95);
    result.layoutMedianNanoseconds = percentile(layouts, 0.5);
    result.paintBuildMedianNanoseconds = percentile(paints, 0.5);
    result.packetCompileMedianNanoseconds = percentile(packets, 0.5);
    result.allocationsMedian = percentile(allocations, 0.5);
    result.allocatedBytesMedian = percentile(allocatedBytes, 0.5);
    return result;
}

[[nodiscard]] henia::ui::FontHandle createBenchmarkFont(henia::ui::FontStore& fonts) {
    using namespace henia::ui;
    FontDefinition definition;
    definition.atlas = TextureHandle{1};
    definition.pixelSize = 16.0F;
    definition.ascent = 12.0F;
    definition.descent = 4.0F;
    definition.lineGap = 2.0F;
    definition.glyphs.reserve(95);
    for (char32_t codepoint = U' '; codepoint <= U'~'; ++codepoint) {
        const std::uint32_t index = static_cast<std::uint32_t>(codepoint - U' ');
        const float u = static_cast<float>(index % 16U) / 16.0F;
        const float v = static_cast<float>(index / 16U) / 6.0F;
        definition.glyphs.push_back({
            .codepoint = codepoint,
            .uv = {{u, v}, {u + 1.0F / 16.0F, v + 1.0F / 6.0F}},
            .size = {8.0F, 16.0F},
            .bearing = {0.0F, 12.0F},
            .advance = 8.0F,
        });
    }
    return fonts.add(std::move(definition));
}

[[nodiscard]] WidgetScene createWidgetScene(henia::ui::FontHandle font) {
    using namespace henia::ui;
    auto root = std::make_unique<Panel>(PanelStyle{
        .background = {0.02F, 0.03F, 0.05F, 1.0F},
        .padding = {8.0F, 8.0F, 8.0F, 8.0F},
        .gap = 2.0F,
        .direction = LayoutDirection::Column,
    });
    Label* dynamicLabel = nullptr;
    for (std::size_t row = 0; row < 16; ++row) {
        auto rowPanel = std::make_unique<Panel>(PanelStyle{
            .gap = 4.0F,
            .direction = LayoutDirection::Row,
        });
        for (std::size_t column = 0; column < 8; ++column) {
            const std::string text = "metric-" + std::to_string((row * 8U + column) % 32U);
            auto label = std::make_unique<Label>(text, LabelStyle{.font = font, .size = 14.0F});
            if (row == 8 && column == 4) {
                dynamicLabel = label.get();
            }
            rowPanel->addChild(std::move(label));
        }
        root->addChild(std::move(rowPanel));
    }
    return {std::move(root), dynamicLabel};
}

[[nodiscard]] std::vector<henia::gfx::BoxInstance> createBoxes(
    std::size_t count = kBoxCount) {
    std::vector<henia::gfx::BoxInstance> boxes;
    boxes.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float x = static_cast<float>(index % 128U) * 1.25F;
        const float y = static_cast<float>((index / 128U) % 64U) * 1.25F;
        const float z = static_cast<float>(index / (128U * 64U)) * 1.25F;
        boxes.push_back({
            .minimum = {x, y, z},
            .lineWidth = 1.5F,
            .maximum = {x + 1.0F, y + 1.0F, z + 1.0F},
            .hueOffset = static_cast<float>(index % 360U),
            .color = {0.20F, 0.65F, 0.95F, 1.0F},
            .effects = henia::gfx::BoxEffect::None,
        });
    }
    return boxes;
}

[[nodiscard]] ScenarioResult benchmarkTessellation(const Options& options) {
    std::vector<TessVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(kPrimitiveCount * (kRoundedSegments + 1U));
    indices.reserve(kPrimitiveCount * kRoundedSegments * 3U);
    ScenarioResult result = measureScenario(
        "imdrawlist_cpu_tessellation",
        options,
        [&](std::size_t iteration) {
            const auto started = Clock::now();
            vertices.clear();
            indices.clear();
            for (std::size_t primitive = 0; primitive < kPrimitiveCount; ++primitive) {
                const float centerX = static_cast<float>(primitive % 128U) * 12.0F;
                const float centerY = static_cast<float>(primitive / 128U) * 12.0F;
                const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
                vertices.push_back({centerX, centerY, 0.5F, 0.5F, 0xFFFFFFFFU});
                for (std::size_t segment = 0; segment < kRoundedSegments; ++segment) {
                    constexpr float tau = 6.28318530717958647692F;
                    const float angle = tau * static_cast<float>(segment)
                        / static_cast<float>(kRoundedSegments);
                    const float cosine = std::cos(angle);
                    const float sine = std::sin(angle);
                    vertices.push_back({
                        centerX + cosine * 5.0F,
                        centerY + sine * 5.0F,
                        cosine * 0.5F + 0.5F,
                        sine * 0.5F + 0.5F,
                        0xFFFFFFFFU,
                    });
                }
                for (std::size_t segment = 0; segment < kRoundedSegments; ++segment) {
                    indices.push_back(base);
                    indices.push_back(base + 1U + static_cast<std::uint32_t>(segment));
                    indices.push_back(base + 1U
                        + static_cast<std::uint32_t>((segment + 1U) % kRoundedSegments));
                }
            }
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = vertices.size() ^ (indices.size() << 1U) ^ iteration,
            };
        });
    const std::uint64_t vertexBytes = vertices.size() * sizeof(TessVertex);
    const std::uint64_t indexBytes = indices.size() * sizeof(std::uint32_t);
    result.drawCalls = 1;
    result.uploadBytes = vertexBytes + indexBytes;
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = vertices.capacity() * sizeof(TessVertex)
        + indices.capacity() * sizeof(std::uint32_t);
    result.gpuBufferBytes = result.uploadBytes;
    return result;
}

[[nodiscard]] ScenarioResult benchmarkShaderEllipses(const Options& options) {
    using namespace henia::ui;
    Frame frame;
    frame.reserve(kPrimitiveCount, 4, CapacityPolicy::Fixed);
    RenderPacket packet;
    ScenarioResult result = measureScenario(
        "shader_analytic_ellipses",
        options,
        [&](std::size_t iteration) {
            const auto paintStarted = Clock::now();
            Canvas& canvas = frame.begin();
            for (std::size_t primitive = 0; primitive < kPrimitiveCount; ++primitive) {
                const float centerX = static_cast<float>(primitive % 128U) * 12.0F;
                const float centerY = static_cast<float>(primitive / 128U) * 12.0F;
                canvas.ellipse(
                    {{centerX - 5.0F, centerY - 5.0F},
                     {centerX + 5.0F, centerY + 5.0F}},
                    {0.20F, 0.65F, 0.95F, 1.0F});
            }
            const std::uint64_t paint = nanosecondsSince(paintStarted);
            const auto packetStarted = Clock::now();
            packet = frame.finish();
            const std::uint64_t compile = nanosecondsSince(packetStarted);
            return IterationPhases{
                .paintBuildNanoseconds = paint,
                .packetCompileNanoseconds = compile,
                .checksum = packet.identity() ^ packet.revision() ^ iteration,
            };
        });
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = frame.displayList().capacity() * sizeof(DrawCommand)
        + packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkManyPrimitives(const Options& options) {
    using namespace henia::ui;
    Frame frame;
    frame.reserve(kPrimitiveCount, 4, CapacityPolicy::Fixed);
    RenderPacket packet;
    ScenarioResult result = measureScenario(
        "henia_many_primitives",
        options,
        [&](std::size_t iteration) {
            const auto paintStarted = Clock::now();
            Canvas& canvas = frame.begin();
            for (std::size_t primitive = 0; primitive < kPrimitiveCount; ++primitive) {
                const float x = static_cast<float>(primitive % 128U) * 12.0F;
                const float y = static_cast<float>(primitive / 128U) * 12.0F;
                canvas.fillRect(
                    {{x, y}, {x + 10.0F, y + 10.0F}},
                    {0.20F, 0.65F, 0.95F, 1.0F},
                    5.0F);
            }
            const std::uint64_t paint = nanosecondsSince(paintStarted);
            const auto packetStarted = Clock::now();
            packet = frame.finish();
            const std::uint64_t compile = nanosecondsSince(packetStarted);
            return IterationPhases{
                .paintBuildNanoseconds = paint,
                .packetCompileNanoseconds = compile,
                .checksum = packet.identity() ^ packet.revision() ^ iteration,
            };
        });
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = frame.displayList().capacity() * sizeof(DrawCommand)
        + packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkAnalyticFragmentBounds(const Options& options) {
    using namespace henia::ui;
    Frame frame;
    frame.reserve(2, 9, 4, CapacityPolicy::Fixed);
    frame.setFragmentAreaTracking(true);
    RenderPacket packet;
    ScenarioResult result = measureScenario(
        "analytic_2d_fragment_bounds",
        options,
        [&](std::size_t iteration) {
            const auto paintStarted = Clock::now();
            Canvas& canvas = frame.begin();
            canvas.line(
                {0.0F, 0.0F},
                {1920.0F, 1080.0F},
                {0.2F, 0.65F, 0.95F, 1.0F},
                1.0F,
                LineCap::Butt);
            canvas.strokeRect(
                {{100.0F, 100.0F}, {1820.0F, 980.0F}},
                {0.95F, 0.65F, 0.2F, 1.0F},
                16.0F,
                2.0F);
            const std::uint64_t paint = nanosecondsSince(paintStarted);
            const auto packetStarted = Clock::now();
            packet = frame.finish();
            const std::uint64_t compile = nanosecondsSince(packetStarted);
            return IterationPhases{
                .paintBuildNanoseconds = paint,
                .packetCompileNanoseconds = compile,
                .checksum = packet.statistics().estimatedFragmentArea ^ iteration,
            };
        });
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.estimatedFragmentArea = packet.statistics().estimatedFragmentArea;
    result.cpuResidentBytes = frame.displayList().capacity() * sizeof(DrawCommand)
        + packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkShaderEffectLayers(const Options& options) {
    using namespace henia::ui;
    constexpr std::size_t rectangleCount = 1024;
    constexpr std::size_t effectsPerRectangle = 4;
    constexpr std::size_t instanceCount = rectangleCount * effectsPerRectangle;
    Frame frame;
    frame.reserve(instanceCount, instanceCount, 4, CapacityPolicy::Fixed);
    frame.setFragmentAreaTracking(true);
    RenderPacket packet;
    ScenarioResult result = measureScenario(
        "shader_effect_layers_4096",
        options,
        [&](std::size_t iteration) {
            std::array layers{
                EffectLayer{
                    .kind = EffectLayerKind::SoftShadow,
                    .color = {0.0F, 0.0F, 0.0F, 0.35F},
                    .vector = {1.0F, 1.0F},
                    .amount = 2.0F,
                },
                EffectLayer{
                    .kind = EffectLayerKind::Glow,
                    .color = {0.1F, 0.5F, 1.0F, 0.4F},
                    .amount = 2.0F,
                },
                EffectLayer{
                    .kind = EffectLayerKind::AnimatedGradient,
                    .color = {0.15F, 0.45F, 0.95F, 1.0F},
                    .secondaryColor = {0.85F, 0.20F, 0.70F, 1.0F},
                    .vector = {1.0F, 0.5F},
                    .phase = static_cast<float>(iteration % 256U) / 256.0F,
                },
                EffectLayer{
                    .kind = EffectLayerKind::Outline,
                    .color = {0.85F, 0.95F, 1.0F, 0.9F},
                    .amount = 1.0F,
                },
                EffectLayer{
                    .kind = EffectLayerKind::Tint,
                    .color = {1.0F, 0.0F, 0.0F, 1.0F},
                    .enabled = false,
                },
            };
            const auto paintStarted = Clock::now();
            Canvas& canvas = frame.begin();
            for (std::size_t rectangle = 0; rectangle < rectangleCount; ++rectangle) {
                const float x = static_cast<float>(rectangle % 64U) * 14.0F;
                const float y = static_cast<float>(rectangle / 64U) * 14.0F;
                canvas.effectRect({{x, y}, {x + 10.0F, y + 10.0F}}, 3.0F, layers);
            }
            const std::uint64_t paint = nanosecondsSince(paintStarted);
            const auto packetStarted = Clock::now();
            packet = frame.finish();
            const std::uint64_t compile = nanosecondsSince(packetStarted);
            return IterationPhases{
                .paintBuildNanoseconds = paint,
                .packetCompileNanoseconds = compile,
                .checksum = packet.statistics().effectEstimatedFragmentArea ^ iteration,
            };
        });
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.estimatedFragmentArea = packet.statistics().estimatedFragmentArea;
    result.cpuResidentBytes = frame.displayList().capacity() * sizeof(DrawCommand)
        + packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkStaticRetained(const Options& options) {
    using namespace henia::ui;
    FontStore fonts;
    const FontHandle font = createBenchmarkFont(fonts);
    TextRunCache cache(fonts);
    cache.reserve(64, 16);
    TextPainter painter(cache);
    UiDocument document(painter);
    WidgetScene scene = createWidgetScene(font);
    document.reserve(4096, 32, CapacityPolicy::Fixed);
    document.setViewport({1280.0F, 720.0F});
    document.setRoot(std::move(scene.root));
    RenderPacket packet = document.compose();
    const std::uint64_t cachedBefore = document.statistics().cachedFrames;
    ScenarioResult result = measureScenario(
        "retained_static_ui",
        options,
        [&](std::size_t iteration) {
            packet = document.compose();
            return IterationPhases{
                .checksum = packet.identity() ^ packet.revision() ^ iteration,
            };
        });
    result.cacheHits = document.statistics().cachedFrames - cachedBefore;
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = 0;
    result.coldUploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.cpuResidentBytes = packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    result.textureBytes = kTextAtlasBytes;
    return result;
}

[[nodiscard]] ScenarioResult benchmarkDynamicFullRepaint(const Options& options) {
    using namespace henia::ui;
    FontStore fonts;
    const FontHandle font = createBenchmarkFont(fonts);
    TextRunCache cache(fonts);
    cache.reserve(64, 16);
    TextPainter painter(cache);
    WidgetScene scene = createWidgetScene(font);
    Frame frame;
    frame.reserve(4096, 32, CapacityPolicy::Fixed);
    RenderPacket packet;
    const Theme theme{};
    const Constraints constraints{{0.0F, 0.0F}, {1280.0F, 720.0F}};
    static_cast<void>(scene.root->measure(painter, constraints));
    scene.root->arrange(painter, {{0.0F, 0.0F}, {1280.0F, 720.0F}});
    ScenarioResult result = measureScenario(
        "full_repaint_dynamic_ui",
        options,
        [&](std::size_t iteration) {
            LabelStyle style = scene.dynamicLabel->style();
            style.color = iteration % 2U == 0
                ? Color{0.2F, 0.7F, 1.0F, 1.0F}
                : Color{1.0F, 0.6F, 0.2F, 1.0F};
            scene.dynamicLabel->setStyle(style);
            const auto paintStarted = Clock::now();
            Canvas& canvas = frame.begin();
            scene.root->paint(canvas, painter, theme);
            const std::uint64_t paint = nanosecondsSince(paintStarted);
            const auto packetStarted = Clock::now();
            packet = frame.finish();
            const std::uint64_t compile = nanosecondsSince(packetStarted);
            return IterationPhases{
                .paintBuildNanoseconds = paint,
                .packetCompileNanoseconds = compile,
                .checksum = packet.identity() ^ packet.revision() ^ iteration,
            };
        });
    result.cacheHits = cache.hits();
    result.cacheMisses = cache.misses();
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = frame.displayList().capacity() * sizeof(DrawCommand)
        + packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    result.textureBytes = kTextAtlasBytes;
    return result;
}

[[nodiscard]] ScenarioResult benchmarkDynamicDirty(const Options& options) {
    using namespace henia::ui;
    FontStore fonts;
    const FontHandle font = createBenchmarkFont(fonts);
    TextRunCache cache(fonts);
    cache.reserve(64, 16);
    TextPainter painter(cache);
    UiDocument document(painter);
    WidgetScene scene = createWidgetScene(font);
    Label* dynamicLabel = scene.dynamicLabel;
    document.reserve(4096, 32, CapacityPolicy::Fixed);
    document.setViewport({1280.0F, 720.0F});
    document.setRoot(std::move(scene.root));
    RenderPacket packet = document.compose();
    const UiDocumentStatistics statisticsBefore = document.statistics();
    ScenarioResult result = measureScenario(
        "retained_dynamic_dirty_ui",
        options,
        [&](std::size_t iteration) {
            LabelStyle style = dynamicLabel->style();
            style.color = iteration % 2U == 0
                ? Color{0.2F, 0.7F, 1.0F, 1.0F}
                : Color{1.0F, 0.6F, 0.2F, 1.0F};
            dynamicLabel->setStyle(style);
            const auto compositionStarted = Clock::now();
            packet = document.compose();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(compositionStarted),
                .checksum = packet.identity() ^ packet.revision() ^ iteration,
            };
        });
    const UiDocumentStatistics statisticsAfter = document.statistics();
    result.cacheHits = statisticsAfter.reusedSegments - statisticsBefore.reusedSegments;
    result.cacheMisses = statisticsAfter.rebuiltSegments - statisticsBefore.rebuiltSegments;
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    result.textureBytes = kTextAtlasBytes;
    return result;
}

[[nodiscard]] ScenarioResult benchmarkTextHeavy(const Options& options) {
    using namespace henia::ui;
    FontStore fonts;
    const FontHandle font = createBenchmarkFont(fonts);
    TextRunCache cache(fonts);
    cache.reserve(32, 32);
    cache.setMaximumEntries(32);
    TextPainter painter(cache);
    std::array<std::string, 32> lines;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        lines[index] = "telemetry row " + std::to_string(index) + " value 1234";
        static_cast<void>(cache.layout(font, 14.0F, lines[index]));
    }
    Frame frame;
    frame.reserve(kGlyphCount + 256U, 64, CapacityPolicy::Fixed);
    RenderPacket packet;
    const std::uint64_t hitsBefore = cache.hits();
    const std::uint64_t missesBefore = cache.misses();
    ScenarioResult result = measureScenario(
        "text_heavy_ui",
        options,
        [&](std::size_t iteration) {
            const auto paintStarted = Clock::now();
            Canvas& canvas = frame.begin();
            for (std::size_t row = 0; row < 160; ++row) {
                painter.draw(
                    canvas,
                    font,
                    14.0F,
                    {8.0F, 8.0F + static_cast<float>(row) * 18.0F},
                    {0.90F, 0.95F, 1.0F, 1.0F},
                    lines[row % lines.size()]);
            }
            const std::uint64_t paint = nanosecondsSince(paintStarted);
            const auto packetStarted = Clock::now();
            packet = frame.finish();
            const std::uint64_t compile = nanosecondsSince(packetStarted);
            return IterationPhases{
                .paintBuildNanoseconds = paint,
                .packetCompileNanoseconds = compile,
                .checksum = packet.identity() ^ packet.revision() ^ iteration,
            };
        });
    result.cacheHits = cache.hits() - hitsBefore;
    result.cacheMisses = cache.misses() - missesBefore;
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = frame.displayList().capacity() * sizeof(DrawCommand)
        + packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    result.textureBytes = kTextAtlasBytes;
    return result;
}

[[nodiscard]] ScenarioResult benchmarkVirtualList(const Options& options) {
    using namespace henia::ui;
    FontStore fonts;
    const FontHandle font = createBenchmarkFont(fonts);
    TextRunCache cache(fonts);
    cache.reserve(16, 16);
    cache.setMaximumEntries(16);
    TextPainter painter(cache);
    VirtualListLabels labels;
    auto list = std::make_unique<ListView>(std::vector<std::string>{}, ListViewStyle{
        .font = font,
        .width = 320.0F,
        .height = 200.0F,
        .rowHeight = 20.0F,
    });
    ListView* listPointer = list.get();
    list->setVirtualItems(10'000,
        ValueCallback<std::string_view, std::size_t>::bind<
            VirtualListLabels, &VirtualListLabels::label>(labels));
    UiDocument document(painter);
    document.reserve(256, 256, 16, CapacityPolicy::Fixed);
    document.setViewport({320.0F, 200.0F});
    document.setRoot(std::move(list));
    static_cast<void>(document.compose());
    for (std::size_t row = 0; row < labels.labels.size(); ++row) {
        listPointer->setScrollOffset(static_cast<float>(row) * 20.0F);
        static_cast<void>(document.compose());
    }

    RenderPacket packet;
    const std::uint64_t hitsBefore = cache.hits();
    const std::uint64_t missesBefore = cache.misses();
    ScenarioResult result = measureScenario(
        "virtualized_list_10000",
        options,
        [&](std::size_t iteration) {
            listPointer->setScrollOffset(
                static_cast<float>((iteration * 97U) % 9'990U) * 20.0F);
            const auto paintStarted = Clock::now();
            packet = document.compose();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(paintStarted),
                .checksum = packet.identity() ^ packet.revision()
                    ^ static_cast<std::uint64_t>(listPointer->lastPaintedRowCount()),
            };
        });
    result.cacheHits = cache.hits() - hitsBefore;
    result.cacheMisses = cache.misses() - missesBefore;
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    result.textureBytes = kTextAtlasBytes;
    result.logicalItems = 10'000;
    return result;
}

[[nodiscard]] ScenarioResult benchmarkRecycledWidgetList(const Options& options) {
    using namespace henia::ui;
    constexpr std::size_t logicalItemCount = 50'000;
    FontStore fonts;
    const FontHandle font = createBenchmarkFont(fonts);
    TextRunCache cache(fonts);
    cache.reserve(16, 16);
    cache.setMaximumEntries(16);
    TextPainter painter(cache);
    RecycledListBenchmarkModel model(font);
    auto list = std::make_unique<ListView>(std::vector<std::string>{}, ListViewStyle{
        .font = font,
        .width = 320.0F,
        .height = 240.0F,
        .rowHeight = 22.0F,
        .overscanRows = 2,
    });
    ListView* listPointer = list.get();
    list->setRecycledItems({
        .itemCount = logicalItemCount,
        .itemKey = ValueCallback<ListItemKey, std::size_t>::bind<
            RecycledListBenchmarkModel, &RecycledListBenchmarkModel::key>(model),
        .itemExtent = ValueCallback<float, std::size_t>::bind<
            RecycledListBenchmarkModel, &RecycledListBenchmarkModel::extent>(model),
        .createWidget = ValueCallback<std::unique_ptr<Widget>>::bind<
            RecycledListBenchmarkModel, &RecycledListBenchmarkModel::create>(model),
        .bindWidget = Callback<Widget&, std::size_t, ListItemKey, bool>::bind<
            RecycledListBenchmarkModel, &RecycledListBenchmarkModel::bind>(model),
    });
    UiDocument document(painter);
    document.reserve(384, 384, 24, CapacityPolicy::Fixed);
    document.setViewport({320.0F, 240.0F});
    document.setRoot(std::move(list));
    static_cast<void>(document.compose());
    for (std::size_t sample = 0; sample < 128; ++sample) {
        listPointer->setScrollOffset(static_cast<float>(sample * 173U));
        static_cast<void>(document.compose());
    }
    const std::uint64_t warmCreations = listPointer->widgetCreationCount();
    if (warmCreations == 0 || warmCreations > 18U) {
        throw std::runtime_error("Recycled list did not establish a viewport-bounded widget pool");
    }

    RenderPacket packet;
    const std::uint64_t hitsBefore = cache.hits();
    const std::uint64_t missesBefore = cache.misses();
    ScenarioResult result = measureScenario(
        "recycled_widget_list_50000",
        options,
        [&](std::size_t iteration) {
            listPointer->setScrollOffset(
                static_cast<float>((iteration * 104'729U) % 1'080'000U));
            const auto paintStarted = Clock::now();
            packet = document.compose();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(paintStarted),
                .checksum = packet.identity() ^ packet.revision()
                    ^ listPointer->widgetBindCount()
                    ^ static_cast<std::uint64_t>(listPointer->realizedItems().size()),
            };
        });
    if (listPointer->widgetCreationCount() != warmCreations) {
        throw std::runtime_error("Recycled list allocated a larger widget pool while scrolling");
    }
    result.cacheHits = cache.hits() - hitsBefore;
    result.cacheMisses = cache.misses() - missesBefore;
    result.drawCalls = packet.batches().size();
    result.submittedInstances = packet.instances().size();
    capturePacketStatistics(result, packet);
    result.uploadBytes = packet.instances().size() * sizeof(DrawInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = packet.instanceCapacity() * sizeof(DrawInstance)
        + packet.batchCapacity() * sizeof(DrawBatch)
        + (logicalItemCount + 1U) * sizeof(double)
        + listPointer->pooledWidgetCount() * sizeof(BenchmarkRecycledRow);
    result.gpuBufferBytes = packet.instanceCapacity() * sizeof(DrawInstance);
    result.textureBytes = kTextAtlasBytes;
    result.logicalItems = logicalItemCount;
    result.residentItemWidgets = listPointer->pooledWidgetCount();
    return result;
}

[[nodiscard]] ScenarioResult benchmarkLarge3DFull(const Options& options) {
    using namespace henia::gfx;
    const std::vector<BoxInstance> boxes = createBoxes();
    InstanceBatch batch;
    ScenarioResult result = measureScenario(
        "large_3d_full_build",
        options,
        [&](std::size_t iteration) {
            const auto started = Clock::now();
            ShapeBatch3D builder;
            builder.reserve(boxes.size());
            static_cast<void>(builder.replaceBoxes(boxes));
            batch = builder.snapshot();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = batch.identity() ^ batch.revision() ^ batch.boxes().size() ^ iteration,
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = batch.boxes().size();
    result.uploadBytes = batch.boxes().size() * sizeof(BoxInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.copiedBoxInstances = batch.copiedBoxCount();
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance)
        + batch.storageBytes();
    result.gpuBufferBytes = batch.boxes().size() * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkLarge3DDirty(const Options& options) {
    using namespace henia::gfx;
    const std::vector<BoxInstance> boxes = createBoxes();
    ShapeBatch3D builder;
    builder.reserve(boxes.size());
    static_cast<void>(builder.replaceBoxes(boxes));
    InstanceBatch retained = builder.snapshot();
    ScenarioResult result = measureScenario(
        "large_3d_dirty_update",
        options,
        [&](std::size_t iteration) {
            const std::size_t index = iteration % boxes.size();
            BoxInstance changed = retained.boxes()[index];
            changed.hueOffset += 1.0F;
            const auto started = Clock::now();
            static_cast<void>(builder.updateBox(index, changed));
            retained = builder.snapshot();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = retained.identity() ^ retained.revision()
                    ^ retained.dirtyOffset() ^ retained.dirtyCount(),
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = retained.boxes().size();
    result.uploadBytes = sizeof(BoxInstance);
    result.coldUploadBytes = retained.boxes().size() * sizeof(BoxInstance);
    result.copiedBoxInstances = retained.copiedBoxCount();
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance)
        + retained.storageBytes();
    result.gpuBufferBytes = retained.boxes().size() * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkVisibility(
    const Options& options,
    std::size_t boxCount,
    bool cpuCulling) {
    using namespace henia::gfx;
    std::vector<BoxInstance> boxes(boxCount);
    const std::size_t visibleCount = boxCount / 4U;
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        const float localX = static_cast<float>(index % 16U) * 0.08F;
        const float localY = static_cast<float>((index / 16U) % 16U) * 0.08F;
        const bool visible = index < visibleCount;
        const float x = visible ? -0.64F + localX : 3.0F + localX;
        const float y = -0.64F + localY;
        boxes[index] = {
            .minimum = {x, y, 0.25F},
            .lineWidth = 2.0F,
            .maximum = {x + 0.04F, y + 0.04F, 0.35F},
            .hueOffset = static_cast<float>(index % 1024U) / 1024.0F,
            .color = {0.2F, 0.7F, 0.9F, 0.8F},
            .effects = BoxEffect::HueCycle,
        };
    }
    ShapeBatch3D builder;
    builder.reserve(boxes.size());
    static_cast<void>(builder.replaceBoxes(boxes));
    const InstanceBatch batch = builder.snapshot();
    VisibilityList visibility;
    if (!visibility.reserve(boxCount)) {
        throw std::runtime_error("Unable to reserve the visibility benchmark workspace");
    }
    const std::string name = std::string(cpuCulling ? "visibility_cpu_" : "visibility_direct_")
        + std::to_string(boxCount) + "_75pct_offscreen";
    ScenarioResult result = measureScenario(
        name,
        options,
        [&](std::size_t iteration) {
            ViewParameters view{
                .viewport = {1920.0F, 1080.0F},
            };
            view.viewProjection.values[12] = iteration % 2U == 0 ? 0.0F : 0.0001F;
            std::uint64_t checksum = iteration;
            if (cpuCulling) {
                if (!visibility.update(
                        batch,
                        view,
                        {.mode = VisibilityMode::CpuFrustum})) {
                    throw std::runtime_error("Visibility benchmark update failed");
                }
                checksum ^= visibility.revision() ^ visibility.boxes().size();
            } else {
                for (std::size_t pageIndex = 0; pageIndex < batch.boxPageCount(); ++pageIndex) {
                    for (const BoxInstance& box : batch.boxPage(pageIndex)) {
                        checksum ^= static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(
                            box.minimum.x));
                        checksum ^= validate(box).size();
                    }
                }
            }
            return IterationPhases{
                .paintBuildNanoseconds = cpuCulling
                    ? visibility.statistics().cullingNanoseconds : 0,
                .checksum = checksum,
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = cpuCulling ? visibility.boxes().size() : boxCount;
    result.uploadBytes = result.submittedInstances * sizeof(BoxInstance);
    result.coldUploadBytes = result.uploadBytes;
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance) + batch.storageBytes()
        + (cpuCulling ? visibility.storageBytes() : 0U);
    result.gpuBufferBytes = boxCount * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkPaged3DStable(const Options& options) {
    using namespace henia::gfx;
    const std::vector<BoxInstance> boxes = createBoxes(kPagedBoxCount);
    ShapeBatch3D builder;
    builder.reserve(boxes.size());
    static_cast<void>(builder.replaceBoxes(boxes));
    InstanceBatch retained = builder.snapshot();
    ScenarioResult result = measureScenario(
        "paged_3d_stable_snapshot_100k",
        options,
        [&](std::size_t iteration) {
            const auto started = Clock::now();
            retained = builder.snapshot();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = retained.identity() ^ retained.revision()
                    ^ retained.boxes().size() ^ iteration,
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = retained.boxes().size();
    result.coldUploadBytes = retained.boxes().size() * sizeof(BoxInstance);
    result.copiedBoxInstances = retained.copiedBoxCount();
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance) + retained.storageBytes();
    result.gpuBufferBytes = retained.boxes().size() * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkPaged3DOneEdit(const Options& options) {
    using namespace henia::gfx;
    const std::vector<BoxInstance> boxes = createBoxes(kPagedBoxCount);
    ShapeBatch3D builder;
    builder.reserve(boxes.size());
    static_cast<void>(builder.replaceBoxes(boxes));
    InstanceBatch retained = builder.snapshot();
    ScenarioResult result = measureScenario(
        "paged_3d_one_edit_100k",
        options,
        [&](std::size_t iteration) {
            const std::size_t index = iteration % boxes.size();
            BoxInstance changed = retained.boxes()[index];
            changed.hueOffset += 1.0F;
            const auto started = Clock::now();
            static_cast<void>(builder.updateBox(index, changed));
            retained = builder.snapshot();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = retained.revision() ^ retained.copiedBoxCount()
                    ^ retained.dirtyRanges().size(),
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = retained.boxes().size();
    result.uploadBytes = sizeof(BoxInstance);
    result.coldUploadBytes = retained.boxes().size() * sizeof(BoxInstance);
    result.copiedBoxInstances = retained.copiedBoxCount();
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance) + retained.storageBytes();
    result.gpuBufferBytes = retained.boxes().size() * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkPaged3DClusteredEdits(const Options& options) {
    using namespace henia::gfx;
    const std::vector<BoxInstance> boxes = createBoxes(kPagedBoxCount);
    ShapeBatch3D builder;
    builder.reserve(boxes.size());
    static_cast<void>(builder.replaceBoxes(boxes));
    InstanceBatch retained = builder.snapshot();
    const std::size_t pageCount = retained.boxPageCount();
    ScenarioResult result = measureScenario(
        "paged_3d_clustered_edits_100k",
        options,
        [&](std::size_t iteration) {
            const std::size_t page = iteration % (pageCount - 1U);
            const std::size_t start = page * InstanceBatch::kBoxesPerPage + 64U;
            const auto started = Clock::now();
            for (std::size_t offset = 0; offset < kClusteredBoxEdits; ++offset) {
                BoxInstance changed = retained.boxes()[start + offset];
                changed.hueOffset += 1.0F;
                static_cast<void>(builder.updateBox(start + offset, changed));
            }
            retained = builder.snapshot();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = retained.revision() ^ retained.copiedBoxCount()
                    ^ retained.dirtyCount(),
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = retained.boxes().size();
    result.uploadBytes = kClusteredBoxEdits * sizeof(BoxInstance);
    result.coldUploadBytes = retained.boxes().size() * sizeof(BoxInstance);
    result.copiedBoxInstances = retained.copiedBoxCount();
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance) + retained.storageBytes();
    result.gpuBufferBytes = retained.boxes().size() * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] ScenarioResult benchmarkPaged3DSparseEdits(const Options& options) {
    using namespace henia::gfx;
    const std::vector<BoxInstance> boxes = createBoxes(kPagedBoxCount);
    ShapeBatch3D builder;
    builder.reserve(boxes.size());
    static_cast<void>(builder.replaceBoxes(boxes));
    InstanceBatch retained = builder.snapshot();
    constexpr std::array<std::size_t, kSparseBoxEdits> indices{1, 25000, 75000, 99998};
    ScenarioResult result = measureScenario(
        "paged_3d_sparse_edits_100k",
        options,
        [&](std::size_t iteration) {
            const auto started = Clock::now();
            for (const std::size_t index : indices) {
                BoxInstance changed = retained.boxes()[index];
                changed.hueOffset += 1.0F;
                static_cast<void>(builder.updateBox(index, changed));
            }
            retained = builder.snapshot();
            return IterationPhases{
                .paintBuildNanoseconds = nanosecondsSince(started),
                .checksum = retained.revision() ^ retained.copiedBoxCount()
                    ^ retained.dirtyRanges().size() ^ iteration,
            };
        });
    result.drawCalls = 1;
    result.submittedInstances = retained.boxes().size();
    result.uploadBytes = kSparseBoxEdits * sizeof(BoxInstance);
    result.coldUploadBytes = retained.boxes().size() * sizeof(BoxInstance);
    result.copiedBoxInstances = retained.copiedBoxCount();
    result.cpuResidentBytes = boxes.capacity() * sizeof(BoxInstance) + retained.storageBytes();
    result.gpuBufferBytes = retained.boxes().size() * sizeof(BoxInstance);
    return result;
}

[[nodiscard]] const ScenarioResult* findScenario(
    std::span<const ScenarioResult> results,
    std::string_view name) noexcept {
    const auto found = std::find_if(results.begin(), results.end(), [name](const auto& result) {
        return result.name == name;
    });
    return found == results.end() ? nullptr : &*found;
}

[[nodiscard]] bool verify(std::span<const ScenarioResult> results) {
    const ScenarioResult* tessellation = findScenario(results, "imdrawlist_cpu_tessellation");
    const ScenarioResult* shaderEllipses = findScenario(results, "shader_analytic_ellipses");
    const ScenarioResult* primitives = findScenario(results, "henia_many_primitives");
    const ScenarioResult* analytic = findScenario(results, "analytic_2d_fragment_bounds");
    const ScenarioResult* effects = findScenario(results, "shader_effect_layers_4096");
    const ScenarioResult* retained = findScenario(results, "retained_static_ui");
    const ScenarioResult* fullDynamic = findScenario(results, "full_repaint_dynamic_ui");
    const ScenarioResult* dynamic = findScenario(results, "retained_dynamic_dirty_ui");
    const ScenarioResult* text = findScenario(results, "text_heavy_ui");
    const ScenarioResult* virtualList = findScenario(results, "virtualized_list_10000");
    const ScenarioResult* recycledList = findScenario(results, "recycled_widget_list_50000");
    const ScenarioResult* full3d = findScenario(results, "large_3d_full_build");
    const ScenarioResult* dirty3d = findScenario(results, "large_3d_dirty_update");
    const ScenarioResult* visibilityDirectSmall = findScenario(
        results, "visibility_direct_2048_75pct_offscreen");
    const ScenarioResult* visibilityCpuSmall = findScenario(
        results, "visibility_cpu_2048_75pct_offscreen");
    const ScenarioResult* visibilityDirectLarge = findScenario(
        results, "visibility_direct_32768_75pct_offscreen");
    const ScenarioResult* visibilityCpuLarge = findScenario(
        results, "visibility_cpu_32768_75pct_offscreen");
    const ScenarioResult* pagedStable = findScenario(results, "paged_3d_stable_snapshot_100k");
    const ScenarioResult* pagedOne = findScenario(results, "paged_3d_one_edit_100k");
    const ScenarioResult* pagedClustered = findScenario(results, "paged_3d_clustered_edits_100k");
    const ScenarioResult* pagedSparse = findScenario(results, "paged_3d_sparse_edits_100k");
    bool valid = tessellation != nullptr && shaderEllipses != nullptr
        && primitives != nullptr && analytic != nullptr && effects != nullptr
        && retained != nullptr && fullDynamic != nullptr && dynamic != nullptr && text != nullptr
        && virtualList != nullptr && recycledList != nullptr
        && full3d != nullptr && dirty3d != nullptr
        && visibilityDirectSmall != nullptr && visibilityCpuSmall != nullptr
        && visibilityDirectLarge != nullptr && visibilityCpuLarge != nullptr
        && pagedStable != nullptr
        && pagedOne != nullptr && pagedClustered != nullptr && pagedSparse != nullptr;
    if (!valid) {
        std::cerr << "Benchmark verification: a required scenario is missing\n";
        return false;
    }
    valid = shaderEllipses->submittedInstances == kPrimitiveCount
        && shaderEllipses->drawCalls == 1
        && shaderEllipses->packetStatisticsAvailable
        && shaderEllipses->packetStatistics.sourceCommands == kPrimitiveCount
        && shaderEllipses->packetStatistics.instances == kPrimitiveCount
        && shaderEllipses->packetStatistics.batches == 1
        && shaderEllipses->uploadBytes == kPrimitiveCount * sizeof(henia::ui::DrawInstance)
        && tessellation->uploadBytes > shaderEllipses->uploadBytes * 4U
        && primitives->submittedInstances == kPrimitiveCount
        && primitives->drawCalls == 1
        && primitives->packetStatisticsAvailable
        && primitives->packetStatistics.sourceCommands == kPrimitiveCount
        && primitives->packetStatistics.instances == kPrimitiveCount
        && primitives->packetStatistics.batches == 1
        && primitives->packetStatistics.batchedInstancesBeyondFirst == kPrimitiveCount - 1U
        && primitives->packetStatistics.maxInstancesPerBatch == kPrimitiveCount
        && primitives->packetStatistics.fullInstanceUploadBytes == primitives->coldUploadBytes
        && !tessellation->packetStatisticsAvailable
        && tessellation->uploadBytes > primitives->uploadBytes * 4U
        && analytic->submittedInstances == 9
        && analytic->packetStatistics.sourceCommands == 2
        && analytic->packetStatistics.batchedInstancesBeyondFirst == 8
        && analytic->estimatedFragmentArea > 0
        && analytic->estimatedFragmentArea < (1925U * 1085U + 1724U * 884U) / 8U
        && effects->allocationsMedian == 0
        && effects->submittedInstances == kPrimitiveCount
        && effects->drawCalls == 1
        && effects->packetStatistics.effectInstances == kPrimitiveCount
        && effects->packetStatistics.shaderVariantTransitions == kPrimitiveCount - 1U
        && effects->packetStatistics.effectEstimatedFragmentArea > 0
        && effects->packetStatistics.effectEstimatedFragmentArea
            == effects->packetStatistics.estimatedFragmentArea
        && effects->uploadBytes == kPrimitiveCount * sizeof(henia::ui::DrawInstance)
        && retained->allocationsMedian == 0
        && retained->uploadBytes == 0
        && retained->cacheHits >= retained->iterations
        && dynamic->paintBuildMedianNanoseconds > 0
        && dynamic->cacheHits >= dynamic->iterations
        && dynamic->cacheMisses >= dynamic->iterations
        && dynamic->totalMedianNanoseconds < fullDynamic->totalMedianNanoseconds
        && text->cacheHits > 0
        && text->cacheMisses == 0
        && text->submittedInstances >= kGlyphCount * 3U / 4U
        && virtualList->allocationsMedian == 0
        && virtualList->cacheHits > 0
        && virtualList->cacheMisses == 0
        && virtualList->packetStatisticsAvailable
        && virtualList->packetStatistics.sourceCommands <= 192
        && virtualList->submittedInstances <= 192
        && recycledList->allocationsMedian == 0
        && recycledList->allocatedBytesMedian == 0
        && recycledList->cacheHits > 0
        && recycledList->cacheMisses == 0
        && recycledList->packetStatisticsAvailable
        && recycledList->packetStatistics.sourceCommands <= 256
        && recycledList->submittedInstances <= 256
        && full3d->submittedInstances == kBoxCount
        && full3d->uploadBytes == kBoxCount * sizeof(henia::gfx::BoxInstance)
        && dirty3d->uploadBytes == sizeof(henia::gfx::BoxInstance)
        && dirty3d->copiedBoxInstances <= henia::gfx::InstanceBatch::kBoxesPerPage
        && dirty3d->gpuBufferBytes == full3d->gpuBufferBytes
        && visibilityDirectSmall->submittedInstances == 2048
        && visibilityCpuSmall->submittedInstances == 512
        && visibilityDirectLarge->submittedInstances == kBoxCount
        && visibilityCpuLarge->submittedInstances == kBoxCount / 4U
        && visibilityCpuSmall->allocationsMedian == 0
        && visibilityCpuLarge->allocationsMedian == 0
        && visibilityCpuSmall->uploadBytes == visibilityDirectSmall->uploadBytes / 4U
        && visibilityCpuLarge->uploadBytes == visibilityDirectLarge->uploadBytes / 4U
        && pagedStable->submittedInstances == kPagedBoxCount
        && pagedStable->allocationsMedian == 0
        && pagedStable->uploadBytes == 0
        && pagedStable->copiedBoxInstances == 0
        && pagedOne->uploadBytes == sizeof(henia::gfx::BoxInstance)
        && pagedOne->copiedBoxInstances <= henia::gfx::InstanceBatch::kBoxesPerPage
        && pagedOne->allocatedBytesMedian
            < kPagedBoxCount * sizeof(henia::gfx::BoxInstance) / 16U
        && pagedClustered->uploadBytes == kClusteredBoxEdits * sizeof(henia::gfx::BoxInstance)
        && pagedClustered->copiedBoxInstances <= henia::gfx::InstanceBatch::kBoxesPerPage
        && pagedSparse->uploadBytes == kSparseBoxEdits * sizeof(henia::gfx::BoxInstance)
        && pagedSparse->copiedBoxInstances
            <= kSparseBoxEdits * henia::gfx::InstanceBatch::kBoxesPerPage;
    if (!valid) {
        std::cerr << "Benchmark verification: structural performance invariant failed\n";
    }
    return valid;
}

void writeJson(
    const std::string& path,
    const Options& options,
    std::span<const ScenarioResult> results) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Unable to open benchmark JSON output");
    }
#if defined(NDEBUG)
    constexpr std::string_view configuration = "Release";
#else
    constexpr std::string_view configuration = "Debug";
#endif
#if defined(_MSC_VER)
    constexpr std::string_view compiler = "msvc";
#elif defined(__clang__)
    constexpr std::string_view compiler = "clang";
#elif defined(__GNUC__)
    constexpr std::string_view compiler = "gcc";
#else
    constexpr std::string_view compiler = "unknown";
#endif
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"configuration\": \"" << configuration << "\",\n"
           << "  \"compiler\": \"" << compiler << "\",\n"
           << "  \"pointer_bits\": " << sizeof(void*) * 8U << ",\n"
           << "  \"draw_layout\": {\n"
           << "    \"command_bytes\": " << sizeof(henia::ui::DrawCommand) << ",\n"
           << "    \"instance_bytes\": " << sizeof(henia::ui::DrawInstance) << ",\n"
           << "    \"glyph_command_bytes\": " << sizeof(henia::ui::DrawCommand) << ",\n"
           << "    \"glyph_upload_bytes\": " << sizeof(henia::ui::DrawInstance) << ",\n"
           << "    \"vertex_attributes\": 5\n"
           << "  },\n"
           << "  \"iterations\": " << options.iterations << ",\n"
           << "  \"warmup\": " << options.warmup << ",\n"
           << "  \"scenarios\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        const ScenarioResult& result = results[index];
        const std::uint64_t cacheRequests = result.cacheHits + result.cacheMisses;
        const double cacheRate = cacheRequests == 0
            ? 0.0
            : static_cast<double>(result.cacheHits) / static_cast<double>(cacheRequests);
        const henia::ui::PacketStatistics& batching = result.packetStatistics;
        const double averageInstancesPerBatch = batching.batches == 0
            ? 0.0
            : static_cast<double>(batching.instances) / static_cast<double>(batching.batches);
        const std::uint64_t textureSlotCapacity = batching.texturedBatches
            * henia::ui::DrawBatch::kTextureCapacity;
        const double textureTableUtilization = textureSlotCapacity == 0
            ? 0.0
            : static_cast<double>(batching.textureSlotsUsed)
                / static_cast<double>(textureSlotCapacity);
        output << "    {\n"
               << "      \"name\": \"" << result.name << "\",\n"
               << "      \"cpu\": {\n"
               << "        \"measurement_scope\": \"per_iteration_distribution\",\n"
               << "        \"total_median_ns\": " << result.totalMedianNanoseconds << ",\n"
               << "        \"total_p95_ns\": " << result.totalP95Nanoseconds << ",\n"
               << "        \"layout_median_ns\": " << result.layoutMedianNanoseconds << ",\n"
               << "        \"paint_build_median_ns\": " << result.paintBuildMedianNanoseconds << ",\n"
               << "        \"packet_compile_median_ns\": " << result.packetCompileMedianNanoseconds << ",\n"
               << "        \"allocations_median\": " << result.allocationsMedian << ",\n"
               << "        \"allocated_bytes_median\": " << result.allocatedBytesMedian << ",\n"
               << "        \"peak_transient_bytes\": " << result.peakTransientBytes << ",\n"
               << "        \"copied_box_instances\": " << result.copiedBoxInstances << "\n"
               << "      },\n"
               << "      \"cache\": {\n"
               << "        \"measurement_scope\": \"measured_iterations_cumulative\",\n"
               << "        \"hits\": " << result.cacheHits << ",\n"
               << "        \"misses\": " << result.cacheMisses << ",\n"
               << "        \"hit_rate\": " << std::fixed << std::setprecision(6) << cacheRate << "\n"
               << "      },\n"
               << "      \"batching\": {\n"
               << "        \"measurement_scope\": \"latest_packet_snapshot\",\n"
               << "        \"available\": "
               << (result.packetStatisticsAvailable ? "true" : "false") << ",\n"
               << "        \"source_commands\": " << batching.sourceCommands << ",\n"
               << "        \"compiled_instances\": " << batching.instances << ",\n"
               << "        \"draw_batches\": " << batching.batches << ",\n"
               << "        \"average_instances_per_batch\": "
               << averageInstancesPerBatch << ",\n"
               << "        \"max_instances_per_batch\": "
               << batching.maxInstancesPerBatch << ",\n"
               << "        \"batched_instances_beyond_first\": "
               << batching.batchedInstancesBeyondFirst << ",\n"
               << "        \"textured_batches\": " << batching.texturedBatches << ",\n"
               << "        \"texture_slots_used\": " << batching.textureSlotsUsed << ",\n"
               << "        \"texture_slots_capacity\": " << textureSlotCapacity << ",\n"
               << "        \"texture_table_utilization_ratio\": "
               << textureTableUtilization << ",\n"
               << "        \"max_texture_slots_per_batch\": "
               << batching.maxTextureSlotsPerBatch << ",\n"
               << "        \"clip_state_boundaries\": "
               << batching.clipStateBoundaries << ",\n"
               << "        \"blend_state_boundaries\": "
               << batching.blendStateBoundaries << ",\n"
               << "        \"texture_table_capacity_boundaries\": "
               << batching.textureTableCapacityBoundaries << ",\n"
               << "        \"full_instance_upload_bytes\": "
               << batching.fullInstanceUploadBytes << ",\n"
               << "        \"effect_instances\": " << batching.effectInstances << ",\n"
               << "        \"shader_variant_transitions\": "
               << batching.shaderVariantTransitions << ",\n"
               << "        \"effect_estimated_fragment_area_px\": "
               << batching.effectEstimatedFragmentArea << "\n"
               << "      },\n"
               << "      \"gpu\": {\n"
               << "        \"measurement_scope\": \"per_iteration\",\n"
               << "        \"draw_calls\": " << result.drawCalls << ",\n"
               << "        \"submitted_instances\": " << result.submittedInstances << ",\n"
               << "        \"upload_bytes\": " << result.uploadBytes << ",\n"
               << "        \"cold_upload_bytes\": " << result.coldUploadBytes << ",\n"
               << "        \"estimated_fragment_area_px\": "
               << result.estimatedFragmentArea << ",\n"
               << "        \"timestamp_available\": "
               << (result.gpuTimestampAvailable ? "true" : "false") << ",\n"
               << "        \"timestamp_ns\": " << result.gpuNanoseconds << "\n"
               << "      },\n"
               << "      \"memory\": {\n"
               << "        \"measurement_scope\": \"resident_snapshot\",\n"
               << "        \"cpu_resident_bytes\": " << result.cpuResidentBytes << ",\n"
               << "        \"gpu_buffer_bytes\": " << result.gpuBufferBytes << ",\n"
               << "        \"texture_bytes\": " << result.textureBytes << "\n"
               << "      },\n"
               << "      \"virtualization\": {\n"
               << "        \"logical_items\": " << result.logicalItems << ",\n"
               << "        \"resident_item_widgets\": "
               << result.residentItemWidgets << "\n"
               << "      },\n"
               << "      \"checksum\": " << result.checksum << "\n"
               << "    }" << (index + 1U == results.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

void printResults(std::span<const ScenarioResult> results) {
    std::cout << "HeniaUI reproducible renderer benchmarks\n"
              << "draw layout: command " << sizeof(henia::ui::DrawCommand)
              << " B, instance/glyph upload " << sizeof(henia::ui::DrawInstance)
              << " B, 5 vertex attributes\n"
              << "table scope: per measured iteration (times/allocations are distributions)\n"
              << std::left << std::setw(33) << "scenario"
              << std::right << std::setw(12) << "median us"
              << std::setw(12) << "p95 us"
              << std::setw(12) << "allocs"
              << std::setw(14) << "upload KiB"
              << std::setw(10) << "draws" << '\n';
    for (const ScenarioResult& result : results) {
        std::cout << std::left << std::setw(33) << result.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(12) << static_cast<double>(result.totalMedianNanoseconds) / 1000.0
                  << std::setw(12) << static_cast<double>(result.totalP95Nanoseconds) / 1000.0
                  << std::setw(12) << result.allocationsMedian
                  << std::setw(14) << static_cast<double>(result.uploadBytes) / 1024.0
                  << std::setw(10) << result.drawCalls << '\n';
    }
    std::cout << "GPU timestamps are reported as unavailable in the portable CPU harness; "
                 "backend integrations may fill them from RenderProfile.\n";
}

[[nodiscard]] bool parseUnsigned(std::string_view text, std::size_t& output) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (value > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    output = value;
    return value > 0;
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--verify") {
            options.verify = true;
        } else if ((argument == "--iterations" || argument == "--warmup" || argument == "--json")
            && index + 1 < argc) {
            const std::string_view value = argv[++index];
            if (argument == "--iterations") {
                if (!parseUnsigned(value, options.iterations)) return false;
            } else if (argument == "--warmup") {
                if (!parseUnsigned(value, options.warmup)) return false;
            } else {
                options.jsonPath = value;
            }
        } else if (argument == "--help") {
            std::cout << "Usage: HeniaUIBenchmarks [--iterations N] [--warmup N] "
                         "[--json path] [--verify]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        std::cerr << "Invalid benchmark arguments; use --help\n";
        return EXIT_FAILURE;
    }
    try {
        std::vector<ScenarioResult> results;
        results.reserve(21);
        results.push_back(benchmarkTessellation(options));
        results.push_back(benchmarkShaderEllipses(options));
        results.push_back(benchmarkManyPrimitives(options));
        results.push_back(benchmarkAnalyticFragmentBounds(options));
        results.push_back(benchmarkShaderEffectLayers(options));
        results.push_back(benchmarkStaticRetained(options));
        results.push_back(benchmarkDynamicFullRepaint(options));
        results.push_back(benchmarkDynamicDirty(options));
        results.push_back(benchmarkTextHeavy(options));
        results.push_back(benchmarkVirtualList(options));
        results.push_back(benchmarkRecycledWidgetList(options));
        results.push_back(benchmarkLarge3DFull(options));
        results.push_back(benchmarkLarge3DDirty(options));
        results.push_back(benchmarkVisibility(options, 2048, false));
        results.push_back(benchmarkVisibility(options, 2048, true));
        results.push_back(benchmarkVisibility(options, kBoxCount, false));
        results.push_back(benchmarkVisibility(options, kBoxCount, true));
        results.push_back(benchmarkPaged3DStable(options));
        results.push_back(benchmarkPaged3DOneEdit(options));
        results.push_back(benchmarkPaged3DClusteredEdits(options));
        results.push_back(benchmarkPaged3DSparseEdits(options));
        printResults(results);
        if (!options.jsonPath.empty()) {
            writeJson(options.jsonPath, options, results);
        }
        if (options.verify && !verify(results)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Benchmark failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
