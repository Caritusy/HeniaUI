#include "henia/gfx/ShapeBatch3D.h"
#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"
#include "henia/ui/Frame.h"
#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"
#include "henia/ui/text/Utf8.h"
#include "henia/ui/widget/controls/Panel.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace henia::ui;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

[[nodiscard]] bool scalar(char32_t value) noexcept {
    return value <= U'\U0010FFFF'
        && !(value >= static_cast<char32_t>(0xD800) && value <= static_cast<char32_t>(0xDFFF));
}

[[nodiscard]] std::string encodeUtf8(char32_t value) {
    std::string result;
    if (value <= 0x7F) {
        result.push_back(static_cast<char>(value));
    } else if (value <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | (value >> 6)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else if (value <= 0xFFFF) {
        result.push_back(static_cast<char>(0xE0 | (value >> 12)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        result.push_back(static_cast<char>(0xF0 | (value >> 18)));
        result.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
    return result;
}

void fuzzUtf8() {
    std::mt19937_64 random(0x48454E4941554932ULL);
    std::uniform_int_distribution<std::uint32_t> scalarDistribution(0, 0x10FFFF);
    for (int iteration = 0; iteration < 20000; ++iteration) {
        char32_t expected{};
        do {
            expected = static_cast<char32_t>(scalarDistribution(random));
        } while (!scalar(expected));
        const std::string encoded = encodeUtf8(expected);
        const Utf8Codepoint decoded = decodeUtf8(encoded, 0);
        if (!decoded.valid || decoded.value != expected || decoded.bytes != encoded.size()) {
            fail("UTF-8 scalar round-trip property failed");
        }
    }

    std::uniform_int_distribution<int> byteDistribution(0, 255);
    for (int iteration = 0; iteration < 10000; ++iteration) {
        std::array<char, 16> bytes{};
        for (char& byte : bytes) {
            byte = static_cast<char>(byteDistribution(random));
        }
        const std::string_view input(bytes.data(), bytes.size());
        for (std::size_t offset = 0; offset < input.size(); ++offset) {
            const Utf8Codepoint decoded = decodeUtf8(input, offset);
            if (decoded.bytes == 0 || decoded.bytes > 4 || decoded.bytes > input.size() - offset
                || (decoded.valid && !scalar(decoded.value))) {
                fail("UTF-8 decoder violated progress or scalar bounds");
            }
        }
    }
}

void propertyLayoutConstraints() {
    FontStore fonts;
    TextRunCache cache(fonts);
    TextPainter text(cache);
    std::mt19937 random(0x25A11D);
    std::uniform_real_distribution<float> extent(0.0F, 2048.0F);
    std::uniform_real_distribution<float> inset(0.0F, 32.0F);

    for (int iteration = 0; iteration < 5000; ++iteration) {
        const float maximumWidth = extent(random);
        const float maximumHeight = extent(random);
        std::uniform_real_distribution<float> minimumWidth(0.0F, maximumWidth);
        std::uniform_real_distribution<float> minimumHeight(0.0F, maximumHeight);
        const Constraints constraints{
            .minimum = {minimumWidth(random), minimumHeight(random)},
            .maximum = {maximumWidth, maximumHeight},
        };
        Panel panel({
            .padding = {inset(random), inset(random), inset(random), inset(random)},
            .gap = inset(random),
            .direction = iteration % 2 == 0 ? LayoutDirection::Row : LayoutDirection::Column,
        });
        for (int child = 0; child < iteration % 5; ++child) {
            Widget& widget = panel.emplaceChild<Widget>();
            widget.setLayoutParameters({
                .width = extent(random),
                .height = extent(random),
                .flexGrow = static_cast<float>(child % 3),
            });
        }
        const Vec2 measured = panel.measure(text, constraints);
        if (!std::isfinite(measured.x) || !std::isfinite(measured.y)
            || measured.x < constraints.minimum.x || measured.y < constraints.minimum.y
            || measured.x > constraints.maximum.x || measured.y > constraints.maximum.y) {
            fail("Widget measurement escaped finite constraints");
        }
        panel.arrange(text, {{0.0F, 0.0F}, measured});
        if (panel.frame().min != Vec2{} || panel.frame().max != measured) {
            fail("Widget arrangement changed the measured frame");
        }
    }
}

void propertyBatchCompilation() {
    std::mt19937 random(0xBA7C4125);
    std::uniform_real_distribution<float> coordinate(-4096.0F, 4096.0F);
    std::uniform_real_distribution<float> size(0.01F, 256.0F);
    std::uniform_int_distribution<std::uint32_t> texture(1, 24);

    for (int iteration = 0; iteration < 1000; ++iteration) {
        Frame frame;
        frame.reserve(256, 64);
        Canvas& canvas = frame.begin();
        const int commandCount = 1 + iteration % 128;
        for (int command = 0; command < commandCount; ++command) {
            const float x = coordinate(random);
            const float y = coordinate(random);
            const float width = size(random);
            const float height = size(random);
            if (command % 7 == 0) {
                canvas.image(
                    TextureHandle{texture(random)},
                    {{x, y}, {x + width, y + height}});
            } else {
                canvas.fillRect(
                    {{x, y}, {x + width, y + height}},
                    {0.2F, 0.4F, 0.8F, 0.75F},
                    std::min(width, height) * 0.25F);
            }
        }
        const RenderPacket packet = frame.finish();
        std::uint64_t coveredInstances = 0;
        for (const DrawBatch& batch : packet.batches()) {
            if (batch.textureCount > DrawBatch::kTextureCapacity
                || batch.firstInstance != coveredInstances
                || batch.instanceCount > packet.instances().size() - coveredInstances) {
                fail("Compiled batch violated texture or instance bounds");
            }
            coveredInstances += batch.instanceCount;
        }
        if (coveredInstances != packet.instances().size()
            || packet.statistics().instances != packet.instances().size()
            || packet.statistics().batches != packet.batches().size()) {
            fail("Compiled batch coverage/statistics property failed");
        }
    }
}

void fuzzFiniteGeometryAndArithmetic() {
    std::mt19937 random(0xF117E123);
    std::uniform_int_distribution<std::uint32_t> bits(
        0, std::numeric_limits<std::uint32_t>::max());
    Frame frame;
    frame.reserve(2, 1, CapacityPolicy::Fixed);
    for (int iteration = 0; iteration < 50000; ++iteration) {
        const float value = std::bit_cast<float>(bits(random));
        Canvas& canvas = frame.begin();
        canvas.fillRect({{0.0F, 0.0F}, {value, 1.0F}}, {value, 0.5F, 0.25F, 1.0F});
        const RenderPacket packet = frame.finish();
        for (const DrawInstance& instance : packet.instances()) {
            const std::array constants{
                instance.bounds.min.x, instance.bounds.min.y,
                instance.bounds.max.x, instance.bounds.max.y,
                instance.color.red, instance.color.green,
                instance.color.blue, instance.color.alpha,
                instance.radius, instance.thickness,
            };
            if (!std::all_of(constants.begin(), constants.end(), [](float component) {
                    return std::isfinite(component);
                })) {
                fail("Geometry fuzz published a non-finite GPU constant");
            }
        }

        henia::gfx::BoxInstance box{};
        box.maximum.x = value;
        const bool accepted = henia::gfx::validate(box).empty();
        if (accepted && !std::isfinite(value)) {
            fail("3D geometry fuzz accepted a non-finite GPU constant");
        }

        const std::size_t left = static_cast<std::size_t>(bits(random));
        const std::size_t right = static_cast<std::size_t>(bits(random));
        std::size_t product = 0;
        const bool multiplied = henia::checkedMultiply(left, right, product);
        const bool expected = left == 0
            || right <= std::numeric_limits<std::size_t>::max() / left;
        if (multiplied != expected || (multiplied && product != left * right)) {
            fail("Checked-capacity multiplication property failed");
        }
        std::size_t sum = 0;
        const bool added = henia::checkedAdd(left, right, sum);
        const bool addExpected = right <= std::numeric_limits<std::size_t>::max() - left;
        if (added != addExpected || (added && sum != left + right)) {
            fail("Checked-capacity addition property failed");
        }
    }
}

void stressImmutable3dSnapshots() {
    using namespace henia::gfx;
    ShapeBatch3D shapes;
    shapes.reserve(4096);
    std::vector<BoxInstance> boxes(2048);
    shapes.replaceBoxes(boxes);
    const InstanceBatch original = shapes.snapshot();
    for (std::size_t iteration = 0; iteration < 20000; ++iteration) {
        const std::size_t index = iteration % boxes.size();
        BoxInstance changed = boxes[index];
        changed.lineWidth = 2.0F
            + static_cast<float>((iteration / boxes.size()) % 2U);
        boxes[index] = changed;
        if (!shapes.updateBox(index, changed)) {
            fail("3D snapshot stress update failed");
        }
        const InstanceBatch snapshot = shapes.snapshot();
        if (snapshot.boxes()[index].lineWidth != changed.lineWidth
            || original.boxes()[index].lineWidth != 1.5F) {
            fail("3D snapshot stress observed mutable published storage");
        }
    }
}

} // namespace

int main() {
    fuzzUtf8();
    propertyLayoutConstraints();
    propertyBatchCompilation();
    fuzzFiniteGeometryAndArithmetic();
    stressImmutable3dSnapshots();
    henia::gfx::ViewParameters invalidMotionView{.viewport = {1.0F, 1.0F}};
    invalidMotionView.motionScale = std::numeric_limits<float>::quiet_NaN();
    if (henia::gfx::validate(invalidMotionView) != "view.motionScale") {
        fail("Non-finite motion view scale was accepted");
    }
    henia::gfx::BoxInstance invalidMotionBox{};
    invalidMotionBox.setMotionDelta({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F});
    if (henia::gfx::validate(invalidMotionBox) != "box.motionDelta") {
        fail("Non-finite motion payload was accepted");
    }
    std::cout << "HeniaUI validation/property tests passed\n";
    return EXIT_SUCCESS;
}
