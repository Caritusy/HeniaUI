#include "henia/ui/BatchCompiler.h"
#include "henia/ui/Canvas.h"
#include "henia/CheckedArithmetic.h"
#include "henia/ui/Frame.h"
#include "henia/ui/Validation.h"
#include "henia/ui/backend/opengl/OpenGlRenderer.h"
#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/text/Utf8.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace henia::ui;

static_assert(!std::is_move_constructible_v<OpenGlRenderer>);
static_assert(!std::is_move_assignable_v<OpenGlRenderer>);
static_assert(std::is_nothrow_copy_constructible_v<RenderPacket>);
static_assert(std::is_nothrow_move_constructible_v<RenderPacket>);
static_assert(!std::is_copy_constructible_v<RenderPacketBuilder>);

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

void testMixedUiUsesOneBatch() {
    Frame frame;
    frame.reserve(2048, 16);
    Canvas& canvas = frame.begin();

    canvas.fillRect({{10.0F, 10.0F}, {410.0F, 310.0F}}, {0.04F, 0.06F, 0.09F, 1.0F}, 12.0F);
    canvas.strokeRect(
        {{10.0F, 10.0F}, {410.0F, 310.0F}},
        {0.15F, 0.72F, 0.94F, 1.0F},
        12.0F,
        1.0F);

    std::array<GlyphQuad, 1024> glyphs{};
    for (std::size_t index = 0; index < glyphs.size(); ++index) {
        const float x = 20.0F + static_cast<float>(index % 64) * 6.0F;
        const float y = 30.0F + static_cast<float>(index / 64) * 12.0F;
        glyphs[index] = {{{x, y}, {x + 5.0F, y + 10.0F}}, {{0.0F, 0.0F}, {0.1F, 0.1F}}};
    }
    canvas.glyphs(TextureHandle{1}, glyphs, {0.92F, 0.96F, 1.0F, 1.0F});
    canvas.fillRect({{20.0F, 270.0F}, {200.0F, 290.0F}}, {0.20F, 0.75F, 0.55F, 1.0F}, 5.0F);

    const RenderPacket packet = frame.finish();
    require(packet.statistics().sourceCommands == 1027, "unexpected source command count");
    require(packet.instances().size() == 1027, "unexpected instance count");
    require(packet.batches().size() == 1, "shape and glyph work did not merge into one UI batch");
    require(packet.batches().front().textureCount == 1, "font atlas was not assigned to the batch texture table");
}

void testClipAndBlendPreserveOrdering() {
    Frame frame;
    frame.reserve(16, 8);
    Canvas& canvas = frame.begin();

    canvas.fillRect({{0.0F, 0.0F}, {50.0F, 50.0F}}, {}, 0.0F);
    require(canvas.pushClip({{10.0F, 10.0F}, {40.0F, 40.0F}}), "clip push failed");
    canvas.fillRect({{0.0F, 0.0F}, {50.0F, 50.0F}}, {}, 0.0F);
    canvas.setBlendMode(BlendMode::Additive);
    canvas.fillRect({{12.0F, 12.0F}, {20.0F, 20.0F}}, {}, 0.0F);
    canvas.setBlendMode(BlendMode::PremultipliedAlpha);
    require(canvas.popClip(), "clip pop failed");
    canvas.fillRect({{60.0F, 0.0F}, {90.0F, 30.0F}}, {}, 0.0F);

    const RenderPacket packet = frame.finish();
    require(packet.batches().size() == 4, "clip or blend boundary was merged incorrectly");
    require(!packet.batches()[0].clip.enabled, "first batch unexpectedly clipped");
    require(packet.batches()[1].clip.enabled, "second batch lost its clip");
    require(packet.batches()[2].blend == BlendMode::Additive, "additive boundary was lost");
    require(!packet.batches()[3].clip.enabled, "final batch unexpectedly clipped");
}

void testTextureTableOverflowStartsOneNewBatch() {
    Frame frame;
    frame.reserve(16, 4);
    Canvas& canvas = frame.begin();

    for (std::uint32_t texture = 1; texture <= 9; ++texture) {
        const float x = static_cast<float>(texture) * 10.0F;
        canvas.image(TextureHandle{texture}, {{x, 0.0F}, {x + 8.0F, 8.0F}});
    }

    const RenderPacket packet = frame.finish();
    require(packet.batches().size() == 2, "texture table overflow did not start exactly one new batch");
    require(packet.batches()[0].textureCount == DrawBatch::kTextureCapacity, "first texture table was not filled");
    require(packet.batches()[1].textureCount == 1, "overflow texture was not retained");
}

void testWarmFrameDoesNotGrow() {
    Frame frame;
    frame.reserve(256, 8);

    const auto record = [&frame]() {
        Canvas& canvas = frame.begin();
        for (int index = 0; index < 128; ++index) {
            const float x = static_cast<float>(index);
            canvas.fillRect({{x, x}, {x + 10.0F, x + 10.0F}}, {}, 2.0F);
        }
        return frame.finish().statistics();
    };

    const PacketStatistics first = record();
    const std::uint64_t firstRevision = frame.packet().revision();
    require(frame.packet().identity() != 0, "compiled frame identity is invalid");
    const std::uint64_t displayGrowths = frame.displayList().capacityGrowths();
    const PacketStatistics second = record();

    require(first.batches == 1 && second.batches == 1, "stable frame did not remain one batch");
    require(frame.displayList().capacityGrowths() == displayGrowths, "display list grew after warm-up");
    require(
        second.instanceCapacityGrowths == first.instanceCapacityGrowths,
        "instance storage grew after warm-up");
    require(second.batchCapacityGrowths == first.batchCapacityGrowths, "batch storage grew after warm-up");
    require(frame.packet().revision() == firstRevision + 1, "compiled frame revision did not advance");
}

void testNestedClipIntersection() {
    DisplayList displayList;
    Canvas canvas(displayList);

    require(canvas.pushClip({{0.0F, 0.0F}, {100.0F, 100.0F}}), "outer clip push failed");
    require(canvas.pushClip({{50.0F, 20.0F}, {120.0F, 80.0F}}), "inner clip push failed");
    canvas.fillRect({{0.0F, 0.0F}, {120.0F, 120.0F}}, {}, 0.0F);

    require(displayList.size() == 1, "clipped primitive was not recorded");
    require(
        displayList.commands().front().clip.area == Rect{{50.0F, 20.0F}, {100.0F, 80.0F}},
        "nested clip was not intersected");
}

void testFixedCapacityOverflowIsRejected() {
    Frame frame;
    frame.reserve(1, 1, CapacityPolicy::Fixed);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{0.0F, 0.0F}, {10.0F, 10.0F}}, {});
    canvas.fillRect({{12.0F, 0.0F}, {22.0F, 10.0F}}, {});
    require(canvas.rejectedCommands() == 1 && canvas.invalidInputCommands() == 0
            && canvas.capacityRejectedCommands() == 1,
        "fixed display-list overflow was not classified as capacity exhaustion");
    const RenderPacket& framePacket = frame.finish();
    require(framePacket.instances().size() == 1, "fixed display-list overflow changed accepted work");

    DisplayList displayList;
    displayList.reserve(2);
    DrawCommand command{};
    command.bounds = {{0.0F, 0.0F}, {1.0F, 1.0F}};
    require(displayList.append(command) && displayList.append(command), "test commands were not recorded");
    RenderPacketBuilder builder;
    builder.reserve(1, 1, CapacityPolicy::Fixed);
    require(builder.begin(), "packet builder did not acquire a snapshot slot");
    BatchCompiler compiler;
    require(!compiler.compile(displayList, builder), "fixed packet overflow unexpectedly compiled");
    const RenderPacket packet = builder.publish();
    require(packet.instances().empty() && packet.batches().empty(), "packet overflow published partial work");
    require(packet.statistics().sourceCommands == 2 && packet.statistics().rejectedCommands == 2
            && packet.statistics().invalidInputCommands == 0
            && packet.statistics().capacityRejectedCommands == 2,
        "packet overflow statistics are incorrect");
}

void testInvalidGeometryAndCheckedArithmetic() {
    DisplayList displayList;
    Canvas canvas(displayList);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    canvas.fillRect({{0.0F, 0.0F}, {nan, 1.0F}}, {});
    canvas.line({0.0F, 0.0F}, {1.0F, infinity}, {}, 1.0F);
    canvas.strokeRect({{0.0F, 0.0F}, {4.0F, 4.0F}}, {}, nan, 1.0F);
    require(!canvas.pushClip({{5.0F, 0.0F}, {4.0F, 1.0F}}),
        "inverted clip rectangle was accepted");
    require(displayList.commands().empty() && canvas.rejectedCommands() == 4
            && canvas.invalidInputCommands() == 4 && canvas.capacityRejectedCommands() == 0,
        "non-finite/inverted Canvas inputs were not rejected and classified");
    require(canvas.lastError() == "clip.area",
        "Canvas diagnostics did not identify the rejected field");

    DrawCommand invalid{};
    invalid.bounds = {{0.0F, 0.0F}, {nan, 1.0F}};
    require(displayList.append(invalid), "raw invalid command setup failed");
    RenderPacketBuilder builder;
    builder.reserve(4, 2, CapacityPolicy::Fixed);
    require(builder.begin(), "invalid command test could not begin packet");
    BatchCompiler compiler;
    require(!compiler.compile(displayList, builder), "raw invalid command compiled");
    const RenderPacket rejected = builder.publish();
    require(rejected.instances().empty() && rejected.batches().empty()
            && rejected.statistics().invalidInputCommands == 1
            && rejected.statistics().capacityRejectedCommands == 0,
        "raw invalid command rejection was not transactional/classified");

    ScissorRect scissor{};
    require(makeScissorRect({{-2.25F, 3.75F}, {10.10F, 20.01F}}, 8, 16, scissor)
            && scissor.left == 0 && scissor.top == 3
            && scissor.right == 8 && scissor.bottom == 16,
        "fractional scissor did not floor minima, ceil maxima, and clamp");
    require(!makeScissorRect({{0.0F, 0.0F}, {nan, 2.0F}}, 8, 8, scissor),
        "non-finite scissor was accepted");
    const std::uint32_t maximumViewport = static_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::max());
    require(makeScissorRect(
            {{0.0F, 0.0F}, {std::numeric_limits<float>::max(), 1.0F}},
            maximumViewport,
            8,
            scissor)
            && scissor.right == std::numeric_limits<std::int32_t>::max(),
        "maximum viewport scissor narrowing was not range safe");

    std::size_t result = 0;
    require(henia::checkedMultiply<std::size_t>(7, 9, result) && result == 63,
        "checked multiplication rejected a valid product");
    require(!henia::checkedMultiply(
            std::numeric_limits<std::size_t>::max(), std::size_t{2}, result),
        "checked multiplication accepted overflow");
    require(!henia::checkedAdd(
            std::numeric_limits<std::size_t>::max(), std::size_t{1}, result),
        "checked addition accepted overflow");
    std::uint32_t narrowed = 0;
    require(!henia::checkedNarrow(
            std::numeric_limits<std::uint64_t>::max(), narrowed),
        "checked narrowing accepted an out-of-range value");
}

void testPacketSnapshotsRemainImmutable() {
    Frame frame;
    frame.reserve(16, 4, CapacityPolicy::Fixed);

    Canvas& firstCanvas = frame.begin();
    firstCanvas.fillRect({{1.0F, 2.0F}, {3.0F, 4.0F}}, {0.25F, 0.5F, 0.75F, 1.0F});
    const RenderPacket first = frame.finish();
    const DrawInstance original = first.instances().front();
    const std::uint64_t identity = first.identity();
    const std::uint64_t revision = first.revision();

    for (int frameIndex = 0; frameIndex < 8; ++frameIndex) {
        Canvas& canvas = frame.begin();
        for (int instanceIndex = 0; instanceIndex <= frameIndex; ++instanceIndex) {
            const float x = static_cast<float>(frameIndex * 10 + instanceIndex);
            canvas.fillRect({{x, x}, {x + 1.0F, x + 1.0F}}, {});
        }
        const RenderPacket next = frame.finish();
        require(next.revision() > revision, "new snapshot revision did not advance");
        require(first.identity() == identity && first.revision() == revision,
            "old snapshot identity or revision changed");
        require(first.instances().size() == 1 && first.instances().front().bounds == original.bounds
                && first.instances().front().color == original.color,
            "old snapshot storage was mutated by a later composition");
    }

    const RenderPacket stable = frame.finish();
    require(stable.identity() == frame.packet().identity()
            && stable.revision() == frame.packet().revision(),
        "stable frame did not reuse snapshot storage");
    require(frame.snapshotSlotCount() == RenderPacketBuilder::kDefaultSnapshotSlots
            && frame.snapshotSlotGrowths() == 0,
        "warmed fixed snapshot pool unexpectedly grew");

    RenderPacket survivingSnapshot;
    {
        Frame temporaryFrame;
        temporaryFrame.reserve(4, 2, CapacityPolicy::Fixed);
        temporaryFrame.begin().fillRect({{7.0F, 8.0F}, {9.0F, 10.0F}}, {});
        survivingSnapshot = temporaryFrame.finish();
    }
    require(survivingSnapshot.instances().size() == 1
            && survivingSnapshot.instances().front().bounds.min == Vec2{7.0F, 8.0F},
        "snapshot storage did not outlive its producer frame");
}

void testFixedSnapshotPoolRejectsExhaustion() {
    Frame frame;
    frame.reserve(8, 2, CapacityPolicy::Fixed, 2);

    frame.begin().fillRect({{1.0F, 0.0F}, {2.0F, 1.0F}}, {});
    const RenderPacket first = frame.finish();
    frame.begin().fillRect({{2.0F, 0.0F}, {3.0F, 1.0F}}, {});
    const RenderPacket second = frame.finish();
    frame.begin().fillRect({{3.0F, 0.0F}, {4.0F, 1.0F}}, {});
    const RenderPacket rejected = frame.finish();

    require(first.identity() != second.identity(), "concurrent snapshots shared mutable storage");
    require(rejected.identity() == second.identity() && rejected.revision() == second.revision(),
        "fixed snapshot exhaustion replaced the last valid packet");
    require(frame.snapshotSlotCount() == 2 && frame.snapshotSlotGrowths() == 0
            && frame.rejectedFrames() == 1 && !frame.lastBuildPublished(),
        "fixed snapshot exhaustion was not reported deterministically");
}

void testPacketSnapshotsSupportConcurrentConsumption() {
    Frame frame;
    frame.reserve(96, 4, CapacityPolicy::Grow);
    std::mutex publicationMutex;
    RenderPacket published;
    std::atomic_bool producerDone = false;
    std::atomic_bool failed = false;
    std::uint64_t consumedSnapshots = 0;

    std::thread consumer([&]() {
        for (;;) {
            RenderPacket snapshot;
            {
                const std::scoped_lock lock(publicationMutex);
                snapshot = published;
            }
            if (!snapshot) {
                if (producerDone.load(std::memory_order_acquire)) {
                    return;
                }
                std::this_thread::yield();
                continue;
            }
            ++consumedSnapshots;
            const std::uint64_t identity = snapshot.identity();
            const std::uint64_t revision = snapshot.revision();
            const std::span<const DrawInstance> instances = snapshot.instances();
            const std::size_t count = instances.size();
            const float marker = instances.empty() ? 0.0F : instances.front().bounds.min.x;
            std::this_thread::yield();
            if (snapshot.identity() != identity || snapshot.revision() != revision
                || snapshot.instances().size() != count
                || snapshot.statistics().instances != count) {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
            for (const DrawInstance& instance : snapshot.instances()) {
                if (instance.bounds.min.x != marker) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
            if (producerDone.load(std::memory_order_acquire)) {
                return;
            }
        }
    });

    for (int frameIndex = 1; frameIndex <= 2000; ++frameIndex) {
        Canvas& canvas = frame.begin();
        const int count = 1 + frameIndex % 64;
        const float marker = static_cast<float>(frameIndex);
        for (int index = 0; index < count; ++index) {
            canvas.fillRect({
                {marker, static_cast<float>(index)},
                {marker + 1.0F, static_cast<float>(index + 1)},
            }, {});
        }
        RenderPacket snapshot = frame.finish();
        {
            const std::scoped_lock lock(publicationMutex);
            published = std::move(snapshot);
        }
    }
    producerDone.store(true, std::memory_order_release);
    consumer.join();
    require(consumedSnapshots != 0 && !failed.load(std::memory_order_relaxed),
        "concurrent consumer observed a mutated packet snapshot");
}

void testUtf8Validation() {
    const std::string_view text = "A\xE4\xB8\xAD\xF0\x9F\x94\xA5";
    const Utf8Codepoint ascii = decodeUtf8(text, 0);
    const Utf8Codepoint cjk = decodeUtf8(text, 1);
    const Utf8Codepoint emoji = decodeUtf8(text, 4);
    require(ascii.valid && ascii.value == U'A' && ascii.bytes == 1, "ASCII decoding failed");
    require(cjk.valid && cjk.value == U'\u4E2D' && cjk.bytes == 3, "CJK decoding failed");
    require(emoji.valid && emoji.value == U'\U0001F525' && emoji.bytes == 4, "four-byte UTF-8 decoding failed");

    const std::string_view overlong = "\xC0\xAF";
    const Utf8Codepoint invalid = decodeUtf8(overlong, 0);
    require(!invalid.valid && invalid.bytes == 1, "overlong UTF-8 was accepted");
}

void testTextRunsAreCachedAndBatched() {
    TextureStore textures;
    const std::array<std::byte, 16> pixels{};
    const TextureHandle atlas = textures.create(TextureFormat::Alpha8, 4, 4, 4, pixels);
    require(atlas.valid(), "test atlas creation failed");

    FontStore fonts;
    std::vector<GlyphMetrics> glyphs{
        {U'?', {{0.0F, 0.0F}, {0.25F, 0.25F}}, {5.0F, 8.0F}, {0.0F, 7.0F}, 6.0F},
        {U'A', {{0.25F, 0.0F}, {0.5F, 0.25F}}, {6.0F, 8.0F}, {0.0F, 7.0F}, 7.0F},
        {U'V', {{0.5F, 0.0F}, {0.75F, 0.25F}}, {6.0F, 8.0F}, {0.0F, 7.0F}, 7.0F},
    };
    std::vector<KerningPair> kerning{{U'A', U'V', -1.0F}};
    const FontHandle font = fonts.add({
        .atlas = atlas,
        .pixelSize = 10.0F,
        .ascent = 8.0F,
        .descent = 2.0F,
        .lineGap = 1.0F,
        .glyphs = std::move(glyphs),
        .kerning = std::move(kerning),
    });
    require(font.valid(), "test font creation failed");

    TextRunCache cache(fonts);
    cache.reserve(8, 16);
    TextPainter painter(cache);
    const TextMetrics firstMetrics = painter.measure(font, 20.0F, "AVA");
    const TextMetrics secondMetrics = painter.measure(font, 20.0F, "AVA");
    require(firstMetrics.width == 40.0F && firstMetrics.height == 22.0F, "scaled text metrics are incorrect");
    require(secondMetrics.width == firstMetrics.width, "cached metrics changed");
    require(cache.misses() == 1 && cache.hits() == 1, "text run cache did not hit");

    cache.setMaximumEntries(2);
    static_cast<void>(painter.measure(font, 20.0F, "A"));
    static_cast<void>(painter.measure(font, 20.0F, "V"));
    static_cast<void>(painter.measure(font, 20.0F, "AV"));
    require(cache.size() == 2, "dynamic text exceeded the configured cache bound");
    const std::uint64_t missesBeforeRevisit = cache.misses();
    static_cast<void>(painter.measure(font, 20.0F, "A"));
    require(cache.misses() == missesBeforeRevisit + 1, "evicted text unexpectedly remained indexed");

    Frame frame;
    frame.reserve(16, 4);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{0.0F, 0.0F}, {100.0F, 40.0F}}, {}, 4.0F);
    painter.draw(canvas, font, 20.0F, {4.0F, 4.0F}, {}, "AVA");
    const RenderPacket packet = frame.finish();
    require(packet.instances().size() == 4, "text run did not emit three glyph instances");
    require(packet.batches().size() == 1, "text and shape did not share one batch");
}

} // namespace

int main() {
    testMixedUiUsesOneBatch();
    testClipAndBlendPreserveOrdering();
    testTextureTableOverflowStartsOneNewBatch();
    testWarmFrameDoesNotGrow();
    testNestedClipIntersection();
    testFixedCapacityOverflowIsRejected();
    testInvalidGeometryAndCheckedArithmetic();
    testPacketSnapshotsRemainImmutable();
    testFixedSnapshotPoolRejectsExhaustion();
    testPacketSnapshotsSupportConcurrentConsumption();
    testUtf8Validation();
    testTextRunsAreCachedAndBatched();
    std::cout << "HeniaUI core tests passed\n";
    return EXIT_SUCCESS;
}
