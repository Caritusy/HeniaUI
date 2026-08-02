#pragma once

#include "henia/ui/BatchCompiler.h"
#include "henia/ui/Canvas.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace henia::ui {

class Frame final {
public:
    Frame();

    void reserve(
        std::size_t commandCapacity,
        std::size_t batchCapacity,
        CapacityPolicy capacityPolicy = CapacityPolicy::Grow,
        std::size_t snapshotSlots = RenderPacketBuilder::kDefaultSnapshotSlots);
    [[nodiscard]] Canvas& begin() noexcept;
    // Returns a cheap immutable handle. Keep the handle alive for as long as any
    // spans obtained from it are consumed.
    [[nodiscard]] RenderPacket finish();
    // Compiles retained segments without concatenating their commands into the
    // immediate-mode display list.
    [[nodiscard]] RenderPacket finish(std::span<const DisplayListSegment> segments);

    [[nodiscard]] const DisplayList& displayList() const noexcept;
    [[nodiscard]] RenderPacket packet() const noexcept;
    [[nodiscard]] std::size_t snapshotSlotCount() const noexcept;
    [[nodiscard]] std::uint64_t snapshotSlotGrowths() const noexcept;
    [[nodiscard]] std::uint64_t rejectedFrames() const noexcept;
    [[nodiscard]] bool lastBuildPublished() const noexcept;

private:
    DisplayList mDisplayList;
    Canvas mCanvas;
    BatchCompiler mCompiler;
    RenderPacketBuilder mPacketBuilder;
    RenderPacket mPacket;
    bool mRecording = false;
    bool mLastBuildPublished = false;
};

} // namespace henia::ui
