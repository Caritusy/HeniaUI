#include "D3D12TextureUploadTransaction.h"

#include <cstdlib>
#include <iostream>

namespace {

using henia::ui::detail::D3D12TextureUploadState;
using henia::ui::detail::D3D12TextureUploadTransaction;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void verifyRecordingFailureRollback() {
    // Resource creation, upload mapping, and command-list close failures all
    // happen before execution and therefore share the same rollback boundary.
    for (int injectedFailure = 0; injectedFailure < 3; ++injectedFailure) {
        D3D12TextureUploadTransaction transaction;
        if (!transaction.begin() || !transaction.ownsResources()) {
            fail("Texture upload transaction did not enter recording state");
        }
        transaction.rollback();
        if (transaction.state() != D3D12TextureUploadState::Available
            || transaction.ownsResources() || transaction.fenceValue() != 0) {
            fail("Pre-execution upload failure did not roll back");
        }
    }
}

void verifySignalFailureRetainsResources() {
    D3D12TextureUploadTransaction transaction;
    if (!transaction.begin()) {
        fail("Texture upload transaction could not begin");
    }
    transaction.abandon();
    if (transaction.state() != D3D12TextureUploadState::Abandoned
        || !transaction.ownsResources() || transaction.release(UINT64_MAX)) {
        fail("Signal failure released resources with untracked GPU ownership");
    }
}

void verifyCompletionControlsCommit() {
    D3D12TextureUploadTransaction transaction;
    if (!transaction.begin() || !transaction.submit(42)) {
        fail("Texture upload transaction could not be submitted");
    }
    if (transaction.completed(41) || transaction.release(41)
        || !transaction.ownsResources()) {
        fail("Incomplete texture upload became synchronized");
    }
    if (!transaction.completed(42) || !transaction.release(42)
        || transaction.ownsResources()
        || transaction.state() != D3D12TextureUploadState::Available) {
        fail("Completed texture upload was not committed and released");
    }
}

} // namespace

int main() {
    verifyRecordingFailureRollback();
    verifySignalFailureRetainsResources();
    verifyCompletionControlsCommit();
    std::cout << "HeniaUI D3D12 texture upload transaction tests passed\n";
    return EXIT_SUCCESS;
}
