#include "OpenGlUploadRing.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using henia::detail::OpenGlUploadRing;
using henia::detail::UploadFenceStatus;
using henia::detail::UploadSelectionKind;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void testCachedReadsAndPartialReuse() {
    OpenGlUploadRing ring;
    ring.reset(3);
    std::array<int, 3> delays{};
    const auto poll = [&delays](std::size_t index) noexcept {
        return --delays[index] <= 0 ? UploadFenceStatus::Signaled : UploadFenceStatus::Busy;
    };

    const auto first = ring.select(7, 1, false, poll);
    if (first.kind != UploadSelectionKind::Full) {
        fail("First upload did not select a free ring slot");
    }
    ring.markUploaded(first.slot, 7, 1);
    delays[first.slot] = 4;
    ring.markSubmitted(first.slot);

    const auto cached = ring.select(7, 1, false, poll);
    if (cached.kind != UploadSelectionKind::Cached || cached.slot != first.slot
        || delays[first.slot] != 4) {
        fail("Cached read polled or rewrote an in-flight slot");
    }

    delays[first.slot] = 1;
    const auto partial = ring.select(7, 2, true, poll);
    if (partial.kind != UploadSelectionKind::Partial || partial.slot != first.slot) {
        fail("Signaled predecessor slot did not accept a partial upload");
    }
}

void testDelayedGpuExhaustionNeverSelectsBusyStorage() {
    OpenGlUploadRing ring;
    ring.reset(3);
    std::array<int, 3> delays{};
    std::array<std::array<std::uint64_t, 16>, 3> slotContents{};
    std::array<std::uint64_t, 16> expected{};
    const auto poll = [&delays](std::size_t index) noexcept {
        if (delays[index] > 0) {
            --delays[index];
        }
        return delays[index] == 0 ? UploadFenceStatus::Signaled : UploadFenceStatus::Busy;
    };

    std::uint64_t revision = 1;
    for (std::size_t index = 0; index < delays.size(); ++index) {
        const std::size_t dirtyOffset = static_cast<std::size_t>((revision - 1) % 8);
        for (std::size_t dirty = dirtyOffset; dirty < dirtyOffset + 8; ++dirty) {
            expected[dirty] = revision;
        }
        const auto selected = ring.select(11, revision++, true, poll);
        if (!selected.requiresUpload() || delays[selected.slot] != 0) {
            fail("Ring selected storage still owned by the delayed GPU");
        }
        slotContents[selected.slot] = expected;
        ring.markUploaded(selected.slot, 11, revision - 1);
        delays[selected.slot] = 6;
        ring.markSubmitted(selected.slot);
    }

    std::uint64_t exhaustions = 0;
    std::uint64_t uploads = 0;
    bool updatePending = false;
    std::size_t dirtyOffset = 0;
    for (int iteration = 0; iteration < 2000; ++iteration) {
        if (!updatePending) {
            dirtyOffset = static_cast<std::size_t>((revision - 1) % 8);
            for (std::size_t dirty = dirtyOffset; dirty < dirtyOffset + 8; ++dirty) {
                expected[dirty] = revision;
            }
            updatePending = true;
        }
        const auto selected = ring.select(11, revision, true, poll);
        if (selected.kind == UploadSelectionKind::Exhausted) {
            ++exhaustions;
            continue;
        }
        if (!selected.requiresUpload() || delays[selected.slot] != 0) {
            fail("Overlapping update targeted an in-flight upload range");
        }
        if (selected.kind == UploadSelectionKind::Partial) {
            for (std::size_t dirty = dirtyOffset; dirty < dirtyOffset + 8; ++dirty) {
                slotContents[selected.slot][dirty] = expected[dirty];
            }
        } else {
            slotContents[selected.slot] = expected;
        }
        if (slotContents[selected.slot] != expected) {
            fail("Full/partial fallback did not preserve overlapping instance updates");
        }
        ring.markUploaded(selected.slot, 11, revision++);
        delays[selected.slot] = 6;
        ring.markSubmitted(selected.slot);
        ++uploads;
        updatePending = false;
    }
    if (exhaustions == 0 || uploads == 0) {
        fail("Delayed-GPU stress did not exercise exhaustion and recovery");
    }
}

void testFailedFenceQuarantinesSlot() {
    OpenGlUploadRing ring;
    ring.reset(2);
    const auto available = [](std::size_t) noexcept { return UploadFenceStatus::Signaled; };
    const auto first = ring.select(3, 1, false, available);
    ring.markUploaded(first.slot, 3, 1);
    ring.markFenceFailed(first.slot);

    const auto second = ring.select(4, 1, false, available);
    if (second.kind != UploadSelectionKind::Full || second.slot == first.slot) {
        fail("Fence failure did not quarantine its upload slot");
    }
}

} // namespace

int main() {
    testCachedReadsAndPartialReuse();
    testDelayedGpuExhaustionNeverSelectsBusyStorage();
    testFailedFenceQuarantinesSlot();
    std::cout << "HeniaUI OpenGL upload-ring tests passed\n";
    return EXIT_SUCCESS;
}
