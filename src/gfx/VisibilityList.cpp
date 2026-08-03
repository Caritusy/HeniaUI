#include "henia/gfx/VisibilityList.h"

#include "henia/gfx/Validation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace henia::gfx {
namespace {

std::atomic_uint64_t gNextVisibilityIdentity{1};

struct ClipPoint final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

struct Bounds final {
    Vec3 minimum{};
    Vec3 maximum{};
    float maximumLineWidth = 0.0F;
    std::uint32_t visibilityMaskUnion = 0;
};

struct Plane final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;
};

struct Frustum final {
    std::array<Plane, 6> planes{};
    Plane clipW{};
};

[[nodiscard]] Frustum extractFrustum(
    const Mat4& matrix,
    ClipDepthRange depthRange) noexcept {
    const auto& m = matrix.values;
    const Plane row0{m[0], m[4], m[8], m[12]};
    const Plane row1{m[1], m[5], m[9], m[13]};
    const Plane row2{m[2], m[6], m[10], m[14]};
    const Plane row3{m[3], m[7], m[11], m[15]};
    const auto add = [](Plane left, Plane right) noexcept {
        return Plane{left.x + right.x, left.y + right.y,
            left.z + right.z, left.w + right.w};
    };
    const auto subtract = [](Plane left, Plane right) noexcept {
        return Plane{left.x - right.x, left.y - right.y,
            left.z - right.z, left.w - right.w};
    };
    return {
        .planes = {
            add(row3, row0),
            subtract(row3, row0),
            add(row3, row1),
            subtract(row3, row1),
            depthRange == ClipDepthRange::ZeroToOne ? row2 : add(row3, row2),
            subtract(row3, row2),
        },
        .clipW = row3,
    };
}

[[nodiscard]] double maximumDistance(Plane plane, const Bounds& bounds) noexcept {
    return plane.w
        + plane.x * (plane.x >= 0.0 ? bounds.maximum.x : bounds.minimum.x)
        + plane.y * (plane.y >= 0.0 ? bounds.maximum.y : bounds.minimum.y)
        + plane.z * (plane.z >= 0.0 ? bounds.maximum.z : bounds.minimum.z);
}

[[nodiscard]] double minimumDistance(Plane plane, const Bounds& bounds) noexcept {
    return plane.w
        + plane.x * (plane.x >= 0.0 ? bounds.minimum.x : bounds.maximum.x)
        + plane.y * (plane.y >= 0.0 ? bounds.minimum.y : bounds.maximum.y)
        + plane.z * (plane.z >= 0.0 ? bounds.minimum.z : bounds.maximum.z);
}

[[nodiscard]] ClipPoint transform(const Mat4& matrix, Vec3 point) noexcept {
    const auto& m = matrix.values;
    return {
        static_cast<double>(m[0]) * point.x + static_cast<double>(m[4]) * point.y
            + static_cast<double>(m[8]) * point.z + m[12],
        static_cast<double>(m[1]) * point.x + static_cast<double>(m[5]) * point.y
            + static_cast<double>(m[9]) * point.z + m[13],
        static_cast<double>(m[2]) * point.x + static_cast<double>(m[6]) * point.y
            + static_cast<double>(m[10]) * point.z + m[14],
        static_cast<double>(m[3]) * point.x + static_cast<double>(m[7]) * point.y
            + static_cast<double>(m[11]) * point.z + m[15],
    };
}

[[nodiscard]] std::array<ClipPoint, 8> transformedCorners(
    const Bounds& bounds,
    const Mat4& matrix) noexcept {
    std::array<ClipPoint, 8> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = transform(matrix, {
            (index & 1U) == 0 ? bounds.minimum.x : bounds.maximum.x,
            (index & 2U) == 0 ? bounds.minimum.y : bounds.maximum.y,
            (index & 4U) == 0 ? bounds.minimum.z : bounds.maximum.z,
        });
    }
    return result;
}

[[nodiscard]] bool finite(const ClipPoint& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y)
        && std::isfinite(point.z) && std::isfinite(point.w);
}

[[nodiscard]] bool outsideFrustum(
    const Bounds& bounds,
    const Frustum& frustum,
    const ViewParameters& view) noexcept {
    const double xMargin = static_cast<double>(bounds.maximumLineWidth + 2.5F)
        * 2.0 / static_cast<double>(view.viewport.x);
    const double yMargin = static_cast<double>(bounds.maximumLineWidth + 2.5F)
        * 2.0 / static_cast<double>(view.viewport.y);
    const double minimumW = minimumDistance(frustum.clipW, bounds);
    const double maximumW = maximumDistance(frustum.clipW, bounds);
    const double absoluteW = std::max(std::abs(minimumW), std::abs(maximumW));
    for (std::size_t plane = 0; plane < 6; ++plane) {
        const double distance = maximumDistance(frustum.planes[plane], bounds);
        if (!std::isfinite(distance)) return false;
        const double margin = plane < 2 ? xMargin * absoluteW
            : plane < 4 ? yMargin * absoluteW : 0.0;
        if (distance < -margin) return true;
    }
    return false;
}

[[nodiscard]] bool belowProjectedExtent(
    const Bounds& bounds,
    const ViewParameters& view,
    float minimumPixels) noexcept {
    if (minimumPixels <= 0.0F) return false;
    const std::array<ClipPoint, 8> corners = transformedCorners(bounds, view.viewProjection);
    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (const ClipPoint& corner : corners) {
        // Boxes crossing the eye/near plane are retained conservatively.
        if (!finite(corner) || corner.w <= 1.0e-6) return false;
        const double x = corner.x / corner.w;
        const double y = corner.y / corner.w;
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        minimumX = std::min(minimumX, x);
        minimumY = std::min(minimumY, y);
        maximumX = std::max(maximumX, x);
        maximumY = std::max(maximumY, y);
    }
    const double width = (maximumX - minimumX) * 0.5 * view.viewport.x;
    const double height = (maximumY - minimumY) * 0.5 * view.viewport.y;
    return std::max(width, height) < static_cast<double>(minimumPixels);
}

[[nodiscard]] Bounds boxBounds(const BoxInstance& box) noexcept {
    return {box.minimum, box.maximum, box.lineWidth, box.visibilityMask()};
}

[[nodiscard]] Bounds pageBounds(std::span<const BoxInstance> boxes) noexcept {
    Bounds result = boxBounds(boxes.front());
    for (const BoxInstance& box : boxes.subspan(1)) {
        result.minimum.x = std::min(result.minimum.x, box.minimum.x);
        result.minimum.y = std::min(result.minimum.y, box.minimum.y);
        result.minimum.z = std::min(result.minimum.z, box.minimum.z);
        result.maximum.x = std::max(result.maximum.x, box.maximum.x);
        result.maximum.y = std::max(result.maximum.y, box.maximum.y);
        result.maximum.z = std::max(result.maximum.z, box.maximum.z);
        result.maximumLineWidth = std::max(result.maximumLineWidth, box.lineWidth);
        result.visibilityMaskUnion |= box.visibilityMask();
    }
    return result;
}

[[nodiscard]] std::uint64_t elapsedNanoseconds(
    std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

} // namespace

struct VisibilityList::Implementation final {
    std::vector<std::size_t> indices;
    std::vector<BoxInstance> boxes;
    std::vector<Bounds> chunks;
    std::size_t sourceCapacity = 0;
    // Visibility streams occupy a disjoint practical identity namespace so a
    // backend can never mistake a compact stream for its source InstanceBatch.
    std::uint64_t outputIdentity = (std::uint64_t{1} << 63U)
        | gNextVisibilityIdentity.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t outputRevision = 0;
    std::uint64_t sourceIdentity = 0;
    std::uint64_t sourceRevision = 0;
    Mat4 viewProjection{};
    Vec2 viewport{};
    ClipDepthRange clipDepthRange = ClipDepthRange::ZeroToOne;
    float minimumProjectedExtentPixels = 0.0F;
    std::uint32_t applicationVisibilityMask = ~std::uint32_t{0};
    VisibilityStatistics statistics{};
    std::array<char, 128> error{};
    bool hasResult = false;

    void assignError(std::string_view value) noexcept {
        const std::size_t count = std::min(value.size(), error.size() - 1U);
        std::copy_n(value.data(), count, error.data());
        error[count] = '\0';
    }

    void clearError() noexcept { error[0] = '\0'; }
};

VisibilityList::VisibilityList() : mImplementation(std::make_unique<Implementation>()) {}
VisibilityList::~VisibilityList() = default;

bool VisibilityList::reserve(std::size_t sourceCapacity) noexcept {
    if (sourceCapacity <= mImplementation->sourceCapacity) {
        mImplementation->clearError();
        return true;
    }
    const std::size_t chunkCapacity = sourceCapacity / InstanceBatch::kBoxesPerPage
        + (sourceCapacity % InstanceBatch::kBoxesPerPage == 0 ? 0U : 1U);
    try {
        mImplementation->indices.reserve(sourceCapacity);
        mImplementation->boxes.reserve(sourceCapacity);
        mImplementation->chunks.reserve(chunkCapacity);
    } catch (...) {
        mImplementation->assignError("visibility workspace allocation failed");
        return false;
    }
    mImplementation->sourceCapacity = sourceCapacity;
    mImplementation->clearError();
    return true;
}

bool VisibilityList::update(
    const InstanceBatch& batch,
    const ViewParameters& view,
    VisibilityOptions options) noexcept {
    Implementation& state = *mImplementation;
    if (const std::string_view issue = validate(view); !issue.empty()) {
        state.assignError(issue);
        return false;
    }
    if (const std::string_view issue = validate(options); !issue.empty()) {
        state.assignError(issue);
        return false;
    }
    const BoxInstanceView source = batch.boxes();
    if (source.size() > state.sourceCapacity) {
        state.assignError("visibility source count exceeds reserved capacity");
        return false;
    }
    for (const BoxInstance& box : source) {
        if (const std::string_view issue = validate(box); !issue.empty()) {
            state.assignError(issue);
            return false;
        }
    }

    const bool sameResult = state.hasResult
        && state.sourceIdentity == batch.identity()
        && state.sourceRevision == batch.revision()
        && state.viewProjection == view.viewProjection
        && state.viewport == view.viewport
        && state.clipDepthRange == view.clipDepthRange
        && state.applicationVisibilityMask == options.applicationVisibilityMask
        && state.minimumProjectedExtentPixels == options.minimumProjectedExtentPixels;
    if (sameResult) {
        VisibilityStatistics reused = state.statistics;
        reused.sourceInstances = source.size();
        reused.visibleInstances = state.boxes.size();
        reused.rebuiltChunks = 0;
        reused.reusedChunks = batch.boxPageCount();
        reused.cullingNanoseconds = 0;
        reused.resultReused = true;
        state.statistics = reused;
        state.clearError();
        return true;
    }

    const auto started = std::chrono::steady_clock::now();
    VisibilityStatistics statistics{.sourceInstances = source.size()};
    const std::size_t pageCount = batch.boxPageCount();
    const bool reuseAllChunks = state.hasResult
        && state.sourceIdentity == batch.identity()
        && state.sourceRevision == batch.revision()
        && state.chunks.size() == pageCount;
    const bool canIncrement = state.hasResult
        && state.sourceIdentity == batch.identity()
        && state.sourceRevision != std::numeric_limits<std::uint64_t>::max()
        && state.sourceRevision + 1U == batch.revision()
        && state.chunks.size() == pageCount
        && !batch.requiresFullUpload()
        && !batch.dirtyRanges().empty();
    state.chunks.resize(pageCount);
    for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        bool rebuild = !reuseAllChunks && !canIncrement;
        if (canIncrement) {
            const std::size_t pageStart = pageIndex * InstanceBatch::kBoxesPerPage;
            const std::size_t pageEnd = pageStart + batch.boxPage(pageIndex).size();
            for (const DirtyRange range : batch.dirtyRanges()) {
                const std::size_t rangeEnd = range.count > std::numeric_limits<std::size_t>::max() - range.offset
                    ? std::numeric_limits<std::size_t>::max() : range.offset + range.count;
                if (range.offset < pageEnd && rangeEnd > pageStart) {
                    rebuild = true;
                    break;
                }
            }
        }
        if (rebuild) {
            state.chunks[pageIndex] = pageBounds(batch.boxPage(pageIndex));
            ++statistics.rebuiltChunks;
        } else {
            ++statistics.reusedChunks;
        }
    }

    state.indices.clear();
    state.boxes.clear();
    const Frustum frustum = extractFrustum(view.viewProjection, view.clipDepthRange);
    for (std::size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
        const std::span<const BoxInstance> page = batch.boxPage(pageIndex);
        ++statistics.chunkTests;
        if ((state.chunks[pageIndex].visibilityMaskUnion
                & options.applicationVisibilityMask) == 0) {
            statistics.applicationMaskRejectedInstances += page.size();
            statistics.chunkRejectedInstances += page.size();
            continue;
        }
        if (outsideFrustum(state.chunks[pageIndex], frustum, view)) {
            statistics.frustumRejectedInstances += page.size();
            statistics.chunkRejectedInstances += page.size();
            continue;
        }
        for (std::size_t localIndex = 0; localIndex < page.size(); ++localIndex) {
            const BoxInstance& box = page[localIndex];
            if ((box.visibilityMask() & options.applicationVisibilityMask) == 0) {
                ++statistics.applicationMaskRejectedInstances;
                continue;
            }
            const Bounds bounds = boxBounds(box);
            if (outsideFrustum(bounds, frustum, view)) {
                ++statistics.frustumRejectedInstances;
                continue;
            }
            if (belowProjectedExtent(
                    bounds, view, options.minimumProjectedExtentPixels)) {
                ++statistics.projectedSizeRejectedInstances;
                continue;
            }
            state.indices.push_back(pageIndex * InstanceBatch::kBoxesPerPage + localIndex);
            state.boxes.push_back(box);
        }
    }
    statistics.visibleInstances = state.boxes.size();
    statistics.cullingNanoseconds = elapsedNanoseconds(started);
    state.statistics = statistics;
    state.sourceIdentity = batch.identity();
    state.sourceRevision = batch.revision();
    state.viewProjection = view.viewProjection;
    state.viewport = view.viewport;
    state.clipDepthRange = view.clipDepthRange;
    state.applicationVisibilityMask = options.applicationVisibilityMask;
    state.minimumProjectedExtentPixels = options.minimumProjectedExtentPixels;
    state.hasResult = true;
    ++state.outputRevision;
    state.clearError();
    return true;
}

std::span<const std::size_t> VisibilityList::indices() const noexcept {
    return mImplementation->indices;
}

std::span<const BoxInstance> VisibilityList::boxes() const noexcept {
    return mImplementation->boxes;
}

std::size_t VisibilityList::capacity() const noexcept { return mImplementation->sourceCapacity; }

std::size_t VisibilityList::storageBytes() const noexcept {
    return sizeof(Implementation)
        + mImplementation->indices.capacity() * sizeof(std::size_t)
        + mImplementation->boxes.capacity() * sizeof(BoxInstance)
        + mImplementation->chunks.capacity() * sizeof(Bounds);
}

std::uint64_t VisibilityList::identity() const noexcept { return mImplementation->outputIdentity; }
std::uint64_t VisibilityList::revision() const noexcept { return mImplementation->outputRevision; }
VisibilityStatistics VisibilityList::statistics() const noexcept { return mImplementation->statistics; }
std::string_view VisibilityList::lastError() const noexcept { return mImplementation->error.data(); }

} // namespace henia::gfx
