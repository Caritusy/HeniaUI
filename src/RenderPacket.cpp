#include "henia/ui/RenderPacket.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace henia::ui {
namespace {

std::atomic_uint64_t gNextPacketIdentity{1};

[[nodiscard]] std::uint64_t nextPacketIdentity() noexcept {
    std::uint64_t identity = gNextPacketIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0) {
        identity = gNextPacketIdentity.fetch_add(1, std::memory_order_relaxed);
    }
    return identity;
}

} // namespace

namespace detail {

struct RenderPacketStorage final {
    std::atomic_uint32_t readers{0};
    std::vector<DrawInstance> instances;
    std::vector<DrawBatch> batches;
    PacketStatistics statistics{};
    CapacityPolicy capacityPolicy = CapacityPolicy::Grow;
    std::uint64_t identity = nextPacketIdentity();
    std::uint64_t revision = 0;
};

struct RenderPacketPool final {
    std::vector<std::unique_ptr<RenderPacketStorage>> slots;
    std::size_t instanceCapacity = 0;
    std::size_t batchCapacity = 0;
    CapacityPolicy capacityPolicy = CapacityPolicy::Grow;
    std::uint64_t nextRevision = 1;
    std::uint64_t slotGrowths = 0;
    std::uint64_t rejectedBuilds = 0;
};

} // namespace detail

namespace {

void reserveStorage(
    detail::RenderPacketStorage& storage,
    std::size_t instanceCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy) {
    const std::size_t previousInstances = storage.instances.capacity();
    const std::size_t previousBatches = storage.batches.capacity();
    storage.instances.reserve(instanceCapacity);
    storage.batches.reserve(batchCapacity);
    storage.capacityPolicy = capacityPolicy;
    if (storage.instances.capacity() != previousInstances) {
        ++storage.statistics.instanceCapacityGrowths;
    }
    if (storage.batches.capacity() != previousBatches) {
        ++storage.statistics.batchCapacityGrowths;
    }
}

[[nodiscard]] std::unique_ptr<detail::RenderPacketStorage> makeStorage(
    const detail::RenderPacketPool& pool) {
    auto storage = std::make_unique<detail::RenderPacketStorage>();
    reserveStorage(
        *storage,
        pool.instanceCapacity,
        pool.batchCapacity,
        pool.capacityPolicy);
    return storage;
}

} // namespace

RenderPacket::RenderPacket(
    std::shared_ptr<detail::RenderPacketPool> pool,
    detail::RenderPacketStorage* storage) noexcept
    : mPool(std::move(pool)),
      mStorage(storage) {
    retain();
}

RenderPacket::~RenderPacket() { release(); }

RenderPacket::RenderPacket(const RenderPacket& other) noexcept
    : mPool(other.mPool),
      mStorage(other.mStorage) {
    retain();
}

RenderPacket& RenderPacket::operator=(const RenderPacket& other) noexcept {
    if (this == &other) {
        return *this;
    }
    std::shared_ptr<detail::RenderPacketPool> pool = other.mPool;
    detail::RenderPacketStorage* storage = other.mStorage;
    if (storage != nullptr) {
        storage->readers.fetch_add(1, std::memory_order_relaxed);
    }
    release();
    mPool = std::move(pool);
    mStorage = storage;
    return *this;
}

RenderPacket::RenderPacket(RenderPacket&& other) noexcept
    : mPool(std::move(other.mPool)),
      mStorage(std::exchange(other.mStorage, nullptr)) {}

RenderPacket& RenderPacket::operator=(RenderPacket&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    release();
    mPool = std::move(other.mPool);
    mStorage = std::exchange(other.mStorage, nullptr);
    return *this;
}

std::span<const DrawInstance> RenderPacket::instances() const noexcept {
    if (mStorage == nullptr) {
        return {};
    }
    return mStorage->instances;
}

std::span<const DrawBatch> RenderPacket::batches() const noexcept {
    if (mStorage == nullptr) {
        return {};
    }
    return mStorage->batches;
}

const PacketStatistics& RenderPacket::statistics() const noexcept {
    static constexpr PacketStatistics empty{};
    return mStorage == nullptr ? empty : mStorage->statistics;
}

std::uint64_t RenderPacket::identity() const noexcept {
    return mStorage == nullptr ? 0 : mStorage->identity;
}

std::uint64_t RenderPacket::revision() const noexcept {
    return mStorage == nullptr ? 0 : mStorage->revision;
}

std::size_t RenderPacket::instanceCapacity() const noexcept {
    return mStorage == nullptr ? 0 : mStorage->instances.capacity();
}

std::size_t RenderPacket::batchCapacity() const noexcept {
    return mStorage == nullptr ? 0 : mStorage->batches.capacity();
}

CapacityPolicy RenderPacket::capacityPolicy() const noexcept {
    return mStorage == nullptr ? CapacityPolicy::Grow : mStorage->capacityPolicy;
}

RenderPacket::operator bool() const noexcept { return mStorage != nullptr; }

void RenderPacket::retain() noexcept {
    if (mStorage != nullptr) {
        mStorage->readers.fetch_add(1, std::memory_order_relaxed);
    }
}

void RenderPacket::release() noexcept {
    if (mStorage == nullptr) {
        return;
    }
    const std::uint32_t previous = mStorage->readers.fetch_sub(1, std::memory_order_release);
    assert(previous != 0);
    static_cast<void>(previous);
    mStorage = nullptr;
}

RenderPacketBuilder::RenderPacketBuilder()
    : mPool(std::make_shared<detail::RenderPacketPool>()) {}

RenderPacketBuilder::~RenderPacketBuilder() = default;

void RenderPacketBuilder::reserve(
    std::size_t instanceCapacity,
    std::size_t batchCapacity,
    CapacityPolicy capacityPolicy,
    std::size_t snapshotSlots) {
    assert(mStorage == nullptr);
    if (mStorage != nullptr) {
        return;
    }
    const std::size_t requestedSlots = std::max<std::size_t>(2, snapshotSlots);
    mPool->instanceCapacity = instanceCapacity;
    mPool->batchCapacity = batchCapacity;
    mPool->capacityPolicy = capacityPolicy;
    mPool->slots.reserve(requestedSlots);
    while (mPool->slots.size() < requestedSlots) {
        mPool->slots.push_back(makeStorage(*mPool));
    }
    for (const std::unique_ptr<detail::RenderPacketStorage>& storage : mPool->slots) {
        if (storage->readers.load(std::memory_order_acquire) == 0) {
            reserveStorage(*storage, instanceCapacity, batchCapacity, capacityPolicy);
        }
    }
}

bool RenderPacketBuilder::begin() noexcept {
    if (mStorage != nullptr) {
        return false;
    }

    if (mPool->slots.empty()) {
        try {
            mPool->slots.reserve(kDefaultSnapshotSlots);
            while (mPool->slots.size() < kDefaultSnapshotSlots) {
                mPool->slots.push_back(makeStorage(*mPool));
            }
        } catch (...) {
            ++mPool->rejectedBuilds;
            return false;
        }
    }

    for (const std::unique_ptr<detail::RenderPacketStorage>& storage : mPool->slots) {
        if (storage->readers.load(std::memory_order_acquire) != 0) {
            continue;
        }
        try {
            reserveStorage(
                *storage,
                mPool->instanceCapacity,
                mPool->batchCapacity,
                mPool->capacityPolicy);
        } catch (...) {
            ++mPool->rejectedBuilds;
            return false;
        }
        mStorage = storage.get();
        clear();
        return true;
    }

    if (mPool->capacityPolicy == CapacityPolicy::Fixed) {
        ++mPool->rejectedBuilds;
        return false;
    }
    try {
        mPool->slots.push_back(makeStorage(*mPool));
    } catch (...) {
        ++mPool->rejectedBuilds;
        return false;
    }
    ++mPool->slotGrowths;
    mStorage = mPool->slots.back().get();
    clear();
    return true;
}

RenderPacket RenderPacketBuilder::publish() noexcept {
    if (mStorage == nullptr) {
        return {};
    }
    mStorage->revision = mPool->nextRevision++;
    if (mStorage->revision == 0) {
        mStorage->revision = mPool->nextRevision++;
    }
    RenderPacket packet(mPool, mStorage);
    mStorage = nullptr;
    return packet;
}

std::size_t RenderPacketBuilder::snapshotSlotCount() const noexcept {
    return mPool->slots.size();
}

std::uint64_t RenderPacketBuilder::snapshotSlotGrowths() const noexcept {
    return mPool->slotGrowths;
}

std::uint64_t RenderPacketBuilder::rejectedBuilds() const noexcept {
    return mPool->rejectedBuilds;
}

bool RenderPacketBuilder::active() const noexcept { return mStorage != nullptr; }

void RenderPacketBuilder::clear() noexcept {
    assert(mStorage != nullptr);
    const std::uint64_t instanceGrowths = mStorage->statistics.instanceCapacityGrowths;
    const std::uint64_t batchGrowths = mStorage->statistics.batchCapacityGrowths;
    mStorage->instances.clear();
    mStorage->batches.clear();
    mStorage->statistics = {};
    mStorage->statistics.instanceCapacityGrowths = instanceGrowths;
    mStorage->statistics.batchCapacityGrowths = batchGrowths;
}

DrawBatch* RenderPacketBuilder::lastBatch() noexcept {
    assert(mStorage != nullptr);
    return mStorage->batches.empty() ? nullptr : &mStorage->batches.back();
}

std::size_t RenderPacketBuilder::instanceCount() const noexcept {
    assert(mStorage != nullptr);
    return mStorage->instances.size();
}

bool RenderPacketBuilder::appendInstance(const DrawInstance& instance) noexcept {
    assert(mStorage != nullptr);
    if (mStorage->capacityPolicy == CapacityPolicy::Fixed
        && mStorage->instances.size() == mStorage->instances.capacity()) {
        return false;
    }
    const std::size_t previous = mStorage->instances.capacity();
    try {
        mStorage->instances.push_back(instance);
    } catch (...) {
        return false;
    }
    if (mStorage->instances.capacity() != previous) {
        ++mStorage->statistics.instanceCapacityGrowths;
    }
    return true;
}

DrawBatch* RenderPacketBuilder::appendBatch(const DrawBatch& batch) noexcept {
    assert(mStorage != nullptr);
    if (mStorage->capacityPolicy == CapacityPolicy::Fixed
        && mStorage->batches.size() == mStorage->batches.capacity()) {
        return nullptr;
    }
    const std::size_t previous = mStorage->batches.capacity();
    try {
        mStorage->batches.push_back(batch);
    } catch (...) {
        return nullptr;
    }
    if (mStorage->batches.capacity() != previous) {
        ++mStorage->statistics.batchCapacityGrowths;
    }
    return &mStorage->batches.back();
}

void RenderPacketBuilder::addEstimatedFragmentArea(std::uint64_t area) noexcept {
    assert(mStorage != nullptr);
    const std::uint64_t current = mStorage->statistics.estimatedFragmentArea;
    mStorage->statistics.estimatedFragmentArea =
        area > std::numeric_limits<std::uint64_t>::max() - current
        ? std::numeric_limits<std::uint64_t>::max()
        : current + area;
}

void RenderPacketBuilder::setSourceCommands(std::size_t count) noexcept {
    assert(mStorage != nullptr);
    mStorage->statistics.sourceCommands = count;
}

bool RenderPacketBuilder::rejectPacket(bool invalidInput) noexcept {
    assert(mStorage != nullptr);
    const std::uint64_t sourceCommands = mStorage->statistics.sourceCommands;
    clear();
    mStorage->statistics.sourceCommands = sourceCommands;
    mStorage->statistics.rejectedCommands = sourceCommands;
    if (invalidInput) {
        mStorage->statistics.invalidInputCommands = 1;
    } else {
        mStorage->statistics.capacityRejectedCommands = sourceCommands;
    }
    return false;
}

void RenderPacketBuilder::completePacket() noexcept {
    assert(mStorage != nullptr);
    mStorage->statistics.instances = mStorage->instances.size();
    mStorage->statistics.batches = mStorage->batches.size();
    mStorage->statistics.mergedCommands =
        mStorage->statistics.instances > mStorage->statistics.batches
        ? mStorage->statistics.instances - mStorage->statistics.batches
        : 0;
}

} // namespace henia::ui
