#include "henia/ui/Frame.h"
#include "henia/ui/backend/d3d12/D3D12Renderer.h"
#include "henia/ui/resource/TextureStore.h"

#include "../src/backend/FixedError.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

static_assert(!std::is_move_constructible_v<henia::ui::D3D12Renderer>);
static_assert(!std::is_move_assignable_v<henia::ui::D3D12Renderer>);

using Microsoft::WRL::ComPtr;

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
    if (FAILED(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        return false;
    }
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr) {
        return false;
    }
    const bool result = SUCCEEDED(queue.Signal(fence.Get(), 1))
        && SUCCEEDED(fence->SetEventOnCompletion(1, eventHandle))
        && WaitForSingleObject(eventHandle, 10000) == WAIT_OBJECT_0;
    CloseHandle(eventHandle);
    return result;
}

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using namespace henia::ui;

    henia::detail::FixedError diagnostic;
    std::array<char, henia::detail::FixedError::kCapacity + 32U> oversizedDiagnostic{};
    oversizedDiagnostic.fill('x');
    diagnostic.assign(oversizedDiagnostic.data(), oversizedDiagnostic.size());
    if (diagnostic.view().size() != henia::detail::FixedError::kCapacity - 1U) {
        fail("Fixed backend diagnostics did not truncate without allocation");
    }

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter> warpAdapter;
    ComPtr<ID3D12Device> device;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))
        || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)))
        || FAILED(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)))) {
        fail("Unable to create the D3D12 WARP device");
    }

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)))) {
        fail("Unable to create the D3D12 queue");
    }

    TextureStore textures;
    std::array<std::byte, 16> alpha{};
    alpha.fill(std::byte{0xFF});
    const TextureHandle atlas = textures.create(TextureFormat::Alpha8, 4, 4, 4, alpha);
    if (!atlas.valid()) {
        fail("Unable to create the test texture");
    }

    D3D12Renderer renderer;
    if (!renderer.initialize(
            *device.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            {.instanceCapacity = 128, .submissionCapacity = 2, .batchCapacity = 8, .textureCapacity = 1})
        || !renderer.synchronizeTextures(textures, *queue.Get())) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }

    const TextureHandle overflowTexture = textures.create(TextureFormat::Alpha8, 4, 4, 4, alpha);
    if (!overflowTexture.valid() || renderer.synchronizeTextures(textures, *queue.Get())
        || renderer.lastError() != "D3D12 texture store exceeds configured capacity") {
        fail("D3D12 texture bookkeeping overflow was not rejected deterministically");
    }

    Frame frame;
    frame.reserve(16, 4);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{8.0F, 8.0F}, {120.0F, 120.0F}}, {0.85F, 0.12F, 0.18F, 1.0F}, 10.0F);
    constexpr std::array glyphs{
        GlyphQuad{{{40.0F, 40.0F}, {88.0F, 88.0F}}, {{0.0F, 0.0F}, {1.0F, 1.0F}}},
    };
    canvas.glyphs(atlas, glyphs, {1.0F, 1.0F, 1.0F, 0.4F});
    const RenderPacket& packet = frame.finish();
    if (packet.batches().size() != 1) {
        fail("Test UI did not compile into one batch");
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
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &targetDescription,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&target)))) {
        fail("Unable to create the D3D12 render target");
    }

    D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDescription{};
    renderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    renderTargetHeapDescription.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> renderTargetHeap;
    if (FAILED(device->CreateDescriptorHeap(
            &renderTargetHeapDescription,
            IID_PPV_ARGS(&renderTargetHeap)))) {
        fail("Unable to create the D3D12 render target descriptor");
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.Get(), nullptr, renderTarget);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 readbackBytes = 0;
    device->GetCopyableFootprints(
        &targetDescription,
        0,
        1,
        0,
        &footprint,
        &rows,
        &rowBytes,
        &readbackBytes);
    const D3D12_HEAP_PROPERTIES readbackHeap = heapProperties(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC readbackDescription = bufferDescription(readbackBytes);
    ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &readbackDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback)))) {
        fail("Unable to create the D3D12 readback buffer");
    }

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)))
        || FAILED(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)))) {
        fail("Unable to create D3D12 recording objects");
    }

    constexpr std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    commandList->ClearRenderTargetView(renderTarget, clearColor.data(), 0, nullptr);
    if (!renderer.record(packet, *commandList.Get(), 0, width, height)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = target.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (FAILED(commandList->Close())) {
        fail("Unable to close the D3D12 test command list");
    }
    ID3D12CommandList* lists[]{commandList.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) {
        fail("D3D12 test queue timed out");
    }

    const std::byte* mapped = nullptr;
    const D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackBytes)};
    if (FAILED(readback->Map(0, &readRange, reinterpret_cast<void**>(const_cast<std::byte**>(&mapped))))
        || mapped == nullptr) {
        fail("Unable to map the D3D12 readback buffer");
    }
    const std::size_t centerOffset = footprint.Offset
        + static_cast<std::size_t>(height / 2) * footprint.Footprint.RowPitch
        + static_cast<std::size_t>(width / 2) * 4U;
    const auto red = static_cast<unsigned char>(mapped[centerOffset]);
    const auto green = static_cast<unsigned char>(mapped[centerOffset + 1]);
    const auto blue = static_cast<unsigned char>(mapped[centerOffset + 2]);
    readback->Unmap(0, nullptr);
    if (red < 180 || green < 60 || blue < 60) {
        std::cerr << "Unexpected center pixel: "
                  << static_cast<int>(red) << ','
                  << static_cast<int>(green) << ','
                  << static_cast<int>(blue) << '\n';
        return EXIT_FAILURE;
    }

    const D3D12RenderStatistics statistics = renderer.statistics();
    if (statistics.drawCalls != 1 || statistics.submittedInstances != 2
        || statistics.textureUploads != 1 || statistics.instanceUploads != 1) {
        fail("D3D12 renderer statistics are incorrect");
    }

    renderer.shutdown();
    std::cout << "HeniaUI D3D12 WARP test passed\n";
    return EXIT_SUCCESS;
}
