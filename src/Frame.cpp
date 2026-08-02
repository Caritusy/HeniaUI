#include "henia/ui/Frame.h"

namespace henia::ui {

Frame::Frame() : mCanvas(mDisplayList) {}

void Frame::reserve(
    std::size_t commandCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy,
    std::size_t snapshotSlots) {
    reserve(
        commandCapacity,
        commandCapacity,
        batchCapacity,
        capacityPolicy,
        snapshotSlots);
}

void Frame::reserve(
    std::size_t commandCapacity,
    std::size_t instanceCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy,
    std::size_t snapshotSlots) {
    mDisplayList.reserve(commandCapacity, capacityPolicy);
    mPacketBuilder.reserve(instanceCapacity, batchCapacity, capacityPolicy, snapshotSlots);
}

Canvas& Frame::begin() noexcept {
    mDisplayList.clear();
    mCanvas.reset();
    mRecording = true;
    return mCanvas;
}

void Frame::setFragmentAreaTracking(bool enabled) noexcept {
    mCompiler.setFragmentAreaTracking(enabled);
}

bool Frame::fragmentAreaTracking() const noexcept {
    return mCompiler.fragmentAreaTracking();
}

RenderPacket Frame::finish() {
    mLastBuildPublished = false;
    if (mRecording) {
        mRecording = false;
        if (mPacketBuilder.begin()) {
            static_cast<void>(mCompiler.compile(mDisplayList, mPacketBuilder));
            mPacket = mPacketBuilder.publish();
            mLastBuildPublished = true;
        }
    }
    return mPacket;
}

RenderPacket Frame::finish(std::span<const DisplayListSegment> segments) {
    mLastBuildPublished = false;
    mRecording = false;
    if (mPacketBuilder.begin()) {
        static_cast<void>(mCompiler.compile(segments, mPacketBuilder));
        mPacket = mPacketBuilder.publish();
        mLastBuildPublished = true;
    }
    return mPacket;
}

const DisplayList& Frame::displayList() const noexcept { return mDisplayList; }

RenderPacket Frame::packet() const noexcept { return mPacket; }

std::size_t Frame::snapshotSlotCount() const noexcept {
    return mPacketBuilder.snapshotSlotCount();
}

std::uint64_t Frame::snapshotSlotGrowths() const noexcept {
    return mPacketBuilder.snapshotSlotGrowths();
}

std::uint64_t Frame::rejectedFrames() const noexcept {
    return mPacketBuilder.rejectedBuilds();
}

bool Frame::lastBuildPublished() const noexcept { return mLastBuildPublished; }

} // namespace henia::ui
