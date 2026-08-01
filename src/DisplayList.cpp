#include "henia/ui/DisplayList.h"

namespace henia::ui {

void DisplayList::reserve(std::size_t commandCapacity) {
    const std::size_t previous = mCommands.capacity();
    mCommands.reserve(commandCapacity);
    if (mCommands.capacity() != previous) {
        ++mCapacityGrowths;
    }
}

void DisplayList::clear() noexcept { mCommands.clear(); }

void DisplayList::append(const DrawCommand& command) {
    const std::size_t previous = mCommands.capacity();
    mCommands.push_back(command);
    if (mCommands.capacity() != previous) {
        ++mCapacityGrowths;
    }
}

std::span<const DrawCommand> DisplayList::commands() const noexcept { return mCommands; }

std::size_t DisplayList::size() const noexcept { return mCommands.size(); }

std::size_t DisplayList::capacity() const noexcept { return mCommands.capacity(); }

std::uint64_t DisplayList::capacityGrowths() const noexcept { return mCapacityGrowths; }

} // namespace henia::ui
