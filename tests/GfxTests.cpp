#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/Math.h"
#include "henia/gfx/Validation.h"
#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

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
        || first.dirtyCount() != boxes.size() || first.revision() == 0
        || !first.requiresFullUpload() || first.copiedBoxCount() != boxes.size()) {
        fail("Initial instance snapshot is incorrect");
    }

    const InstanceBatch stable = shapes.snapshot();
    if (stable.revision() != first.revision() || stable.dirtyCount() != 0
        || stable.copiedBoxCount() != 0 || stable.requiresFullUpload()
        || stable.boxPage(0).data() != first.boxPage(0).data()) {
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
        || partial.boxes()[137].lineWidth != 3.0F
        || partial.dirtyRanges().size() != 1
        || partial.copiedBoxCount() != InstanceBatch::kBoxesPerPage
        || partial.boxPage(0).data() == first.boxPage(0).data()
        || partial.boxPage(1).data() != first.boxPage(1).data()) {
        fail("Incremental immutable instance update is incorrect");
    }

    constexpr std::size_t largeBoxCount = 100000;
    std::vector<BoxInstance> largeBoxes(largeBoxCount);
    ShapeBatch3D pagedShapes;
    pagedShapes.reserve(largeBoxes.size());
    if (!pagedShapes.replaceBoxes(largeBoxes)) fail("Large paged batch was rejected");
    const InstanceBatch largeOriginal = pagedShapes.snapshot();
    BoxInstance singleChanged = largeOriginal.boxes()[50000];
    singleChanged.hueOffset = 1.0F;
    if (!pagedShapes.updateBox(50000, singleChanged)) fail("Large single update failed");
    const InstanceBatch largeSingle = pagedShapes.snapshot();
    if (largeSingle.copiedBoxCount() != InstanceBatch::kBoxesPerPage
        || largeSingle.dirtyRanges().size() != 1
        || largeSingle.dirtyRanges().front() != DirtyRange{50000, 1}
        || largeOriginal.boxes()[50000].hueOffset != 0.0F
        || largeSingle.boxes()[50000].hueOffset != 1.0F) {
        fail("A 100k-box single edit copied more than one page or mutated its old snapshot");
    }

    for (std::size_t index = 60000; index < 60032; ++index) {
        BoxInstance clustered = largeSingle.boxes()[index];
        clustered.hueOffset = 2.0F;
        if (!pagedShapes.updateBox(index, clustered)) fail("Clustered paged update failed");
    }
    const InstanceBatch clustered = pagedShapes.snapshot();
    if (clustered.copiedBoxCount() != InstanceBatch::kBoxesPerPage
        || clustered.dirtyRanges().size() != 1
        || clustered.dirtyRanges().front() != DirtyRange{60000, 32}) {
        fail("Clustered edits did not coalesce into one page copy and one dirty range");
    }

    constexpr std::array sparseIndices{1U, 25000U, 75000U, 99998U};
    for (const std::size_t index : sparseIndices) {
        BoxInstance sparse = clustered.boxes()[index];
        sparse.hueOffset = 3.0F;
        if (!pagedShapes.updateBox(index, sparse)) fail("Sparse paged update failed");
    }
    const InstanceBatch sparse = pagedShapes.snapshot();
    if (sparse.copiedBoxCount() != sparseIndices.size() * InstanceBatch::kBoxesPerPage
        || sparse.dirtyRanges().size() != sparseIndices.size()
        || sparse.dirtyCount() < 99998) {
        fail("Sparse edits were collapsed or copied outside their changed pages");
    }
    for (std::size_t index = 0; index < sparseIndices.size(); ++index) {
        if (sparse.dirtyRanges()[index] != DirtyRange{sparseIndices[index], 1}) {
            fail("Sparse dirty ranges were not preserved in index order");
        }
    }
    const InstanceBatch largeStable = pagedShapes.snapshot();
    if (!largeStable.dirtyRanges().empty() || largeStable.copiedBoxCount() != 0
        || largeStable.boxPage(100).data() != sparse.boxPage(100).data()) {
        fail("Stable paged snapshot rebuilt or copied immutable storage");
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
