#include "henia/backend/directx/DirectXBackend.h"

#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>

namespace henia::backend::directx {
namespace {

using Microsoft::WRL::ComPtr;

struct Adapter final {
    ComPtr<IDXGIAdapter1> value;
    bool software = false;
};

[[nodiscard]] bool isSoftware(IDXGIAdapter1& adapter) noexcept {
    DXGI_ADAPTER_DESC1 description{};
    return SUCCEEDED(adapter.GetDesc1(&description))
        && (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

[[nodiscard]] bool tryD3D12(
    IDXGIAdapter& adapter,
    ProbeResult& result,
    bool software) noexcept {
    ComPtr<ID3D12Device> device;
    const HRESULT status = D3D12CreateDevice(
        &adapter,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device));
    if (FAILED(status)) {
        if (result.d3d12Status == 0) result.d3d12Status = static_cast<long>(status);
        return false;
    }
    result.d3d12Available = true;
    result.d3d12Status = static_cast<long>(status);
    result.d3d12FeatureLevel = static_cast<std::uint32_t>(D3D_FEATURE_LEVEL_11_0);
    result.d3d12SoftwareAdapter = software;
    return true;
}

[[nodiscard]] bool tryD3D11(
    IDXGIAdapter& adapter,
    ProbeResult& result,
    bool software) noexcept {
    constexpr std::array levels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL selected = D3D_FEATURE_LEVEL_9_1;
    HRESULT status = D3D11CreateDevice(
        &adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        levels.data(),
        static_cast<UINT>(levels.size()),
        D3D11_SDK_VERSION,
        &device,
        &selected,
        &context);
    if (status == E_INVALIDARG) {
        constexpr std::array fallbackLevels{D3D_FEATURE_LEVEL_11_0};
        status = D3D11CreateDevice(
            &adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            fallbackLevels.data(),
            static_cast<UINT>(fallbackLevels.size()),
            D3D11_SDK_VERSION,
            &device,
            &selected,
            &context);
    }
    if (FAILED(status)) {
        if (result.d3d11Status == 0) result.d3d11Status = static_cast<long>(status);
        return false;
    }
    result.d3d11Available = true;
    result.d3d11Status = static_cast<long>(status);
    result.d3d11FeatureLevel = static_cast<std::uint32_t>(selected);
    result.d3d11SoftwareAdapter = software;
    return true;
}

} // namespace

ProbeResult probe(ProbeOptions options) noexcept {
    ProbeResult result{};
    ComPtr<IDXGIFactory1> factory;
    const HRESULT factoryStatus = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(factoryStatus)) {
        result.d3d12Status = static_cast<long>(factoryStatus);
        result.d3d11Status = static_cast<long>(factoryStatus);
        result.diagnostic = "DXGI factory creation failed";
        return result;
    }

    std::array<Adapter, 32> adapters{};
    std::size_t count = 0;
    for (UINT index = 0; index < adapters.size(); ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (adapter == nullptr) continue;
        const bool software = isSoftware(*adapter.Get());
        if (software && !options.allowSoftware) continue;
        adapters[count++] = {.value = std::move(adapter), .software = software};
    }

    // Hardware adapters are intentionally attempted first. WARP is added only
    // when requested and no usable adapter was found in the normal enumeration.
    for (std::size_t index = 0; index < count; ++index) {
        if (adapters[index].software) continue;
        static_cast<void>(tryD3D12(*adapters[index].value.Get(), result, false));
        static_cast<void>(tryD3D11(*adapters[index].value.Get(), result, false));
        if (result.d3d12Available && result.d3d11Available) break;
    }
    if ((!result.d3d12Available || !result.d3d11Available) && options.allowSoftware) {
        for (std::size_t index = 0; index < count; ++index) {
            if (!adapters[index].software) continue;
            if (!result.d3d12Available) {
                static_cast<void>(tryD3D12(*adapters[index].value.Get(), result, true));
            }
            if (!result.d3d11Available) {
                static_cast<void>(tryD3D11(*adapters[index].value.Get(), result, true));
            }
            if (result.d3d12Available && result.d3d11Available) break;
        }
    }

    // Some stripped-down systems expose WARP through the factory only after
    // the adapter list is exhausted. Explicitly test it as a final fallback.
    if ((!result.d3d12Available || !result.d3d11Available) && options.allowSoftware) {
        ComPtr<IDXGIFactory4> factory4;
        ComPtr<IDXGIAdapter> warp;
        if (SUCCEEDED(factory.As(&factory4))
            && SUCCEEDED(factory4->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) {
            if (!result.d3d12Available) {
                static_cast<void>(tryD3D12(*warp.Get(), result, true));
            }
            if (!result.d3d11Available) {
                static_cast<void>(tryD3D11(*warp.Get(), result, true));
            }
        }
    }

    result.selected = select(result);
    result.usedSoftwareAdapter = result.selected == Api::D3D12
        ? result.d3d12SoftwareAdapter
        : result.selected == Api::D3D11 && result.d3d11SoftwareAdapter;
    if (result.selected == Api::D3D12) {
        result.diagnostic = result.usedSoftwareAdapter
            ? "D3D12 is available through a hardware or WARP adapter"
            : "D3D12 is available";
    } else if (result.selected == Api::D3D11) {
        result.diagnostic = result.usedSoftwareAdapter
            ? "D3D12 unavailable; using D3D11 through WARP"
            : "D3D12 unavailable; using D3D11";
    } else {
        result.diagnostic = "No compatible DirectX device was found";
    }
    return result;
}

} // namespace henia::backend::directx
