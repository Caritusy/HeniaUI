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
    require(packet.instances().size() == 1034, "unexpected tight-geometry instance count");
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

void testBatchStatisticsDescribeWorkWithoutClaimingCompression() {
    Frame frame;
    frame.reserve(32, 32, 8);
    Canvas& canvas = frame.begin();

    canvas.fillRect({{0.0F, 0.0F}, {10.0F, 10.0F}}, {});
    canvas.strokeRect({{12.0F, 0.0F}, {32.0F, 20.0F}}, {}, 3.0F, 2.0F);
    for (std::uint32_t texture = 1; texture <= 9; ++texture) {
        const float x = 40.0F + static_cast<float>(texture) * 4.0F;
        canvas.image(TextureHandle{texture}, {{x, 0.0F}, {x + 3.0F, 3.0F}});
    }
    require(canvas.pushClip({{0.0F, 0.0F}, {100.0F, 100.0F}}),
        "batch-statistics clip setup failed");
    canvas.fillRect({{2.0F, 24.0F}, {8.0F, 30.0F}}, {});
    canvas.setBlendMode(BlendMode::Additive);
    canvas.fillRect({{10.0F, 24.0F}, {16.0F, 30.0F}}, {});
    require(canvas.popClip(), "batch-statistics clip cleanup failed");
    canvas.setBlendMode(BlendMode::PremultipliedAlpha);
    canvas.fillRect({{18.0F, 24.0F}, {24.0F, 30.0F}}, {});

    const RenderPacket packet = frame.finish();
    const PacketStatistics& statistics = packet.statistics();
    require(statistics.sourceCommands == 14 && statistics.instances == 21
            && statistics.batches == 5,
        "batch statistics lost source, compiled-instance, or draw counts");
    require(statistics.batchedInstancesBeyondFirst == 16
            && statistics.maxInstancesPerBatch == 17,
        "batch density was not defined from per-batch instance counts");
    require(statistics.texturedBatches == 2 && statistics.textureSlotsUsed == 9
            && statistics.maxTextureSlotsPerBatch == DrawBatch::kTextureCapacity,
        "texture-table utilization statistics are incorrect");
    require(statistics.clipStateBoundaries == 2
            && statistics.blendStateBoundaries == 2
            && statistics.textureTableCapacityBoundaries == 1,
        "batch state-boundary reasons are incorrect");
    require(statistics.fullInstanceUploadBytes == 21U * sizeof(DrawInstance),
        "full instance-upload byte count is incorrect");
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

void testFailureSafeClippingAndCulling() {
    DisplayList displayList;
    Canvas canvas(displayList);

    require(canvas.pushClip({{0.0F, 0.0F}, {100.0F, 100.0F}}),
        "outer manual clip push failed");
    Canvas::ClipScope invalid = canvas.scopedClip({{5.0F, 5.0F}, {4.0F, 6.0F}});
    require(!invalid.active() && canvas.clipDepth() == 1,
        "failed scoped clip changed the parent depth");

    Canvas::ClipScope empty = canvas.scopedClip({{120.0F, 120.0F}, {140.0F, 140.0F}});
    require(empty.active() && canvas.clipDepth() == 2,
        "empty intersection was not represented on the clip stack");
    canvas.fillRect({{120.0F, 120.0F}, {130.0F, 130.0F}}, {});
    require(displayList.size() == 0 && canvas.clippedCommands() == 1,
        "drawing under an empty clip was not a recording no-op");
    require(empty.reset() && canvas.clipDepth() == 1,
        "empty clip scope did not restore its parent");

    canvas.fillRect({{103.0F, 10.0F}, {120.0F, 20.0F}}, {});
    canvas.line({104.0F, 30.0F}, {104.0F, 60.0F}, {}, 2.0F, LineCap::Butt);
    canvas.image(TextureHandle{1}, {{100.25F, 82.0F}, {110.0F, 90.0F}});
    canvas.fillRect({{101.0F, 70.0F}, {110.0F, 80.0F}}, {});
    canvas.line({102.0F, 30.0F}, {102.0F, 60.0F}, {}, 2.0F, LineCap::Butt);
    canvas.line({103.0F, 103.0F}, {110.0F, 110.0F}, {}, 2.0F, LineCap::Butt);
    require(displayList.size() == 3 && canvas.clippedCommands() == 4,
        "fully clipped commands were not rejected or AA-fringe commands were over-culled");
    require(canvas.popClip() && canvas.clipDepth() == 0,
        "parent manual clip was corrupted by nested scoped clipping");

    Canvas::ClipScope outer = canvas.scopedClip({{0.0F, 0.0F}, {80.0F, 80.0F}});
    Canvas::ClipScope inner = canvas.scopedClip({{10.0F, 10.0F}, {70.0F, 70.0F}});
    require(outer.reset() && canvas.clipDepth() == 2,
        "out-of-order scope release removed an active child");
    require(inner.reset() && canvas.clipDepth() == 0,
        "out-of-order scope release did not collapse pending parents safely");

    require(canvas.pushClip({{0.0F, 0.0F}, {80.0F, 80.0F}}),
        "manual parent setup failed");
    Canvas::ClipScope manuallyPopped = canvas.scopedClip({{10.0F, 10.0F}, {70.0F, 70.0F}});
    require(canvas.popClip() && !manuallyPopped.reset() && canvas.clipDepth() == 1,
        "scope release after a manual pop removed its parent");
    require(canvas.popClip(), "manual parent cleanup failed");
    require(!canvas.popClip() && canvas.clipDepth() == 0
            && canvas.lastError() == "clipDepth.underflow",
        "clip stack underflow was not rejected without changing depth");

    DisplayList deepDisplayList;
    Canvas deepCanvas(deepDisplayList);
    std::array<Canvas::ClipScope, Canvas::kMaximumClipDepth> scopes{};
    for (Canvas::ClipScope& scope : scopes) {
        scope = deepCanvas.scopedClip({{0.0F, 0.0F}, {10.0F, 10.0F}});
        require(scope.active(), "maximum supported clip depth was rejected early");
    }
    Canvas::ClipScope overflow = deepCanvas.scopedClip({{0.0F, 0.0F}, {10.0F, 10.0F}});
    require(!overflow.active() && deepCanvas.clipDepth() == Canvas::kMaximumClipDepth
            && deepCanvas.capacityRejectedCommands() == 1,
        "clip depth overflow did not fail without changing the stack");
    for (std::size_t index = scopes.size(); index > 0; --index) {
        require(scopes[index - 1].reset(), "clip scope did not release at maximum depth");
    }
    require(deepCanvas.clipDepth() == 0, "maximum-depth scopes did not balance");

    DisplayList rawDisplayList;
    DrawCommand clipped{};
    clipped.bounds = {{20.0F, 20.0F}, {30.0F, 30.0F}};
    clipped.clip = {{{0.0F, 0.0F}, {10.0F, 10.0F}}, true};
    require(rawDisplayList.append(clipped), "raw clipped-command setup failed");
    RenderPacketBuilder builder;
    builder.reserve(1, 1, CapacityPolicy::Fixed);
    require(builder.begin(), "raw clipped-command packet could not begin");
    BatchCompiler compiler;
    require(compiler.compile(rawDisplayList, builder), "raw clipped command failed compilation");
    const RenderPacket packet = builder.publish();
    require(packet.statistics().sourceCommands == 1 && packet.instances().empty()
            && packet.batches().empty(),
        "a fully clipped raw display-list command entered the render packet");

    RenderPacketBuilder retainedBuilder;
    retainedBuilder.reserve(1, 1, CapacityPolicy::Fixed);
    require(retainedBuilder.begin(), "retained clipped-command packet could not begin");
    const std::array segments{
        DisplayListSegment{.identity = 27, .revision = 1, .commands = rawDisplayList.commands()},
    };
    require(compiler.compile(segments, retainedBuilder),
        "retained fully clipped command failed compilation");
    const RenderPacket retainedPacket = retainedBuilder.publish();
    require(retainedPacket.statistics().sourceCommands == 1
            && retainedPacket.instances().empty() && retainedPacket.batches().empty(),
        "a fully clipped retained command entered the render packet");
}

void testTightAnalyticGeometryAndLineStyles() {
    static_assert(sizeof(DrawCommand) == 88, "source commands must remain compact");
    static_assert(sizeof(DrawInstance) == 60, "compiled instances must remain compact");

    Frame frame;
    frame.reserve(5, 12, 4, CapacityPolicy::Fixed);
    frame.setFragmentAreaTracking(true);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{10.0F, 20.0F}, {110.0F, 70.0F}}, {}, 8.0F);
    canvas.strokeRect({{0.0F, 0.0F}, {1000.0F, 500.0F}}, {}, 12.0F, 2.0F);
    canvas.line(
        {0.0F, 0.0F},
        {1920.0F, 1080.0F},
        {},
        1.0F,
        LineCap::Butt);
    const std::array polylinePoints{
        Vec2{20.0F, 100.0F},
        Vec2{50.0F, 70.0F},
        Vec2{80.0F, 100.0F},
    };
    canvas.polyline(
        polylinePoints,
        {0.2F, 0.4F, 0.8F, 0.5F},
        6.0F,
        false,
        LineCap::Square,
        LineJoin::Bevel);

    const std::span<const DrawCommand> commands = frame.displayList().commands();
    require(commands.size() == 5, "polyline did not retain one compact command per segment");
    require(commands[3].lineCap == LineCap::Square
            && commands[3].lineJoin == LineJoin::Bevel
            && commands[3].lineFlags == kLineHasNext
            && commands[3].uv.max == polylinePoints[2]
            && commands[4].lineFlags == kLineHasPrevious
            && commands[4].uv.min == polylinePoints[0],
        "polyline cap/join adjacency metadata is incorrect");
    require(commands[3].bounds.min == polylinePoints[0]
            && commands[3].bounds.max == polylinePoints[1],
        "line command did not retain ordered endpoints in its compact geometry payload");

    const RenderPacket packet = frame.finish();
    require(packet.statistics().sourceCommands == 5 && packet.instances().size() == 12,
        "tight stroke compilation produced an unexpected instance topology");
    require(packet.instances()[0].bounds == Rect{{10.0F, 20.0F}, {110.0F, 70.0F}},
        "filled rectangle did not preserve logical bounds for shader-side AA expansion");
    require(packet.instances()[10].lineCap == LineCap::Square
            && packet.instances()[10].lineJoin() == LineJoin::Bevel
            && packet.instances()[10].lineFlags() == kLineHasNext,
        "compact instance style did not preserve cap, join, and adjacency flags");

    double strokeArea = 0.0;
    for (std::size_t index = 1; index <= 8; ++index) {
        const Rect bounds = packet.instances()[index].bounds;
        strokeArea += static_cast<double>(bounds.width()) * bounds.height();
        require(packet.instances()[index].uv == Rect{{0.0F, 0.0F}, {1000.0F, 500.0F}},
            "stroke region lost its original logical rectangle");
    }
    const double fullStrokeRectangle = 1004.0 * 504.0;
    require(strokeArea < fullStrokeRectangle * 0.1,
        "rectangle stroke geometry still shades its full interior");

    Frame lineFrame;
    lineFrame.reserve(1, 1, 1, CapacityPolicy::Fixed);
    lineFrame.setFragmentAreaTracking(true);
    lineFrame.begin().line({0.0F, 0.0F}, {1920.0F, 1080.0F}, {}, 1.0F, LineCap::Butt);
    const RenderPacket linePacket = lineFrame.finish();
    const std::uint64_t oldAxisAlignedArea = 1925U * 1085U;
    require(linePacket.statistics().estimatedFragmentArea < oldAxisAlignedArea / 100U,
        "viewport diagonal line did not compile to a narrow oriented geometry bound");
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
    canvas.line(
        {-std::numeric_limits<float>::max(), 0.0F},
        {std::numeric_limits<float>::max(), 0.0F},
        {},
        1.0F);
    canvas.strokeRect({{0.0F, 0.0F}, {4.0F, 4.0F}}, {}, nan, 1.0F);
    require(!canvas.pushClip({{5.0F, 0.0F}, {4.0F, 1.0F}}),
        "inverted clip rectangle was accepted");
    require(displayList.commands().empty() && canvas.rejectedCommands() == 5
            && canvas.invalidInputCommands() == 5 && canvas.capacityRejectedCommands() == 0,
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
    scissor = {1, 2, 3, 4};
    require(!makeScissorRect({{8.25F, 1.0F}, {9.0F, 2.0F}}, 8, 8, scissor)
            && scissor.left == 0 && scissor.top == 0
            && scissor.right == 0 && scissor.bottom == 0,
        "fully off-screen scissor was not rejected as an empty rectangle");
    require(makeScissorRect({{0.25F, 0.25F}, {0.75F, 0.75F}}, 8, 8, scissor)
            && scissor.left == 0 && scissor.top == 0
            && scissor.right == 1 && scissor.bottom == 1,
        "subpixel scissor lost fractional edge coverage");
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

void testResourceLifetimeAndTextureBackingPolicies() {
    TextureStore textures;
    std::array<std::byte, 64> rgba{};
    for (std::size_t index = 0; index < rgba.size(); ++index) {
        rgba[index] = static_cast<std::byte>(index);
    }

    TextureHandle stale;
    for (std::uint32_t iteration = 0; iteration < 4096; ++iteration) {
        const TextureHandle handle = textures.create(TextureFormat::Rgba8, 4, 4, 16, rgba);
        require(handle.valid(), "bounded texture creation failed");
        if (iteration == 0) stale = handle;
        if (iteration != 0) {
            require(handle.value() == stale.value(), "texture slot was not reused");
            require(handle.generation() != stale.generation(), "texture generation did not advance");
        }
        require(textures.destroy(handle), "texture destruction failed");
        require(!textures.view(handle).handle.valid(), "destroyed texture handle remained valid");
    }
    require(textures.size() == 0 && textures.slotCount() == 1,
        "texture create/destroy cycles grew the slot table");
    require(textures.statistics().cpuBackingBytes == 0,
        "destroyed texture retained CPU pixel storage");

    const TextureHandle atlas = textures.create(TextureFormat::Rgba8, 4, 4, 16, rgba);
    const std::array<std::byte, 24> patch{
        std::byte{0xA0}, std::byte{0xA1}, std::byte{0xA2}, std::byte{0xA3},
        std::byte{0xB0}, std::byte{0xB1}, std::byte{0xB2}, std::byte{0xB3},
        std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE},
        std::byte{0xC0}, std::byte{0xC1}, std::byte{0xC2}, std::byte{0xC3},
        std::byte{0xD0}, std::byte{0xD1}, std::byte{0xD2}, std::byte{0xD3},
        std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE}, std::byte{0xEE},
    };
    const TextureRegion region{1, 1, 2, 2};
    require(textures.updateRegion(atlas, region, 12, patch), "texture region update failed");
    const TextureView updated = textures.view(atlas);
    require(updated.revision == 2 && !updated.fullUpdate && updated.dirtyRegion == region,
        "texture region metadata was not retained");
    require(updated.rollbackPixels.size() == 16,
        "texture region rollback data was not tightly retained");
    for (std::uint32_t row = 0; row < 4; ++row) {
        for (std::uint32_t column = 0; column < 4; ++column) {
            for (std::uint32_t component = 0; component < 4; ++component) {
                const std::size_t offset = row * 16U + column * 4U + component;
                const bool inside = row >= 1 && row < 3 && column >= 1 && column < 3;
                const std::byte expected = inside
                    ? patch[(row - 1U) * 12U + (column - 1U) * 4U + component]
                    : rgba[offset];
                require(updated.pixels[offset] == expected,
                    "texture region update modified bytes outside the requested rectangle");
            }
        }
    }

    const TextureHandle discardable = textures.create(
        TextureFormat::Rgba8,
        4,
        4,
        16,
        rgba,
        {.backingPolicy = TextureBackingPolicy::DiscardAfterUpload});
    require(textures.discardCpuBacking(discardable)
            && !textures.view(discardable).backingAvailable,
        "discard-after-upload texture retained CPU backing");
    require(!textures.ensureCpuBacking(discardable),
        "discard-after-upload texture unexpectedly regenerated backing");
    require(textures.restoreCpuBacking(discardable, 16, rgba)
            && textures.view(discardable).backingAvailable,
        "explicit texture backing restoration failed");

    std::uint32_t regenerationCalls = 0;
    const TextureHandle regenerable = textures.create(
        TextureFormat::Rgba8,
        4,
        4,
        16,
        rgba,
        {
            .backingPolicy = TextureBackingPolicy::Regenerable,
            .regenerator = [&]() {
                ++regenerationCalls;
                return std::vector<std::byte>(rgba.begin(), rgba.end());
            },
        });
    require(textures.discardCpuBacking(regenerable)
            && textures.ensureCpuBacking(regenerable)
            && regenerationCalls == 1,
        "regenerable texture did not restore its CPU backing");
    require(textures.statistics().backingRestorations == 1,
        "texture backing restoration statistics are incorrect");

    const TextureHandle external = textures.createExternal(TextureFormat::Rgba8, 4, 4);
    const TextureView externalView = textures.view(external);
    require(externalView.handle == external
            && externalView.backingPolicy == TextureBackingPolicy::ExternalGpu
            && !externalView.backingAvailable
            && !textures.update(external, 16, rgba),
        "external GPU texture accepted CPU ownership operations");

    FontStore fonts;
    const auto definition = [&]() {
        return FontDefinition{
            .atlas = atlas,
            .pixelSize = 10.0F,
            .ascent = 8.0F,
            .descent = 2.0F,
            .glyphs = {{U'A', {}, {5.0F, 8.0F}, {}, 6.0F}},
        };
    };
    const FontHandle firstFont = fonts.add(definition());
    TextRunCache cache(fonts);
    require(cache.layout(firstFont, 10.0F, "A") != nullptr,
        "font cache fixture could not be populated");
    require(fonts.destroy(firstFont), "font destruction failed");
    const FontHandle replacementFont = fonts.add(definition());
    require(replacementFont.value() == firstFont.value()
            && replacementFont.generation() != firstFont.generation()
            && fonts.find(firstFont) == nullptr
            && cache.layout(firstFont, 10.0F, "A") == nullptr
            && cache.layout(replacementFont, 10.0F, "A") != nullptr,
        "font slot reuse did not reject a stale cached generation");
    require(fonts.destroy(replacementFont), "replacement font destruction failed");
    for (std::uint32_t iteration = 0; iteration < 4096; ++iteration) {
        const FontHandle handle = fonts.add(definition());
        require(handle.valid() && handle.value() == firstFont.value(),
            "font slot was not reused");
        require(fonts.destroy(handle), "font destruction cycle failed");
    }
    require(fonts.size() == 0 && fonts.slotCount() == 1,
        "font create/destroy cycles grew the slot table");
}

} // namespace

int main() {
    testMixedUiUsesOneBatch();
    testClipAndBlendPreserveOrdering();
    testTextureTableOverflowStartsOneNewBatch();
    testBatchStatisticsDescribeWorkWithoutClaimingCompression();
    testWarmFrameDoesNotGrow();
    testNestedClipIntersection();
    testFailureSafeClippingAndCulling();
    testTightAnalyticGeometryAndLineStyles();
    testFixedCapacityOverflowIsRejected();
    testInvalidGeometryAndCheckedArithmetic();
    testPacketSnapshotsRemainImmutable();
    testFixedSnapshotPoolRejectsExhaustion();
    testPacketSnapshotsSupportConcurrentConsumption();
    testUtf8Validation();
    testTextRunsAreCachedAndBatched();
    testResourceLifetimeAndTextureBackingPolicies();
    std::cout << "HeniaUI core tests passed\n";
    return EXIT_SUCCESS;
}
