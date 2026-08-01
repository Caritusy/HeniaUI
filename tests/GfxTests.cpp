#include "henia/gfx/ShapeBatch3D.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

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
    shapes.replaceBoxes(boxes);
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

    std::cout << "HeniaUI gfx lifecycle tests passed\n";
    return EXIT_SUCCESS;
}
