#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace henia::detail {

// Backend errors must remain writable even when the operation failed because
// process memory is exhausted. Messages are truncated instead of allocating.
class FixedError final {
public:
    static constexpr std::size_t kCapacity = 1024;

    FixedError& operator=(std::string_view message) noexcept {
        assign(message);
        return *this;
    }

    void assign(std::string_view message) noexcept {
        assign(message.data(), message.size());
    }

    void assign(const char* data, std::size_t size) noexcept {
        mSize = std::min(size, mStorage.size() - 1U);
        if (mSize != 0) {
            std::memcpy(mStorage.data(), data, mSize);
        }
        mStorage[mSize] = '\0';
    }

    void clear() noexcept {
        mSize = 0;
        mStorage[0] = '\0';
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return {mStorage.data(), mSize};
    }

private:
    std::array<char, kCapacity> mStorage{};
    std::size_t mSize = 0;
};

} // namespace henia::detail
