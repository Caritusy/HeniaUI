#include "henia/ui/DisplayList.h"

namespace henia::ui {

void DisplayList::reserve(std::size_t commandCapacity, CapacityPolicy capacityPolicyValue) {
    const std::size_t previous = mCommands.capacity();
    mCommands.reserve(commandCapacity);
    mCapacityPolicy = capacityPolicyValue;
    if (mCommands.capacity() != previous) {
        ++mCapacityGrowths;
    }
}

void DisplayList::clear() noexcept {
    mCommands.clear();
    mCommandsValidated = true;
}

bool DisplayList::append(const DrawCommand& command) noexcept {
    if (!appendImpl(command)) return false;
    mCommandsValidated = false;
    return true;
}

bool DisplayList::appendValidated(const DrawCommand& command) noexcept {
    return appendImpl(command);
}

bool DisplayList::appendImpl(const DrawCommand& command) noexcept {
    if (mCapacityPolicy == CapacityPolicy::Fixed && mCommands.size() == mCommands.capacity()) {
        return false;
    }
    const std::size_t previous = mCommands.capacity();
    try {
        mCommands.push_back(command);
    } catch (...) {
        return false;
    }
    if (mCommands.capacity() != previous) {
        ++mCapacityGrowths;
    }
    return true;
}

std::span<const DrawCommand> DisplayList::commands() const noexcept { return mCommands; }

std::size_t DisplayList::size() const noexcept { return mCommands.size(); }

std::size_t DisplayList::capacity() const noexcept { return mCommands.capacity(); }

CapacityPolicy DisplayList::capacityPolicy() const noexcept { return mCapacityPolicy; }

std::uint64_t DisplayList::capacityGrowths() const noexcept { return mCapacityGrowths; }

} // namespace henia::ui
