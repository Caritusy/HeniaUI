#include "henia/ui/BatchCompiler.h"
#include "henia/ui/Canvas.h"
#include "henia/ui/Frame.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using namespace henia::ui;

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

    const RenderPacket& packet = frame.finish();
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

    const RenderPacket& packet = frame.finish();
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

    const RenderPacket& packet = frame.finish();
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
    const std::uint64_t displayGrowths = frame.displayList().capacityGrowths();
    const PacketStatistics second = record();

    require(first.batches == 1 && second.batches == 1, "stable frame did not remain one batch");
    require(frame.displayList().capacityGrowths() == displayGrowths, "display list grew after warm-up");
    require(
        second.instanceCapacityGrowths == first.instanceCapacityGrowths,
        "instance storage grew after warm-up");
    require(second.batchCapacityGrowths == first.batchCapacityGrowths, "batch storage grew after warm-up");
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

} // namespace

int main() {
    testMixedUiUsesOneBatch();
    testClipAndBlendPreserveOrdering();
    testTextureTableOverflowStartsOneNewBatch();
    testWarmFrameDoesNotGrow();
    testNestedClipIntersection();
    std::cout << "HeniaUI core tests passed\n";
    return EXIT_SUCCESS;
}
