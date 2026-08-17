#include "henia/backend/directx/DirectXBackend.h"
#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/backend/directx/DirectXRenderDevice.h"
#include "henia/ui/Frame.h"
#include "henia/ui/backend/directx/DirectXRenderer.h"

#include <Windows.h>
#include <d3d11.h>
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

using Microsoft::WRL::ComPtr;

[[noreturn]] void fail(const char* message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

struct D3D11Target final {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11Texture2D> staging;
};

[[nodiscard]] bool createTarget(
    ID3D11Device& device,
    std::uint32_t width,
    std::uint32_t height,
    D3D11Target& output) noexcept {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device.CreateTexture2D(&description, nullptr, &output.texture))
        || FAILED(device.CreateRenderTargetView(
            output.texture.Get(), nullptr, &output.renderTarget))) {
        return false;
    }
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return SUCCEEDED(device.CreateTexture2D(&description, nullptr, &output.staging));
}

[[nodiscard]] std::array<std::uint8_t, 4> readPixel(
    ID3D11DeviceContext& context,
    D3D11Target& target,
    std::uint32_t x,
    std::uint32_t y) {
    ID3D11RenderTargetView* noTargets[]{nullptr};
    context.OMSetRenderTargets(1, noTargets, nullptr);
    context.CopyResource(target.staging.Get(), target.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context.Map(target.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        fail("Unable to map the D3D11 WARP readback texture");
    }
    const auto* bytes = static_cast<const std::byte*>(mapped.pData);
    const std::size_t offset = static_cast<std::size_t>(y) * mapped.RowPitch
        + static_cast<std::size_t>(x) * 4U;
    const std::array result{
        static_cast<std::uint8_t>(bytes[offset]),
        static_cast<std::uint8_t>(bytes[offset + 1U]),
        static_cast<std::uint8_t>(bytes[offset + 2U]),
        static_cast<std::uint8_t>(bytes[offset + 3U]),
    };
    context.Unmap(target.staging.Get(), 0);
    return result;
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

[[nodiscard]] bool waitForQueue(
    ID3D12Device& device,
    ID3D12CommandQueue& queue) noexcept {
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        return false;
    }
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr) return false;
    const bool completed = SUCCEEDED(queue.Signal(fence.Get(), 1))
        && SUCCEEDED(fence->SetEventOnCompletion(1, eventHandle))
        && WaitForSingleObject(eventHandle, 10000) == WAIT_OBJECT_0;
    CloseHandle(eventHandle);
    return completed;
}

} // namespace

int main() {
    using henia::backend::directx::Api;
    using henia::backend::directx::ProbeResult;

    static_assert(!std::is_move_constructible_v<henia::ui::DirectXRenderer>);
    static_assert(!std::is_move_constructible_v<henia::gfx::DirectXRenderDevice>);

    if (henia::backend::directx::select({
            .d3d12Available = true,
            .d3d11Available = true,
        }) != Api::D3D12) {
        fail("DirectX selector did not prefer D3D12");
    }
    if (henia::backend::directx::select({
            .d3d12Available = false,
            .d3d11Available = true,
        }) != Api::D3D11) {
        fail("DirectX selector did not fall back to D3D11");
    }
    if (henia::backend::directx::select(ProbeResult{}) != Api::None) {
        fail("DirectX selector did not report an unavailable machine");
    }
    const ProbeResult detected = henia::backend::directx::probe();
    if (detected.selected == Api::None || !detected.d3d11Available
        || detected.d3d11FeatureLevel < D3D_FEATURE_LEVEL_11_0
        || detected.diagnostic.empty()) {
        fail("DirectX machine probe did not find the WARP-compatible fallback");
    }

    constexpr std::array featureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_9_1;
    if (FAILED(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            featureLevels.data(),
            static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION,
            &d3d11Device,
            &featureLevel,
            &d3d11Context))) {
        fail("Unable to create the D3D11 WARP device");
    }

    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 64;
    D3D11Target target;
    if (!createTarget(*d3d11Device.Get(), width, height, target)) {
        fail("Unable to create the D3D11 WARP render target");
    }
    ID3D11RenderTargetView* renderTargets[]{target.renderTarget.Get()};
    constexpr std::array<float, 4> black{0.0F, 0.0F, 0.0F, 1.0F};
    d3d11Context->OMSetRenderTargets(1, renderTargets, nullptr);
    d3d11Context->ClearRenderTargetView(target.renderTarget.Get(), black.data());

    henia::ui::Frame frame;
    frame.reserve(4, 2);
    frame.begin().fillRect(
        {{8.0F, 8.0F}, {56.0F, 56.0F}},
        {1.0F, 0.0F, 0.0F, 1.0F});
    const henia::ui::RenderPacket packet = frame.finish();
    henia::ui::DirectXRenderer uiRenderer;
    if (!uiRenderer.initialize(
            *d3d11Device.Get(),
            *d3d11Context.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            {.instanceCapacity = 4, .textureCapacity = 1})
        || uiRenderer.backend() != Api::D3D11
        || !uiRenderer.render(packet, {
            .framebufferWidth = width,
            .framebufferHeight = height,
        })) {
        std::cerr << uiRenderer.lastError() << '\n';
        fail("Unified DirectX D3D11 UI path failed");
    }
    const auto uiCenter = readPixel(*d3d11Context.Get(), target, width / 2, height / 2);
    const auto uiCorner = readPixel(*d3d11Context.Get(), target, 2, 2);
    if (uiCenter[0] < 240 || uiCenter[1] > 8 || uiCenter[2] > 8
        || uiCorner[0] > 8 || uiCorner[1] > 8 || uiCorner[2] > 8) {
        fail("D3D11 UI WARP output probes are incorrect");
    }
    const henia::ui::DirectXRenderStatistics uiStatistics = uiRenderer.statistics();
    if (uiStatistics.successfulFrames != 1 || uiStatistics.drawCalls != 1
        || uiStatistics.submittedInstances != 1
        || uiStatistics.uploadedInstanceBytes != sizeof(henia::ui::DrawInstance)) {
        fail("D3D11 UI statistics are incorrect");
    }

    henia::ui::TextureStore textures;
    const std::array<std::byte, 4> bluePixel{
        std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF},
    };
    const henia::ui::TextureHandle blueTexture = textures.create(
        henia::ui::TextureFormat::Rgba8,
        1,
        1,
        4,
        bluePixel);
    henia::ui::Frame imageFrame;
    imageFrame.begin().image(
        blueTexture,
        {{0.0F, 0.0F}, {static_cast<float>(width), static_cast<float>(height)}});
    d3d11Context->OMSetRenderTargets(1, renderTargets, nullptr);
    d3d11Context->ClearRenderTargetView(target.renderTarget.Get(), black.data());
    if (!uiRenderer.synchronizeTextures(textures)
        || !uiRenderer.render(imageFrame.finish(), {
            .framebufferWidth = width,
            .framebufferHeight = height,
        })) {
        std::cerr << uiRenderer.lastError() << '\n';
        fail("D3D11 UI textured path failed");
    }
    const auto texturedCenter = readPixel(
        *d3d11Context.Get(), target, width / 2, height / 2);
    if (texturedCenter[2] < 240 || texturedCenter[0] > 8 || texturedCenter[1] > 8
        || uiRenderer.statistics().textureUploads != 1) {
        fail("D3D11 UI textured WARP output probe is incorrect");
    }

    d3d11Context->OMSetRenderTargets(1, renderTargets, nullptr);
    d3d11Context->ClearRenderTargetView(target.renderTarget.Get(), black.data());
    henia::gfx::ShapeBatch3D shapes;
    henia::gfx::BoxInstance box{
        .minimum = {-0.5F, -0.5F, 0.5F},
        .lineWidth = 2.0F,
        .maximum = {0.5F, 0.5F, 0.5F},
        .color = {0.0F, 1.0F, 0.0F, 1.0F},
    };
    box.setFillOpacity(1.0F);
    box.setOutlineEnabled(false);
    if (!shapes.setDepthState({.enabled = true, .writeEnabled = true})) {
        fail("Unable to configure the DirectX depth fallback fixture");
    }
    shapes.addBox(box);
    const henia::gfx::InstanceBatch batch = shapes.snapshot();
    henia::gfx::DirectXRenderDevice gfxRenderer;
    if (!gfxRenderer.initialize(
            *d3d11Device.Get(),
            *d3d11Context.Get(),
            {.boxCapacity = 4})
        || gfxRenderer.backend() != Api::D3D11
        || !gfxRenderer.render(
            batch,
            {.viewport = {static_cast<float>(width), static_cast<float>(height)}})) {
        std::cerr << gfxRenderer.lastError() << '\n';
        fail("Unified DirectX D3D11 gfx path failed");
    }
    const auto gfxCenter = readPixel(*d3d11Context.Get(), target, width / 2, height / 2);
    if (gfxCenter[1] < 180 || gfxCenter[0] > 32 || gfxCenter[2] > 32) {
        fail("D3D11 gfx WARP output probe is incorrect");
    }
    const henia::gfx::DirectXGfxStatistics gfxStatistics = gfxRenderer.statistics();
    if (gfxStatistics.successfulFrames != 1 || gfxStatistics.drawCalls != 1
        || gfxStatistics.submittedInstances != 1
        || gfxStatistics.uploadedInstanceBytes != sizeof(henia::gfx::BoxInstance)
        || gfxStatistics.depthFallbacks != 1) {
        fail("D3D11 gfx statistics are incorrect");
    }

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> d3d12Device;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))
        || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))
        || FAILED(D3D12CreateDevice(
            warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device)))) {
        fail("Unable to create the D3D12 WARP compatibility device");
    }
    henia::ui::DirectXRenderer d3d12Ui;
    if (!d3d12Ui.initialize(
            *d3d12Device.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            {
                .instanceCapacity = 4,
                .submissionCapacity = 1,
                .batchCapacity = 2,
                .textureCapacity = 1,
                .textureUploadBatchCapacity = 1,
                .textureUploadArenaBytes = 1024,
            })
        || d3d12Ui.backend() != Api::D3D12) {
        std::cerr << d3d12Ui.lastError() << '\n';
        fail("Unified DirectX UI path did not expose D3D12");
    }
    henia::gfx::DirectXRenderDevice d3d12Gfx;
    if (!d3d12Gfx.initialize(
            *d3d12Device.Get(),
            {
                .boxCapacity = 4,
                .submissionCapacity = 1,
                .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
            })
        || d3d12Gfx.backend() != Api::D3D12) {
        std::cerr << d3d12Gfx.lastError() << '\n';
        fail("Unified DirectX gfx path did not expose D3D12");
    }

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(d3d12Device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)))
        || FAILED(d3d12Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)))
        || FAILED(d3d12Device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)))) {
        fail("Unable to create D3D12 unified-path submission objects");
    }
    D3D12_RESOURCE_DESC targetDescription{};
    targetDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    targetDescription.Width = width;
    targetDescription.Height = height;
    targetDescription.DepthOrArraySize = 1;
    targetDescription.MipLevels = 1;
    targetDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    targetDescription.SampleDesc.Count = 1;
    targetDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = targetDescription.Format;
    clearValue.Color[3] = 1.0F;
    const D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> d3d12Target;
    if (FAILED(d3d12Device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &targetDescription,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&d3d12Target)))) {
        fail("Unable to create the D3D12 unified-path render target");
    }
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDescription.NumDescriptors = 1;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    if (FAILED(d3d12Device->CreateDescriptorHeap(
            &rtvHeapDescription,
            IID_PPV_ARGS(&rtvHeap)))) {
        fail("Unable to create the D3D12 unified-path RTV heap");
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    d3d12Device->CreateRenderTargetView(d3d12Target.Get(), nullptr, rtv);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readbackBytes = 0;
    d3d12Device->GetCopyableFootprints(
        &targetDescription,
        0,
        1,
        0,
        &footprint,
        nullptr,
        nullptr,
        &readbackBytes);
    const D3D12_HEAP_PROPERTIES readbackHeap = heapProperties(D3D12_HEAP_TYPE_READBACK);
    const D3D12_RESOURCE_DESC readbackDescription = bufferDescription(readbackBytes);
    ComPtr<ID3D12Resource> d3d12Readback;
    if (FAILED(d3d12Device->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &readbackDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&d3d12Readback)))) {
        fail("Unable to create the D3D12 unified-path readback buffer");
    }

    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList->ClearRenderTargetView(rtv, black.data(), 0, nullptr);
    if (!d3d12Ui.record(packet, *commandList.Get(), 0, {
            .framebufferWidth = width,
            .framebufferHeight = height,
        })
        || !d3d12Gfx.record(
            batch,
            {.viewport = {static_cast<float>(width), static_cast<float>(height)}},
            *commandList.Get(),
            0)) {
        std::cerr << d3d12Ui.lastError() << ' ' << d3d12Gfx.lastError() << '\n';
        fail("Unified DirectX D3D12 recording failed");
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = d3d12Target.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = d3d12Target.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = d3d12Readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (FAILED(commandList->Close())) {
        fail("Unable to close the D3D12 unified-path command list");
    }
    ID3D12CommandList* lists[]{commandList.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*d3d12Device.Get(), *queue.Get())) {
        fail("D3D12 unified-path queue timed out");
    }
    void* mappedReadback = nullptr;
    const D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackBytes)};
    if (FAILED(d3d12Readback->Map(0, &readRange, &mappedReadback))
        || mappedReadback == nullptr) {
        fail("Unable to map the D3D12 unified-path readback buffer");
    }
    const auto* pixels = static_cast<const std::byte*>(mappedReadback);
    const auto d3d12Pixel = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t offset = footprint.Offset
            + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch
            + static_cast<std::size_t>(x) * 4U;
        return std::array{
            static_cast<std::uint8_t>(pixels[offset]),
            static_cast<std::uint8_t>(pixels[offset + 1U]),
            static_cast<std::uint8_t>(pixels[offset + 2U]),
            static_cast<std::uint8_t>(pixels[offset + 3U]),
        };
    };
    const auto d3d12UiProbe = d3d12Pixel(10, 10);
    const auto d3d12GfxProbe = d3d12Pixel(width / 2, height / 2);
    d3d12Readback->Unmap(0, nullptr);
    if (d3d12UiProbe[0] < 240 || d3d12UiProbe[1] > 8 || d3d12UiProbe[2] > 8
        || d3d12GfxProbe[1] < 180 || d3d12GfxProbe[0] > 32
        || d3d12GfxProbe[2] > 32
        || d3d12Ui.statistics().successfulFrames != 1
        || d3d12Gfx.statistics().successfulFrames != 1
        || d3d12Gfx.statistics().depthFallbacks != 1) {
        fail("Unified DirectX D3D12 WARP output probes are incorrect");
    }

    return EXIT_SUCCESS;
}
