#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/Math.h"
#include "henia/gfx/Validation.h"
#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

static_assert(!std::is_move_constructible_v<henia::gfx::OpenGlRenderDevice>);
static_assert(!std::is_move_assignable_v<henia::gfx::OpenGlRenderDevice>);

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using namespace henia::gfx;

    ShapeBatch3D shapes;
    shapes.reserve(1024);
    std::array<BoxInstance, 512> boxes{};
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        const float x = static_cast<float>(index % 32U);
        const float y = static_cast<float>(index / 32U);
        boxes[index] = {
            .minimum = {x, y, 0.0F},
            .lineWidth = 2.0F,
            .maximum = {x + 0.8F, y + 0.8F, 0.8F},
            .hueOffset = static_cast<float>(index) / static_cast<float>(boxes.size()),
            .color = {0.1F, 0.7F, 0.9F, 0.9F},
            .effects = BoxEffect::HueCycle,
        };
    }
    if (!shapes.replaceBoxes(boxes)) {
        fail("Valid boxes were rejected");
    }
    const InstanceBatch first = shapes.snapshot();
    if (first.boxes().size() != boxes.size() || first.dirtyOffset() != 0
        || first.dirtyCount() != boxes.size() || first.revision() == 0) {
        fail("Initial instance snapshot is incorrect");
    }

    const InstanceBatch stable = shapes.snapshot();
    if (stable.revision() != first.revision() || stable.dirtyCount() != 0
        || stable.boxes().data() != first.boxes().data()) {
        fail("Stable instances were rebuilt without a content change");
    }

    ViewParameters view{};
    view.timeSeconds = 1.0F;
    view.viewProjection.values[12] = 3.0F;
    const InstanceBatch cameraMoved = shapes.snapshot();
    view.timeSeconds = 2.0F;
    view.viewProjection.values[12] = 4.0F;
    if (cameraMoved.revision() != first.revision() || cameraMoved.dirtyCount() != 0) {
        fail("View or animation time invalidated static instance data");
    }

    BoxInstance changed = boxes[137];
    changed.lineWidth = 3.0F;
    if (!shapes.updateBox(137, changed)) {
        fail("A changed instance was not accepted");
    }
    const InstanceBatch partial = shapes.snapshot();
    if (partial.revision() != first.revision() + 1 || partial.dirtyOffset() != 137
        || partial.dirtyCount() != 1 || first.boxes()[137].lineWidth != 2.0F
        || partial.boxes()[137].lineWidth != 3.0F) {
        fail("Incremental immutable instance update is incorrect");
    }

    const float nan = std::numeric_limits<float>::quiet_NaN();
    BoxInstance invalid = boxes[0];
    invalid.minimum.x = nan;
    if (shapes.addBox(invalid) != ShapeBatch3D::kInvalidIndex
        || shapes.lastError() != "box.minimum.x" || shapes.size() != boxes.size()) {
        fail("Non-finite box input was not rejected with an exact field");
    }
    invalid = boxes[0];
    invalid.minimum.y = invalid.maximum.y + 1.0F;
    if (shapes.updateBox(0, invalid)
        || shapes.lastError() != "box.minimum.y > box.maximum.y") {
        fail("Inverted box input was not rejected deterministically");
    }
    std::array<BoxInstance, 2> replacement{boxes[0], invalid};
    if (shapes.replaceBoxes(replacement) || shapes.size() != boxes.size()
        || shapes.rejectedBoxChanges() != 3) {
        fail("Invalid replacement mutated the 3D batch or its rejection statistics");
    }
    if (shapes.setDepthState({.compare = static_cast<CompareOp>(0xFF)})
        || shapes.lastError() != "depth.compare") {
        fail("Invalid depth comparison was accepted");
    }

    Mat4 matrix{};
    if (tryPerspective(0.0F, 1.0F, 0.1F, 100.0F, matrix) || !finite(matrix)
        || tryPerspective(1.0F, 1.0F, 1.0F, 1.0F, matrix)) {
        fail("Invalid perspective parameters were not rejected to a finite identity");
    }
    if (!tryPerspective(1.0F, 16.0F / 9.0F, 0.1F, 100.0F, matrix) || !finite(matrix)) {
        fail("Valid perspective parameters were rejected");
    }
    if (tryLookAt({}, {}, {0.0F, 1.0F, 0.0F}, matrix) || !finite(matrix)
        || tryLookAt({}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 2.0F}, matrix)) {
        fail("Degenerate lookAt parameters were not rejected to a finite identity");
    }
    if (!tryLookAt({0.0F, 0.0F, -2.0F}, {}, {0.0F, 1.0F, 0.0F}, matrix)
        || !finite(matrix)) {
        fail("Valid lookAt parameters were rejected");
    }

    std::cout << "HeniaUI gfx lifecycle tests passed\n";
    return EXIT_SUCCESS;
}
