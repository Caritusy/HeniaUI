#include "henia/ui/RenderPacket.h"

#include <atomic>

namespace henia::ui {
namespace {

std::atomic_uint64_t gNextPacketIdentity{1};

} // namespace

RenderPacket::RenderPacket() noexcept
    : mIdentity(gNextPacketIdentity.fetch_add(1, std::memory_order_relaxed)) {}

void RenderPacket::reserve(std::size_t instanceCapacity, std::size_t batchCapacity) {
    const std::size_t previousInstances = mInstances.capacity();
    const std::size_t previousBatches = mBatches.capacity();
    mInstances.reserve(instanceCapacity);
    mBatches.reserve(batchCapacity);
    if (mInstances.capacity() != previousInstances) {
        ++mStatistics.instanceCapacityGrowths;
    }
    if (mBatches.capacity() != previousBatches) {
        ++mStatistics.batchCapacityGrowths;
    }
}

void RenderPacket::clear() noexcept {
    const std::uint64_t instanceGrowths = mStatistics.instanceCapacityGrowths;
    const std::uint64_t batchGrowths = mStatistics.batchCapacityGrowths;
    mInstances.clear();
    mBatches.clear();
    mStatistics = {};
    mStatistics.instanceCapacityGrowths = instanceGrowths;
    mStatistics.batchCapacityGrowths = batchGrowths;
}

std::span<const DrawInstance> RenderPacket::instances() const noexcept { return mInstances; }

std::span<const DrawBatch> RenderPacket::batches() const noexcept { return mBatches; }

const PacketStatistics& RenderPacket::statistics() const noexcept { return mStatistics; }

std::uint64_t RenderPacket::identity() const noexcept { return mIdentity; }

std::uint64_t RenderPacket::revision() const noexcept { return mRevision; }

std::size_t RenderPacket::instanceCapacity() const noexcept { return mInstances.capacity(); }

std::size_t RenderPacket::batchCapacity() const noexcept { return mBatches.capacity(); }

void RenderPacket::appendInstance(const DrawInstance& instance) {
    const std::size_t previous = mInstances.capacity();
    mInstances.push_back(instance);
    if (mInstances.capacity() != previous) {
        ++mStatistics.instanceCapacityGrowths;
    }
}

DrawBatch& RenderPacket::appendBatch(const DrawBatch& batch) {
    const std::size_t previous = mBatches.capacity();
    mBatches.push_back(batch);
    if (mBatches.capacity() != previous) {
        ++mStatistics.batchCapacityGrowths;
    }
    return mBatches.back();
}

} // namespace henia::ui
