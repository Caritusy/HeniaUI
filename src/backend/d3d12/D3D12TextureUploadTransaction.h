#pragma once

#include <cstdint>

namespace henia::ui::detail {

enum class D3D12TextureUploadState : std::uint8_t {
    Available,
    Recording,
    Pending,
    Abandoned,
};

// Tracks the ownership boundary around ExecuteCommandLists/Signal. Before a
// successful signal, a failed recording can roll back immediately. After
// execution, resources may be released only when the tracked fence completes;
// if Signal fails, the ownership is deliberately abandoned until shutdown.
class D3D12TextureUploadTransaction final {
public:
    [[nodiscard]] bool begin() noexcept {
        if (mState != D3D12TextureUploadState::Available) {
            return false;
        }
        mState = D3D12TextureUploadState::Recording;
        return true;
    }

    void rollback() noexcept {
        if (mState == D3D12TextureUploadState::Recording) {
            mState = D3D12TextureUploadState::Available;
        }
    }

    [[nodiscard]] bool submit(std::uint64_t fenceValue) noexcept {
        if (mState != D3D12TextureUploadState::Recording || fenceValue == 0) {
            return false;
        }
        mFenceValue = fenceValue;
        mState = D3D12TextureUploadState::Pending;
        return true;
    }

    void abandon() noexcept {
        if (mState == D3D12TextureUploadState::Recording) {
            mState = D3D12TextureUploadState::Abandoned;
        }
    }

    [[nodiscard]] bool completed(std::uint64_t completedFence) const noexcept {
        return mState == D3D12TextureUploadState::Pending && completedFence >= mFenceValue;
    }

    [[nodiscard]] bool release(std::uint64_t completedFence) noexcept {
        if (!completed(completedFence)) {
            return false;
        }
        mFenceValue = 0;
        mState = D3D12TextureUploadState::Available;
        return true;
    }

    [[nodiscard]] D3D12TextureUploadState state() const noexcept { return mState; }
    [[nodiscard]] std::uint64_t fenceValue() const noexcept { return mFenceValue; }
    [[nodiscard]] bool ownsResources() const noexcept {
        return mState != D3D12TextureUploadState::Available;
    }

private:
    D3D12TextureUploadState mState = D3D12TextureUploadState::Available;
    std::uint64_t mFenceValue = 0;
};

} // namespace henia::ui::detail
