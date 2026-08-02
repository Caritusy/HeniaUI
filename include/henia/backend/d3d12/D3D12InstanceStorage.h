#pragma once

#include <cstddef>
#include <cstdint>

namespace henia::backend::d3d12 {

// Automatic keeps small/UMA workloads on persistently mapped upload memory and
// uses GPU-local default memory for larger packets on discrete adapters.
enum class InstanceStorageStrategy : std::uint8_t {
    Automatic,
    DirectUpload,
    GpuLocal,
};

inline constexpr std::size_t kDefaultGpuLocalInstanceThresholdBytes = 64U * 1024U;

} // namespace henia::backend::d3d12
