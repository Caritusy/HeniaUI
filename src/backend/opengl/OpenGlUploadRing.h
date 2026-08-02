#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace henia::detail {

enum class UploadFenceStatus : std::uint8_t {
    Signaled,
    Busy,
    Failed,
};

enum class UploadSelectionKind : std::uint8_t {
    Cached,
    Full,
    Partial,
    Exhausted,
};

struct UploadSelection final {
    static constexpr std::size_t kInvalidSlot = std::numeric_limits<std::size_t>::max();

    std::size_t slot = kInvalidSlot;
    UploadSelectionKind kind = UploadSelectionKind::Exhausted;

    [[nodiscard]] bool requiresUpload() const noexcept {
        return kind == UploadSelectionKind::Full || kind == UploadSelectionKind::Partial;
    }
};

// CPU-side state machine shared by the 2D and 3D OpenGL upload paths. The GL
// owner supplies a zero-timeout fence poll; selection never waits or allocates.
class OpenGlUploadRing final {
public:
    void reset(std::size_t slotCount) { mSlots.assign(slotCount, {}); }
    void clear() noexcept { mSlots.clear(); }

    template <typename Poll>
    [[nodiscard]] UploadSelection select(
        std::uint64_t identity,
        std::uint64_t revision,
        bool partialRequested,
        Poll&& poll) noexcept {
        if (identity != 0 && revision != 0) {
            for (std::size_t index = 0; index < mSlots.size(); ++index) {
                const Slot& slot = mSlots[index];
                if (slot.identity == identity && slot.revision == revision) {
                    return {index, UploadSelectionKind::Cached};
                }
            }
        }

        for (std::size_t index = 0; index < mSlots.size(); ++index) {
            Slot& slot = mSlots[index];
            if (!slot.inFlight) {
                continue;
            }
            switch (poll(index)) {
                case UploadFenceStatus::Signaled:
                    slot.inFlight = false;
                    break;
                case UploadFenceStatus::Busy:
                    break;
                case UploadFenceStatus::Failed:
                    slot.inFlight = false;
                    slot.reuseBlocked = true;
                    break;
            }
        }

        if (partialRequested && identity != 0 && revision > 1) {
            for (std::size_t index = 0; index < mSlots.size(); ++index) {
                const Slot& slot = mSlots[index];
                if (writable(slot) && slot.identity == identity
                    && slot.revision == revision - 1) {
                    return {index, UploadSelectionKind::Partial};
                }
            }
        }
        for (std::size_t index = 0; index < mSlots.size(); ++index) {
            if (writable(mSlots[index])) {
                return {index, UploadSelectionKind::Full};
            }
        }
        return {};
    }

    void markUploaded(
        std::size_t index,
        std::uint64_t identity,
        std::uint64_t revision) noexcept {
        Slot& slot = mSlots[index];
        slot.identity = identity;
        slot.revision = revision;
    }

    void invalidate(std::size_t index) noexcept {
        Slot& slot = mSlots[index];
        slot.identity = 0;
        slot.revision = 0;
    }

    void markSubmitted(std::size_t index) noexcept {
        Slot& slot = mSlots[index];
        slot.inFlight = true;
        slot.reuseBlocked = false;
    }

    void markFenceFailed(std::size_t index) noexcept {
        Slot& slot = mSlots[index];
        slot.inFlight = false;
        slot.reuseBlocked = true;
    }

    [[nodiscard]] std::size_t slotCount() const noexcept { return mSlots.size(); }

private:
    struct Slot final {
        std::uint64_t identity = 0;
        std::uint64_t revision = 0;
        bool inFlight = false;
        bool reuseBlocked = false;
    };

    [[nodiscard]] static bool writable(const Slot& slot) noexcept {
        return !slot.inFlight && !slot.reuseBlocked;
    }

    std::vector<Slot> mSlots;
};

} // namespace henia::detail
