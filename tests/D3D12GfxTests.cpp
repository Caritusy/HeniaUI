#include "henia/gfx/ShapeBatch3D.h"
#include "henia/gfx/backend/d3d12/D3D12RenderDevice.h"

#include "D3D12Validation.h"
#include "D3D12HostDraw.h"
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
#include <string>
#include <string_view>
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
    henia::test::D3D12HostDraw hostDraw;
    if (!hostDraw.initialize(*device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM)) {
        fail("Unable to create the D3D12 gfx host-state contract pipeline");
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
    D3D12RenderDevice unsupportedSamples;
    if (unsupportedSamples.initialize(*device.Get(), {
            .boxCapacity = 1,
            .submissionCapacity = 1,
            .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
            .sampleCount = 3,
        })) {
        fail("D3D12 gfx accepted an unsupported target sample count");
    }
    D3D12RenderDevice renderer;
    const D3D12GfxConfiguration rendererConfiguration{
            .boxCapacity = boxes.size(),
            .submissionCapacity = 2,
            .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
            .instanceStorage = henia::backend::d3d12::InstanceStorageStrategy::GpuLocal,
    };
    if (!renderer.initialize(*device.Get(), rendererConfiguration)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    D3D12RenderDevice peerRenderer;
    D3D12GfxConfiguration changedConfiguration = rendererConfiguration;
    changedConfiguration.submissionCapacity = 1;
    if (!peerRenderer.initialize(*device.Get(), rendererConfiguration)
        || !peerRenderer.initialize(*device.Get(), rendererConfiguration)
        || peerRenderer.initialize(*device.Get(), changedConfiguration)
        || peerRenderer.lastError()
            != "D3D12 gfx renderer is already initialized with a different configuration"
        || !peerRenderer.initialized()) {
        fail("D3D12 gfx lifecycle/configuration validation failed");
    }
    peerRenderer.shutdown();
    if (!peerRenderer.initialize(*device.Get(), rendererConfiguration)
        || !peerRenderer.initialized()) {
        fail("D3D12 gfx renderer could not be recreated beside another live instance");
    }
    peerRenderer.shutdown();

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
    ComPtr<ID3D12CommandAllocator> bundleAllocator;
    ComPtr<ID3D12GraphicsCommandList> bundleList;
    if (FAILED(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_BUNDLE,
            IID_PPV_ARGS(&bundleAllocator)))
        || FAILED(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_BUNDLE,
            bundleAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&bundleList)))
        || renderer.record(batch, ViewParameters{}, *bundleList.Get(), 0)
        || renderer.lastError() != "D3D12 gfx recording requires a DIRECT command list") {
        fail("D3D12 gfx accepted an unsupported command-list type");
    }
    const D3D12GfxStatistics invalidStatistics = renderer.statistics();
    if (invalidStatistics.invalidInputFrames != 1
        || invalidStatistics.capacityRejectedFrames != 0
        || invalidStatistics.drawCalls != 0 || invalidStatistics.fullInstanceUploads != 0) {
        fail("D3D12 gfx invalid-input rejection issued GPU work");
    }
    ComPtr<ID3D12Fence> submissionReuseFence;
    if (FAILED(device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&submissionReuseFence)))) {
        fail("Unable to create the D3D12 gfx submission-reuse fence");
    }
    const henia::backend::d3d12::SubmissionReuse submissionReuse{
        .completionFence = submissionReuseFence.Get(),
        .completionValue = 1,
    };
    const ViewParameters fenceValidationView{
        .viewport = {static_cast<float>(width), static_cast<float>(height)},
    };
    if (renderer.record(
            batch, fenceValidationView, *commandList.Get(), 0, submissionReuse)
        || renderer.lastError() != "D3D12 gfx submission slot is still referenced by the GPU") {
        fail("D3D12 gfx renderer overwrote a fence-busy submission slot");
    }
    if (renderer.statistics().fullInstanceUploads != 0
        || renderer.statistics().partialInstanceUploads != 0
        || renderer.statistics().instanceCopyOperations != 0
        || renderer.statistics().drawCalls != 0) {
        fail("D3D12 gfx fence-busy rejection touched submission resources");
    }
    if (FAILED(queue->Signal(submissionReuseFence.Get(), 1))
        || !waitForQueue(*device.Get(), *queue.Get())) {
        fail("D3D12 gfx submission-reuse fence did not complete");
    }
    constexpr std::array<float, 4> black{0.0F, 0.0F, 0.0F, 1.0F};
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    commandList->ClearRenderTargetView(renderTarget, black.data(), 0, nullptr);
    hostDraw.record(
        *commandList.Get(), width, height,
        {0.0F, 0.0F, 4.0F, 4.0F},
        {1.0F, 0.0F, 0.0F, 1.0F});
    ViewParameters view{.viewport = {static_cast<float>(width), static_cast<float>(height)}};
    if (!renderer.record(batch, view, *commandList.Get(), 0, submissionReuse)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    const std::uint64_t firstProfileSampleId =
        renderer.statistics().profile.latestSample.identity.sampleId;
    view.timeSeconds = 10.0F;
    view.viewProjection.values[14] = 0.01F;
    if (!renderer.record(batch, view, *commandList.Get(), 0)) {
        fail("D3D12 gfx stable-frame recording failed");
    }
    hostDraw.record(
        *commandList.Get(), width, height,
        {124.0F, 124.0F, 128.0F, 128.0F},
        {0.0F, 1.0F, 0.0F, 1.0F});
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
    const henia::test::Rgba8 beforeMarker = pixels[static_cast<std::size_t>(1) * width + 1];
    const henia::test::Rgba8 afterMarker = pixels[static_cast<std::size_t>(126) * width + 126];
    if (redEdge.red < 100 || redEdge.red <= redEdge.green * 2U
        || nearPlaneEdge.green < 100
        || nearPlaneEdge.green <= nearPlaneEdge.red * 2U
        || beforeMarker.red < 240 || beforeMarker.green > 8
        || afterMarker.green < 240 || afterMarker.red > 8) {
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
        || statistics.uploadedInstanceBytes != boxes.size() * sizeof(BoxInstance)
        || statistics.instanceCopyOperations != 1
        || statistics.copiedInstanceBytes != statistics.uploadedInstanceBytes
        || statistics.uploadHeapReadBytes != 0
        || statistics.gpuLocalResidentBytes
            != rendererConfiguration.boxCapacity * sizeof(BoxInstance)
                * rendererConfiguration.submissionCapacity
        || statistics.gpuLocalFrames != 2 || statistics.directUploadFrames != 0
        || statistics.viewUpdates != 2 || statistics.commandListValidationFailures != 1
        || statistics.submissionFenceChecks != 2
        || statistics.submissionSlotBusyRejections != 1
        || statistics.deviceRemovalRejections != 0
        || statistics.frameAttempts != statistics.successfulFrames + statistics.rejectedFrames
        || statistics.profile.cumulative.samples != statistics.successfulFrames
        || statistics.profile.cumulative.producerBuilds != 1
        || statistics.profile.latestSample.identity.producerIdentity != batch.identity()
        || statistics.profile.latestSample.identity.producerRevision != batch.revision()
        || statistics.profile.latestSample.identity.submissionSlot != 0
        || statistics.profile.latestSample.uploadKind != henia::InstanceUploadKind::None
        || statistics.profile.latestSample.gpuTimingAvailable) {
        fail("D3D12 gfx stable-frame statistics are incorrect");
    }
    const std::uint64_t currentProfileSampleId =
        statistics.profile.latestSample.identity.sampleId;
    if (!renderer.reportGpuTime(firstProfileSampleId, 1234)
        || renderer.reportGpuTime(firstProfileSampleId, 5678)) {
        fail("D3D12 gfx delayed/duplicate GPU profile reporting is incorrect");
    }
    statistics = renderer.statistics();
    if (statistics.profile.latestSample.identity.sampleId != currentProfileSampleId
        || statistics.profile.latestSample.gpuTimingAvailable
        || statistics.profile.latestGpuSample.identity.sampleId != firstProfileSampleId
        || statistics.profile.latestGpuSample.identity.producerRevision != batch.revision()
        || statistics.profile.latestGpuSample.identity.submissionSlot != 0
        || statistics.profile.latestGpuSample.gpuNanoseconds != 1234) {
        fail("D3D12 gfx delayed GPU sample lost its frame association");
    }

    D3D12RenderDevice clipRenderer;
    if (!clipRenderer.initialize(*device.Get(), {
            .boxCapacity = 1,
            .submissionCapacity = 1,
            .renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM,
        })) {
        fail("D3D12 gfx clip-sweep renderer did not initialize");
    }
    const auto renderGfxFrame = [&](const InstanceBatch& batch, const ViewParameters& frameView) {
        if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
            fail("Unable to reset the D3D12 gfx clip-sweep command list");
        }
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &barrier);
        commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
        commandList->ClearRenderTargetView(renderTarget, black.data(), 0, nullptr);
        if (!clipRenderer.record(
                batch,
                frameView,
                *commandList.Get(),
                0)) {
            std::cerr << clipRenderer.lastError() << '\n';
            fail("D3D12 gfx visual-regression recording failed");
        }
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList->ResourceBarrier(1, &barrier);
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        if (FAILED(commandList->Close())) {
            fail("Unable to close the D3D12 gfx clip-sweep command list");
        }
        queue->ExecuteCommandLists(1, lists);
        if (!waitForQueue(*device.Get(), *queue.Get())) {
            fail("D3D12 gfx clip-sweep queue timed out");
        }

        void* clipMappedMemory = nullptr;
        if (FAILED(readback->Map(0, &readRange, &clipMappedMemory))
            || clipMappedMemory == nullptr) {
            fail("Unable to map the D3D12 gfx clip-sweep readback buffer");
        }
        const auto* clipMappedPixels = static_cast<const std::byte*>(clipMappedMemory);
        std::vector<henia::test::Rgba8> clipPixels(static_cast<std::size_t>(width) * height);
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const std::size_t sourceOffset = footprint.Offset
                    + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch
                    + static_cast<std::size_t>(x) * 4U;
                clipPixels[static_cast<std::size_t>(y) * width + x] = {
                    static_cast<std::uint8_t>(clipMappedPixels[sourceOffset]),
                    static_cast<std::uint8_t>(clipMappedPixels[sourceOffset + 1U]),
                    static_cast<std::uint8_t>(clipMappedPixels[sourceOffset + 2U]),
                    static_cast<std::uint8_t>(clipMappedPixels[sourceOffset + 3U]),
                };
            }
        }
        readback->Unmap(0, nullptr);
        return clipPixels;
    };
    constexpr std::array depthRanges{
        ClipDepthRange::ZeroToOne,
        ClipDepthRange::MinusOneToOne,
    };
    for (const ClipDepthRange depthRange : depthRanges) {
        const std::string_view rangeName = depthRange == ClipDepthRange::ZeroToOne
            ? "zero-to-one"
            : "minus-one-to-one";
        for (const henia::test::GfxClipSweep& sweep : henia::test::kGfxClipSweeps) {
            for (const henia::test::GfxClipFrame& frameValue : sweep.frames) {
                const std::vector<henia::test::Rgba8> clipPixels = renderGfxFrame(
                    henia::test::gfxClipBatch(frameValue.box),
                    henia::test::gfxClipView(depthRange));
                if (!henia::test::matchesGfxClipFrame(clipPixels, frameValue.position)) {
                    const std::string filename = "d3d12-gfx-clip-" + std::string(sweep.plane)
                        + '-' + std::string(rangeName) + "-actual.ppm";
                    henia::test::writePpm(filename, clipPixels, width, height);
                    std::cerr << "D3D12 gfx clip sweep failed at " << sweep.plane << " ("
                              << rangeName << "), visible pixels="
                              << henia::test::visibleGfxPixelCount(clipPixels) << '\n';
                    return EXIT_FAILURE;
                }
            }
        }
        const std::vector<henia::test::Rgba8> cameraPixels = renderGfxFrame(
            henia::test::gfxClipBatch(henia::test::kGfxCameraCrossingBox),
            henia::test::gfxClipView(depthRange));
        if (!henia::test::matchesGfxCameraCrossing(cameraPixels)) {
            const std::string filename = "d3d12-gfx-camera-crossing-"
                + std::string(rangeName) + "-actual.ppm";
            henia::test::writePpm(filename, cameraPixels, width, height);
            std::cerr << "D3D12 camera-crossing edges were not shortened continuously ("
                      << rangeName << "), visible pixels="
                      << henia::test::visibleGfxPixelCount(cameraPixels) << '\n';
            return EXIT_FAILURE;
        }
    }
    for (const henia::test::GfxAaCase& aaCase : henia::test::kGfxAaCases) {
        const std::vector<henia::test::Rgba8> aaPixels = renderGfxFrame(
            henia::test::gfxAaBatch(aaCase),
            henia::test::gfxAaView());
        if (!henia::test::matchesGfxAaCase(aaPixels, aaCase)) {
            const std::string filename = "d3d12-gfx-aa-" + std::string(aaCase.name)
                + "-actual.ppm";
            henia::test::writePpm(filename, aaPixels, width, height);
            std::cerr << "D3D12 gfx AA golden failed for " << aaCase.name << '\n';
            return EXIT_FAILURE;
        }
    }
    const D3D12GfxStatistics automaticStatistics = clipRenderer.statistics();
    if (automaticStatistics.instanceCopyOperations != 0
        || automaticStatistics.copiedInstanceBytes != 0
        || automaticStatistics.gpuLocalResidentBytes != 0
        || automaticStatistics.gpuLocalFrames != 0
        || automaticStatistics.directUploadFrames == 0
        || automaticStatistics.uploadHeapReadBytes == 0
        || (automaticStatistics.adapterArchitectureKnown
            && !automaticStatistics.adapterUma)) {
        fail("D3D12 gfx automatic storage did not keep WARP/UMA frames on upload memory");
    }
    clipRenderer.shutdown();

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
    if (statistics.partialInstanceUploads != 1 || statistics.drawCalls != 3
        || statistics.uploadedInstanceBytes != (boxes.size() + 1U) * sizeof(BoxInstance)
        || statistics.instanceCopyOperations != 2
        || statistics.copiedInstanceBytes != statistics.uploadedInstanceBytes) {
        fail("D3D12 gfx did not upload only the changed instance range");
    }

    BoxInstance sparseLeft = partial.boxes()[17];
    BoxInstance sparseRight = partial.boxes()[1900];
    sparseLeft.hueOffset = 17.0F;
    sparseRight.hueOffset = 19.0F;
    static_cast<void>(builder.updateBox(17, sparseLeft));
    static_cast<void>(builder.updateBox(1900, sparseRight));
    const InstanceBatch sparse = builder.snapshot();
    if (sparse.dirtyRanges().size() != 2
        || sparse.dirtyRanges()[0] != DirtyRange{17, 1}
        || sparse.dirtyRanges()[1] != DirtyRange{1900, 1}) {
        fail("D3D12 gfx sparse snapshot lost its independent dirty ranges");
    }
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset the D3D12 gfx sparse command list");
    }
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(sparse, view, *commandList.Get(), 0) || FAILED(commandList->Close())) {
        fail("D3D12 gfx sparse-update recording failed");
    }
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) fail("D3D12 gfx sparse-update queue timed out");
    statistics = renderer.statistics();
    if (statistics.partialInstanceUploads != 2 || statistics.drawCalls != 4
        || statistics.uploadedInstanceBytes != (boxes.size() + 3U) * sizeof(BoxInstance)
        || statistics.instanceCopyOperations != 4
        || statistics.copiedInstanceBytes != statistics.uploadedInstanceBytes) {
        fail("D3D12 gfx sparse update copied the bounding interval instead of two boxes");
    }

    ShapeBatch3D visibilityBuilder;
    std::vector<BoxInstance> visibilityBoxes(boxes.size(), {
        .minimum = {4.0F, 4.0F, 0.25F},
        .lineWidth = 4.0F,
        .maximum = {4.5F, 4.5F, 0.75F},
        .color = {1.0F, 0.0F, 0.0F, 1.0F},
    });
    visibilityBoxes[0] = {
        .minimum = {-0.5F, -0.5F, 0.25F},
        .lineWidth = 4.0F,
        .maximum = {0.5F, 0.5F, 0.75F},
        .hueOffset = 0.75F,
        .color = {0.2F, 0.8F, 0.4F, 0.9F},
        .effects = BoxEffect::HueCycle,
    };
    if (!visibilityBuilder.replaceBoxes(visibilityBoxes)) {
        fail("D3D12 gfx visibility fixture was rejected");
    }
    const InstanceBatch visibilityBatch = visibilityBuilder.snapshot();
    ViewParameters visibilityView{
        .viewport = {static_cast<float>(width), static_cast<float>(height)},
    };
    const D3D12GfxStatistics beforeVisibility = renderer.statistics();
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset the D3D12 gfx indirect command list");
    }
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(
            visibilityBatch,
            visibilityView,
            *commandList.Get(),
            0,
            {.mode = VisibilityMode::CpuFrustum})
        || FAILED(commandList->Close())) {
        std::cerr << renderer.lastError() << '\n';
        fail("D3D12 gfx CPU-culling/indirect recording failed");
    }
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) {
        fail("D3D12 gfx indirect queue timed out");
    }
    D3D12GfxStatistics afterVisibility = renderer.statistics();
    if (afterVisibility.cpuCulledFrames != beforeVisibility.cpuCulledFrames + 1U
        || afterVisibility.indirectDrawCalls != beforeVisibility.indirectDrawCalls + 1U
        || afterVisibility.indirectArgumentUpdates
            != beforeVisibility.indirectArgumentUpdates + 1U
        || afterVisibility.visibilitySourceInstances != visibilityBoxes.size()
        || afterVisibility.visibilityRejectedInstances != visibilityBoxes.size() - 1U
        || afterVisibility.submittedInstances != beforeVisibility.submittedInstances + 1U
        || afterVisibility.fullInstanceUploads != beforeVisibility.fullInstanceUploads + 1U
        || afterVisibility.uploadedInstanceBytes
            != beforeVisibility.uploadedInstanceBytes + sizeof(BoxInstance)
        || afterVisibility.instanceCopyOperations
            != beforeVisibility.instanceCopyOperations + 1U) {
        fail("D3D12 gfx CPU culling did not compact the indirect submission");
    }

    visibilityView.timeSeconds = 10.0F;
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset the D3D12 gfx cached-indirect command list");
    }
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(
            visibilityBatch,
            visibilityView,
            *commandList.Get(),
            0,
            {.mode = VisibilityMode::CpuFrustum})
        || FAILED(commandList->Close())) {
        fail("D3D12 gfx cached CPU visibility recording failed");
    }
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) {
        fail("D3D12 gfx cached indirect queue timed out");
    }
    const D3D12GfxStatistics cachedVisibility = renderer.statistics();
    if (cachedVisibility.indirectDrawCalls != afterVisibility.indirectDrawCalls + 1U
        || cachedVisibility.indirectArgumentUpdates
            != afterVisibility.indirectArgumentUpdates + 1U
        || cachedVisibility.visibilityResultReuses
            != afterVisibility.visibilityResultReuses + 1U
        || cachedVisibility.submittedInstances != afterVisibility.submittedInstances + 1U
        || cachedVisibility.fullInstanceUploads != afterVisibility.fullInstanceUploads
        || cachedVisibility.uploadedInstanceBytes != afterVisibility.uploadedInstanceBytes
        || cachedVisibility.instanceCopyOperations != afterVisibility.instanceCopyOperations) {
        fail("D3D12 gfx time-only frame rebuilt cached visibility or instance data");
    }

    if (!henia::test::verifyD3D12Validation(*device.Get())) {
        fail("D3D12 gfx validation reported an error");
    }

    renderer.shutdown();
    if (!renderer.initialize(*device.Get(), rendererConfiguration) || !renderer.initialized()) {
        fail("D3D12 gfx renderer did not initialize after an orderly shutdown");
    }
    renderer.shutdown();
    std::cout << "HeniaUI D3D12 gfx WARP test passed\n";
    return EXIT_SUCCESS;
}
