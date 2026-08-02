#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <iostream>
#include <vector>

namespace henia::test {

[[nodiscard]] inline bool d3d12ValidationRequested() noexcept {
    std::array<char, 8> value{};
    const DWORD length = GetEnvironmentVariableA(
        "HENIAUI_D3D12_VALIDATION",
        value.data(),
        static_cast<DWORD>(value.size()));
    return length > 0 && length < value.size() && value[0] == '1';
}

[[nodiscard]] inline bool enableD3D12Validation() noexcept {
    if (!d3d12ValidationRequested()) {
        return true;
    }
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        std::cerr << "D3D12 debug layer is unavailable\n";
        return false;
    }
    debug->EnableDebugLayer();
    Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
    if (FAILED(debug.As(&debug1))) {
        std::cerr << "D3D12 GPU-based validation interface is unavailable\n";
        return false;
    }
    debug1->SetEnableGPUBasedValidation(TRUE);
    debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
        dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    }
    return true;
}

[[nodiscard]] inline bool verifyD3D12Validation(ID3D12Device& device) {
    if (!d3d12ValidationRequested()) {
        return true;
    }
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
    if (FAILED(device.QueryInterface(IID_PPV_ARGS(&queue)))) {
        std::cerr << "D3D12 validation info queue is unavailable\n";
        return false;
    }

    bool clean = true;
    const UINT64 count = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 index = 0; index < count; ++index) {
        SIZE_T bytes = 0;
        if (FAILED(queue->GetMessage(index, nullptr, &bytes)) || bytes == 0) {
            clean = false;
            continue;
        }
        std::vector<std::byte> storage(bytes);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(queue->GetMessage(index, message, &bytes))) {
            clean = false;
            continue;
        }
        if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION
            || message->Severity == D3D12_MESSAGE_SEVERITY_ERROR) {
            std::cerr << "D3D12 validation error " << message->ID << ": "
                      << (message->pDescription == nullptr ? "<no description>" : message->pDescription)
                      << '\n';
            clean = false;
        }
    }
    if (FAILED(device.GetDeviceRemovedReason())) {
        std::cerr << "D3D12 device removal detected; DRED breadcrumbs and page-fault tracking were enabled\n";
        clean = false;
    }
    return clean;
}

} // namespace henia::test
