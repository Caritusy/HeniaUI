#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/backend/d3d12/D3D12RenderDevice.h"

#include "D3D12Validation.h"
#include "VisualRegression.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

namespace {

static_assert(!std::is_move_constructible_v<henia::gfx::D3D12RenderDevice>);
static_assert(!std::is_move_assignable_v<henia::gfx::D3D12RenderDevice>);

using Microsoft::WRL::ComPtr;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

[[nodiscard]] D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

[[nodiscard]] D3D12_RESOURCE_DESC bufferDescription(std::uint64_t bytes) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}

[[nodiscard]] bool waitForQueue(ID3D12Device& device, ID3D12CommandQueue& queue) noexcept {
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr) return false;
    const bool result = SUCCEEDED(queue.Signal(fence.Get(), 1))
        && SUCCEEDED(fence->SetEventOnCompletion(1, eventHandle))
        && WaitForSingleObject(eventHandle, 10000) == WAIT_OBJECT_0;
    CloseHandle(eventHandle);
    return result;
}

} // namespace

int main() {
    using namespace henia::gfx;

    if (!henia::test::enableD3D12Validation()) {
        fail("Unable to enable requested D3D12 validation");
    }

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))
        || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))
        || FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        fail("Unable to create the D3D12 WARP gfx device");
    }
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)))) {
        fail("Unable to create the D3D12 gfx queue");
    }

    constexpr std::uint32_t width = 128;
    constexpr std::uint32_t height = 128;
    D3D12_RESOURCE_DESC targetDescription{};
    targetDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    targetDescription.Width = width;
    targetDescription.Height = height;
    targetDescription.DepthOrArraySize = 1;
    targetDescription.MipLevels = 1;
    targetDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    targetDescription.SampleDesc.Count = 1;
    targetDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    const D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = targetDescription.Format;
    ComPtr<ID3D12Resource> target;
    if (FAILED(device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &targetDescription,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&target)))) {
        fail("Unable to create the D3D12 gfx render target");
    }
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDescription.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(&rtvHeap)))) {
        fail("Unable to create the D3D12 gfx RTV heap");
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.Get(), nullptr, renderTarget);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 readbackBytes = 0;
    device->GetCopyableFootprints(&targetDescription, 0, 1, 0, &footprint, &rows, &rowBytes, &readbackBytes);
    const D3D12_HEAP_PROPERTIES readbackHeap = heapProperties(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC readbackDescription = bufferDescription(readbackBytes);
    ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(
            &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDescription,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
        fail("Unable to create the D3D12 gfx readback buffer");
    }

    ShapeBatch3D builder;
    std::vector<BoxInstance> boxes(2048);
    for (BoxInstance& box : boxes) {
        box = {
            .minimum = {-0.5F, -0.5F, 0.25F},
            .lineWidth = 6.0F,
            .maximum = {0.5F, 0.5F, 0.75F},
            .color = {0.9F, 0.1F, 0.15F, 1.0F},
        };
    }
    boxes[0] = {
        .minimum = {0.60F, -0.5F, -0.25F},
        .lineWidth = 6.0F,
        .maximum = {0.90F, 0.5F, 0.25F},
        .color = {0.1F, 0.9F, 0.2F, 1.0F},
    };
    builder.replaceBoxes(boxes);
    const InstanceBatch batch = builder.snapshot();
    D3D12RenderDevice oversizedRenderer;
    if (oversizedRenderer.initialize(*device.Get(), {
            .boxCapacity = static_cast<std::size_t>(std::numeric_limits<UINT>::max())
                / sizeof(BoxInstance) + 1U,
        })) {
        fail("D3D12 gfx accepted a buffer-view byte capacity above UINT");
    }
    D3D12RenderDevice renderer;
    if (!renderer.initialize(*device.Get(), {
            .boxCapacity = boxes.size(),
            .submissionCapacity = 2,
            .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
        })) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))
        || FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)))) {
        fail("Unable to create D3D12 gfx recording objects");
    }
    ViewParameters invalidView{.viewport = {
        static_cast<float>(width), static_cast<float>(height)}};
    invalidView.viewProjection.values[7] = std::numeric_limits<float>::quiet_NaN();
    if (renderer.record(batch, invalidView, *commandList.Get(), 0)
        || renderer.lastError() != "view.viewProjection") {
        fail("D3D12 gfx accepted a non-finite view matrix");
    }
    const D3D12GfxStatistics invalidStatistics = renderer.statistics();
    if (invalidStatistics.invalidInputFrames != 1
        || invalidStatistics.capacityRejectedFrames != 0
        || invalidStatistics.drawCalls != 0 || invalidStatistics.fullInstanceUploads != 0) {
        fail("D3D12 gfx invalid-input rejection issued GPU work");
    }
    constexpr std::array<float, 4> black{0.0F, 0.0F, 0.0F, 1.0F};
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    commandList->ClearRenderTargetView(renderTarget, black.data(), 0, nullptr);
    ViewParameters view{.viewport = {static_cast<float>(width), static_cast<float>(height)}};
    if (!renderer.record(batch, view, *commandList.Get(), 0)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    view.timeSeconds = 10.0F;
    view.viewProjection.values[14] = 0.01F;
    if (!renderer.record(batch, view, *commandList.Get(), 0)) {
        fail("D3D12 gfx stable-frame recording failed");
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{target.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (FAILED(commandList->Close())) fail("Unable to close the D3D12 gfx command list");
    ID3D12CommandList* lists[]{commandList.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) fail("D3D12 gfx queue timed out");

    void* mappedMemory = nullptr;
    const D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackBytes)};
    if (FAILED(readback->Map(0, &readRange, &mappedMemory)) || mappedMemory == nullptr) {
        fail("Unable to map the D3D12 gfx readback buffer");
    }
    const auto* mappedPixels = static_cast<const std::byte*>(mappedMemory);
    std::vector<henia::test::Rgba8> pixels(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset = footprint.Offset
                + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch
                + static_cast<std::size_t>(x) * 4U;
            pixels[static_cast<std::size_t>(y) * width + x] = {
                static_cast<std::uint8_t>(mappedPixels[sourceOffset]),
                static_cast<std::uint8_t>(mappedPixels[sourceOffset + 1U]),
                static_cast<std::uint8_t>(mappedPixels[sourceOffset + 2U]),
                static_cast<std::uint8_t>(mappedPixels[sourceOffset + 3U]),
            };
        }
    }
    readback->Unmap(0, nullptr);
    const henia::test::Rgba8 redEdge = pixels[
        static_cast<std::size_t>(height / 2) * width + width / 4];
    const henia::test::Rgba8 nearPlaneEdge = pixels[
        static_cast<std::size_t>(height / 2) * width + width * 4U / 5U];
    if (redEdge.red < 100 || redEdge.red <= redEdge.green * 2U
        || nearPlaneEdge.green < 100
        || nearPlaneEdge.green <= nearPlaneEdge.red * 2U) {
        henia::test::writePpm("d3d12-gfx-near-plane-actual.ppm", pixels, width, height);
        std::cerr << "Unexpected D3D12 gfx golden probes: red="
                  << static_cast<unsigned>(redEdge.red) << ','
                  << static_cast<unsigned>(redEdge.green) << " near="
                  << static_cast<unsigned>(nearPlaneEdge.red) << ','
                  << static_cast<unsigned>(nearPlaneEdge.green) << '\n';
        return EXIT_FAILURE;
    }

    D3D12GfxStatistics statistics = renderer.statistics();
    if (statistics.drawCalls != 2 || statistics.submittedInstances != boxes.size() * 2U
        || statistics.fullInstanceUploads != 1 || statistics.partialInstanceUploads != 0
        || statistics.viewUpdates != 2) {
        fail("D3D12 gfx stable-frame statistics are incorrect");
    }

    BoxInstance changed = boxes[7];
    changed.lineWidth = 8.0F;
    static_cast<void>(builder.updateBox(7, changed));
    const InstanceBatch partial = builder.snapshot();
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset the D3D12 gfx command list");
    }
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(partial, view, *commandList.Get(), 0) || FAILED(commandList->Close())) {
        fail("D3D12 gfx partial-update recording failed");
    }
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) fail("D3D12 gfx partial-update queue timed out");
    statistics = renderer.statistics();
    if (statistics.partialInstanceUploads != 1 || statistics.drawCalls != 3) {
        fail("D3D12 gfx did not upload only the changed instance range");
    }

    if (!henia::test::verifyD3D12Validation(*device.Get())) {
        fail("D3D12 gfx validation reported an error");
    }

    renderer.shutdown();
    std::cout << "HeniaUI D3D12 gfx WARP test passed\n";
    return EXIT_SUCCESS;
}
