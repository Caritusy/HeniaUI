#pragma once

#include <cstdint>
#include <string_view>

namespace henia::backend::directx {

// The public integration layer uses one DirectX name. The concrete API is an
// implementation detail selected after probing the host adapter.
enum class Api : std::uint8_t {
    Automatic,
    D3D11,
    D3D12,
    None,
};

struct ProbeOptions final {
    // Software adapters are useful for deterministic WARP compatibility tests
    // and are included by default when no hardware adapter is available.
    bool allowSoftware = true;
};

struct ProbeResult final {
    Api selected = Api::None;
    bool d3d12Available = false;
    bool d3d11Available = false;
    bool d3d12SoftwareAdapter = false;
    bool d3d11SoftwareAdapter = false;
    bool usedSoftwareAdapter = false;
    std::uint32_t d3d12FeatureLevel = 0;
    std::uint32_t d3d11FeatureLevel = 0;
    long d3d12Status = 0;
    long d3d11Status = 0;
    std::string_view diagnostic{};
};

// Probes adapters in a deterministic order and always tests D3D12 first.
// This function creates only temporary devices; it never owns a host device,
// context, swap chain, queue, or render target.
[[nodiscard]] ProbeResult probe(ProbeOptions options = {}) noexcept;

[[nodiscard]] constexpr Api select(const ProbeResult& result) noexcept {
    if (result.d3d12Available) return Api::D3D12;
    if (result.d3d11Available) return Api::D3D11;
    return Api::None;
}

[[nodiscard]] constexpr std::string_view apiName(Api api) noexcept {
    switch (api) {
        case Api::Automatic: return "Automatic";
        case Api::D3D11: return "D3D11";
        case Api::D3D12: return "D3D12";
        case Api::None: return "None";
    }
    return "None";
}

} // namespace henia::backend::directx
