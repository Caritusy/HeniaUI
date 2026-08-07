#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/Math.h"
#include "henia/gfx/Validation.h"
#include "henia/gfx/VisibilityList.h"
#include "henia/gfx/backend/opengl/OpenGlRenderDevice.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

namespace {

static_assert(!std::is_move_constructible_v<henia::gfx::OpenGlRenderDevice>);
static_assert(!std::is_move_assignable_v<henia::gfx::OpenGlRenderDevice>);
static_assert(sizeof(henia::gfx::BoxInstance) == 64);

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using namespace henia::gfx;

    BoxInstance compatibilityBox;
    if (compatibilityBox.visibilityMask() != std::numeric_limits<std::uint32_t>::max()) {
        fail("Zeroed legacy BoxInstance reserved words were not visibility-compatible");
    }
    compatibilityBox.setVisibilityMask(0);
    if (compatibilityBox.visibilityMask() != 0) {
        fail("An explicit zero BoxInstance visibility mask was not retained");
    }
    compatibilityBox.clearVisibilityMask();
    if (compatibilityBox.visibilityMask() != std::numeric_limits<std::uint32_t>::max()) {
        fail("Clearing a BoxInstance visibility mask did not restore compatibility visibility");
    }
    compatibilityBox.setMotionDelta({1.25F, -2.5F, 3.75F});
    if (compatibilityBox.motionDelta() != Vec3{1.25F, -2.5F, 3.75F}
        || compatibilityBox.visibilityMask() != std::numeric_limits<std::uint32_t>::max()
        || (static_cast<std::uint32_t>(compatibilityBox.effects)
            & static_cast<std::uint32_t>(BoxEffect::MotionTranslation)) == 0U) {
        fail("BoxInstance motion payload was not round-tripped");
    }
    compatibilityBox.setVisibilityMask(7U);
    if (compatibilityBox.motionDelta() != Vec3{1.25F, -2.5F, 3.75F}
        || compatibilityBox.visibilityMask() != 7U) {
        fail("BoxInstance visibility mask altered motion payload");
    }
    compatibilityBox.setMotionDelta({-4.0F, 5.0F, -6.0F});
    if (compatibilityBox.visibilityMask() != 7U
        || compatibilityBox.motionDelta() != Vec3{-4.0F, 5.0F, -6.0F}) {
        fail("BoxInstance motion update altered visibility payload");
    }
    compatibilityBox.clearMotionDelta();
    if (compatibilityBox.visibilityMask() != 7U || compatibilityBox.motionDelta() != Vec3{}) {
        fail("Clearing BoxInstance motion altered visibility payload");
    }
    compatibilityBox.clearVisibilityMask();
    compatibilityBox.setMotionDelta({1.25F, -2.5F, 3.75F});
    compatibilityBox.setVisibilityMask(0U);
    const BoxInstance copiedCompatibilityBox = compatibilityBox;
    if (copiedCompatibilityBox.visibilityMask() != 0U
        || copiedCompatibilityBox.motionDelta() != Vec3{1.25F, -2.5F, 3.75F}) {
        fail("BoxInstance copy lost combined visibility and motion state");
    }
    BoxInstance legacyMotion;
    legacyMotion.effects = BoxEffect::MotionTranslation;
    legacyMotion.reserved = {
        std::bit_cast<std::uint32_t>(1.25F),
        std::bit_cast<std::uint32_t>(-2.5F),
        std::bit_cast<std::uint32_t>(3.75F),
    };
    legacyMotion.clearVisibilityMask();
    if (legacyMotion.visibilityMask() != std::numeric_limits<std::uint32_t>::max()
        || legacyMotion.motionDelta() != Vec3{1.25F, -2.5F, 3.75F}) {
        fail("Clearing visibility corrupted a legacy motion payload");
    }
    BoxInstance filledBox;
    filledBox.setFillOpacity(0.25F);
    filledBox.setOutlineEnabled(false);
    if (!filledBox.fillEnabled() || filledBox.outlineEnabled()
        || filledBox.fillOpacity() < 0.245F || filledBox.fillOpacity() > 0.255F
        || !validate(filledBox).empty()) {
        fail("BoxInstance fill and outline state was not encoded in the stable layout");
    }
    filledBox.setOutlineEnabled(true);
    filledBox.clearFill();
    if (filledBox.fillEnabled() || filledBox.fillOpacity() != 0.0F
        || !filledBox.outlineEnabled() || !validate(filledBox).empty()) {
        fail("BoxInstance fill state did not clear without changing outline state");
    }

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

    ShapeBatch3D visibilityShapes;
    std::array<BoxInstance, 260> visibilityBoxes{};
    for (BoxInstance& box : visibilityBoxes) {
        box.minimum = {4.0F, 4.0F, 0.25F};
        box.maximum = {4.25F, 4.25F, 0.5F};
    }
    visibilityBoxes[0] = {
        .minimum = {-0.5F, -0.5F, 0.25F},
        .maximum = {-0.25F, -0.25F, 0.5F},
        .hueOffset = 0.375F,
        .effects = BoxEffect::HueCycle,
    };
    visibilityBoxes[0].setVisibilityMask(1U);
    visibilityBoxes[257] = {
        .minimum = {0.25F, 0.25F, 0.25F},
        .maximum = {0.5F, 0.5F, 0.5F},
        .color = {0.2F, 0.3F, 0.4F, 0.5F},
    };
    visibilityBoxes[257].setVisibilityMask(2U);
    if (!visibilityShapes.replaceBoxes(visibilityBoxes)) {
        fail("Visibility fixture boxes were rejected");
    }
    InstanceBatch visibilityBatch = visibilityShapes.snapshot();
    VisibilityList visibility;
    if (!visibility.reserve(visibilityBoxes.size())
        || !visibility.update(visibilityBatch, ViewParameters{.viewport = {100.0F, 100.0F}})) {
        fail("CPU visibility list rejected a valid batch");
    }
    const VisibilityStatistics firstVisibility = visibility.statistics();
    if (visibility.indices().size() != 2 || visibility.indices()[0] != 0
        || visibility.indices()[1] != 257 || visibility.boxes()[0] != visibilityBoxes[0]
        || visibility.boxes()[1] != visibilityBoxes[257]
        || firstVisibility.sourceInstances != visibilityBoxes.size()
        || firstVisibility.visibleInstances != 2
        || firstVisibility.frustumRejectedInstances != visibilityBoxes.size() - 2
        || firstVisibility.rebuiltChunks != 2 || firstVisibility.resultReused
        || visibility.identity() == 0 || visibility.identity() == visibilityBatch.identity()
        || visibility.revision() == 0) {
        fail("CPU visibility compaction lost order, identity, or effect parameters");
    }
    const std::uint64_t visibilityRevision = visibility.revision();
    ViewParameters timeOnlyView{.viewport = {100.0F, 100.0F}, .timeSeconds = 42.0F};
    if (!visibility.update(visibilityBatch, timeOnlyView)
        || !visibility.statistics().resultReused
        || visibility.revision() != visibilityRevision) {
        fail("Animation time unnecessarily invalidated CPU visibility");
    }
    ViewParameters movedVisibilityView = timeOnlyView;
    movedVisibilityView.viewProjection.values[12] = 0.1F;
    if (!visibility.update(visibilityBatch, movedVisibilityView)
        || visibility.statistics().reusedChunks != 2
        || visibility.statistics().rebuiltChunks != 0
        || visibility.revision() != visibilityRevision + 1) {
        fail("Camera movement rebuilt static visibility chunks");
    }
    if (!visibility.update(
            visibilityBatch,
            movedVisibilityView,
            {.mode = VisibilityMode::CpuFrustum, .applicationVisibilityMask = 1U})
        || visibility.indices().size() != 1 || visibility.indices()[0] != 0
        || visibility.statistics().applicationMaskRejectedInstances != 1
        || visibility.statistics().reusedChunks != 2) {
        fail("Application visibility masks lost stable indices or rebuilt static chunks");
    }

    BoxInstance hidden = visibilityBoxes[257];
    hidden.minimum = {5.0F, 5.0F, 0.25F};
    hidden.maximum = {5.25F, 5.25F, 0.5F};
    if (!visibilityShapes.updateBox(257, hidden)) {
        fail("Visibility incremental fixture update failed");
    }
    visibilityBatch = visibilityShapes.snapshot();
    if (!visibility.update(visibilityBatch, ViewParameters{.viewport = {100.0F, 100.0F}})
        || visibility.indices().size() != 1 || visibility.indices()[0] != 0
        || visibility.statistics().rebuiltChunks != 1
        || visibility.statistics().reusedChunks != 1) {
        fail("Visibility chunks did not follow immutable dirty pages");
    }

    ShapeBatch3D projectedShapes;
    const std::array projectedBoxes{
        BoxInstance{
            .minimum = {-0.005F, -0.005F, 0.25F},
            .maximum = {0.005F, 0.005F, 0.5F},
        },
        BoxInstance{
            .minimum = {-0.5F, -0.5F, 0.25F},
            .maximum = {0.5F, 0.5F, 0.5F},
        },
    };
    if (!projectedShapes.replaceBoxes(projectedBoxes)) {
        fail("Projected-size fixture boxes were rejected");
    }
    VisibilityList projectedVisibility;
    if (!projectedVisibility.reserve(projectedBoxes.size())
        || !projectedVisibility.update(
            projectedShapes.snapshot(),
            ViewParameters{.viewport = {100.0F, 100.0F}},
            {.mode = VisibilityMode::CpuFrustum, .minimumProjectedExtentPixels = 2.0F})
        || projectedVisibility.indices().size() != 1
        || projectedVisibility.indices()[0] != 1
        || projectedVisibility.statistics().projectedSizeRejectedInstances != 1) {
        fail("Projected-size visibility filtering is incorrect");
    }
    VisibilityList undersizedVisibility;
    if (undersizedVisibility.reserve(1)
        && undersizedVisibility.update(projectedShapes.snapshot(), ViewParameters{.viewport = {1.0F, 1.0F}})) {
        fail("Visibility workspace accepted a source beyond its reserved capacity");
    }
    if (!usesCpuVisibility({.mode = VisibilityMode::Automatic, .automaticThreshold = 2}, 2)
        || usesCpuVisibility({}, 100000)) {
        fail("Automatic or default direct visibility selection is incorrect");
    }

    std::vector<BoxInstance> animatedBoxes(InstanceBatch::kBoxesPerPage * 2U);
    for (std::size_t index = 0; index < animatedBoxes.size(); ++index) {
        BoxInstance& box = animatedBoxes[index];
        const bool culledPage = index < InstanceBatch::kBoxesPerPage;
        box.minimum = culledPage
            ? Vec3{4.0F, 4.0F, 0.25F} : Vec3{-0.25F, -0.25F, 0.25F};
        box.maximum = culledPage
            ? Vec3{4.25F, 4.25F, 0.5F} : Vec3{0.25F, 0.25F, 0.5F};
        const float direction = (index & 1U) == 0U ? -1.0F : 1.0F;
        box.setMotionDelta({direction * (culledPage ? 0.5F : 0.1F), 0.0F, 0.0F});
    }
    ShapeBatch3D animatedShapes;
    if (!animatedShapes.replaceBoxes(animatedBoxes)) {
        fail("Animated visibility fixture was rejected");
    }
    VisibilityList animatedVisibility;
    if (!animatedVisibility.reserve(animatedBoxes.size())) {
        fail("Animated visibility workspace could not reserve");
    }
    ViewParameters animatedView{.viewport = {100.0F, 100.0F}, .motionScale = 0.0F};
    const InstanceBatch animatedBatch = animatedShapes.snapshot();
    if (!animatedVisibility.update(animatedBatch, animatedView)
        || animatedVisibility.statistics().rebuiltChunks != 2U) {
        fail("Animated visibility envelopes were not built once");
    }
    for (const float scale : {1.0F, -1.0F}) {
        animatedView.motionScale = scale;
        if (!animatedVisibility.update(animatedBatch, animatedView)
            || animatedVisibility.statistics().rebuiltChunks != 0U
            || animatedVisibility.statistics().reusedChunks != 2U
            || animatedVisibility.statistics().pageEnvelopeEvaluations != 2U
            || animatedVisibility.statistics().exactInstanceTests
                != InstanceBatch::kBoxesPerPage
            || animatedVisibility.indices().size() != InstanceBatch::kBoxesPerPage) {
            fail("motionScale change rescanned source pages or produced incorrect visibility");
        }
    }

    std::cout << "HeniaUI gfx lifecycle tests passed\n";
    return EXIT_SUCCESS;
}
