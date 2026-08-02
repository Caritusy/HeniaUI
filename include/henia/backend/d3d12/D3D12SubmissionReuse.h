#pragma once

#include <d3d12.h>

#include <cstdint>

namespace henia::backend::d3d12 {

// Optional proof that a submission slot's previous GPU use has completed.
// An empty value declares that the slot is new or that the host synchronized it
// by another mechanism. When supplied, record() validates the fence, its owner
// device, and completionValue before touching slot-owned upload resources.
struct SubmissionReuse final {
    ID3D12Fence* completionFence = nullptr;
    std::uint64_t completionValue = 0;
};

} // namespace henia::backend::d3d12
