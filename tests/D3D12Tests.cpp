#include "henia/ui/Frame.h"
#include "henia/ui/backend/d3d12/D3D12Renderer.h"
#include "henia/ui/resource/TextureStore.h"

#include "../src/backend/FixedError.h"
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
#include <type_traits>
#include <vector>

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

    if (!henia::test::enableD3D12Validation()) {
        fail("Unable to enable requested D3D12 validation");
    }

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
    henia::test::D3D12HostDraw hostDraw;
    if (!hostDraw.initialize(*device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM)) {
        fail("Unable to create the D3D12 host-state contract pipeline");
    }

    TextureStore textures;
    Frame frame;
    const RenderPacket packet = henia::test::buildUiVisualScene(textures, frame);
    const TextureHandle atlas{1};
    std::array<std::byte, 16> alpha{};
    alpha.fill(std::byte{0xFF});

    D3D12Renderer oversizedRenderer;
    if (oversizedRenderer.initialize(
            *device.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            {.instanceCapacity = static_cast<std::size_t>(std::numeric_limits<UINT>::max())
                    / sizeof(DrawInstance) + 1U})) {
        fail("D3D12 renderer accepted a buffer-view byte capacity above UINT");
    }
    D3D12Renderer renderer;
    const D3D12RendererConfiguration rendererConfiguration{
        .instanceCapacity = 128,
        .submissionCapacity = 2,
        .batchCapacity = 8,
        .textureCapacity = 1,
        .textureUploadBatchCapacity = 3,
    };
    if (!renderer.initialize(
            *device.Get(),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            rendererConfiguration)
        || !renderer.synchronizeTextures(textures, *queue.Get())) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    D3D12Renderer peerRenderer;
    D3D12RendererConfiguration changedConfiguration = rendererConfiguration;
    changedConfiguration.batchCapacity = 7;
    if (!peerRenderer.initialize(
            *device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, rendererConfiguration)
        || !peerRenderer.initialize(
            *device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, rendererConfiguration)
        || peerRenderer.initialize(
            *device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, changedConfiguration)
        || peerRenderer.lastError()
            != "D3D12 renderer is already initialized with a different configuration"
        || !peerRenderer.initialized()) {
        fail("D3D12 renderer lifecycle/configuration validation failed");
    }
    peerRenderer.shutdown();
    if (!peerRenderer.initialize(
            *device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, rendererConfiguration)
        || !peerRenderer.initialized()) {
        fail("D3D12 renderer could not be recreated beside another live instance");
    }
    peerRenderer.shutdown();
    if (renderer.pendingTextureUploadBatches() != 1
        || renderer.statistics().textureUploads != 0) {
        fail("D3D12 texture upload was committed before fence completion");
    }
    if (!waitForQueue(*device.Get(), *queue.Get()) || !renderer.pollTextureUploads()
        || renderer.pendingTextureUploadBatches() != 0
        || renderer.statistics().textureUploads != 1) {
        fail("D3D12 texture upload did not commit after fence completion");
    }

    if (packet.instances().size() != 17 || packet.batches().size() != 2) {
        fail("Visual regression scene compiled unexpectedly");
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

    if (renderer.record(
            packet,
            *commandList.Get(),
            0,
            std::numeric_limits<std::uint32_t>::max(),
            height)
        || renderer.lastError() != "viewportWidth/viewportHeight is outside the LONG range") {
        fail("D3D12 renderer accepted an out-of-range viewport");
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
        || renderer.record(packet, *bundleList.Get(), 0, width, height)
        || renderer.lastError() != "D3D12 UI recording requires a DIRECT command list") {
        fail("D3D12 renderer accepted an unsupported command-list type");
    }
    ComPtr<ID3D12Device> otherDevice;
    ComPtr<ID3D12CommandAllocator> otherAllocator;
    ComPtr<ID3D12GraphicsCommandList> otherCommandList;
    std::uint64_t expectedCommandListValidationFailures = 1;
    if (FAILED(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&otherDevice)))
        || FAILED(otherDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&otherAllocator)))
        || FAILED(otherDevice->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            otherAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&otherCommandList)))) {
        fail("Unable to create the second D3D12 validation device/list");
    }
    ComPtr<IUnknown> deviceIdentity;
    ComPtr<IUnknown> otherIdentity;
    if (FAILED(device.As(&deviceIdentity)) || FAILED(otherDevice.As(&otherIdentity))) {
        fail("Unable to query D3D12 device identities");
    }
    // Some runtimes return the same cached WARP device for repeated creation.
    // Exercise the mismatch path only when the returned COM identities differ.
    if (deviceIdentity.Get() != otherIdentity.Get()) {
        if (renderer.record(packet, *otherCommandList.Get(), 0, width, height)
            || renderer.lastError() != "D3D12 UI command list belongs to a different device") {
            fail("D3D12 renderer accepted a command list from another device");
        }
        ++expectedCommandListValidationFailures;
    }
    const D3D12RenderStatistics invalidStatistics = renderer.statistics();
    if (invalidStatistics.invalidInputFrames != 1
        || invalidStatistics.capacityRejectedFrames != 0
        || invalidStatistics.drawCalls != 0 || invalidStatistics.instanceUploads != 0) {
        fail("D3D12 invalid-input rejection issued work or used capacity statistics");
    }

    Frame offscreenFrame;
    Canvas& offscreenCanvas = offscreenFrame.begin();
    {
        Canvas::ClipScope clip = offscreenCanvas.scopedClip(
            {{200.25F, 200.25F}, {220.75F, 220.75F}});
        if (!clip.active()) fail("D3D12 off-screen clip setup failed");
        offscreenCanvas.fillRect(
            {{202.0F, 202.0F}, {218.0F, 218.0F}},
            {1.0F, 1.0F, 1.0F, 1.0F});
    }
    const RenderPacket offscreenPacket = offscreenFrame.finish();
    if (offscreenPacket.instances().size() != 1 || offscreenPacket.batches().size() != 1) {
        fail("D3D12 off-screen scissor packet compiled unexpectedly");
    }
    const D3D12RenderStatistics beforeOffscreen = renderer.statistics();
    if (!renderer.record(offscreenPacket, *commandList.Get(), 1, width, height)) {
        fail("D3D12 rejected a valid fully off-screen scissor");
    }
    const D3D12RenderStatistics afterOffscreen = renderer.statistics();
    if (afterOffscreen.recordedFrames != beforeOffscreen.recordedFrames + 1U
        || afterOffscreen.drawCalls != beforeOffscreen.drawCalls
        || afterOffscreen.instanceUploads != beforeOffscreen.instanceUploads) {
        fail("D3D12 submitted or uploaded a fully off-screen scissor batch");
    }

    ComPtr<ID3D12Fence> submissionReuseFence;
    if (FAILED(device->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&submissionReuseFence)))) {
        fail("Unable to create the D3D12 submission-reuse fence");
    }
    const henia::backend::d3d12::SubmissionReuse submissionReuse{
        .completionFence = submissionReuseFence.Get(),
        .completionValue = 1,
    };
    if (renderer.record(packet, *commandList.Get(), 0, width, height, submissionReuse)
        || renderer.lastError() != "D3D12 submission slot is still referenced by the GPU") {
        fail("D3D12 renderer overwrote a fence-busy submission slot");
    }
    if (renderer.statistics().instanceUploads != 0
        || renderer.statistics().drawCalls != 0) {
        fail("D3D12 fence-busy rejection touched submission resources");
    }
    if (FAILED(queue->Signal(submissionReuseFence.Get(), 1))
        || !waitForQueue(*device.Get(), *queue.Get())) {
        fail("D3D12 submission-reuse fence did not complete");
    }

    constexpr std::array<float, 4> clearColor{0.0F, 0.0F, 0.0F, 1.0F};
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    commandList->ClearRenderTargetView(renderTarget, clearColor.data(), 0, nullptr);
    hostDraw.record(
        *commandList.Get(), width, height,
        {0.0F, 0.0F, 4.0F, 4.0F},
        {1.0F, 0.0F, 0.0F, 1.0F});
    if (!renderer.record(packet, *commandList.Get(), 0, width, height, submissionReuse)) {
        std::cerr << renderer.lastError() << '\n';
        return EXIT_FAILURE;
    }
    // Henia deliberately does not restore command-list state. This full host
    // rebind includes its descriptor heap, root state, PSO, IA, viewport, and
    // scissor before the draw that follows Henia.
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
    std::vector<henia::test::Rgba8> pixels(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t sourceOffset = footprint.Offset
                + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch
                + static_cast<std::size_t>(x) * 4U;
            pixels[static_cast<std::size_t>(y) * width + x] = {
                static_cast<std::uint8_t>(mapped[sourceOffset]),
                static_cast<std::uint8_t>(mapped[sourceOffset + 1U]),
                static_cast<std::uint8_t>(mapped[sourceOffset + 2U]),
                static_cast<std::uint8_t>(mapped[sourceOffset + 3U]),
            };
        }
    }
    readback->Unmap(0, nullptr);
    const henia::test::Rgba8 beforeMarker = pixels[static_cast<std::size_t>(1) * width + 1];
    const henia::test::Rgba8 afterMarker = pixels[static_cast<std::size_t>(126) * width + 126];
    if (beforeMarker.red < 240 || beforeMarker.green > 8
        || afterMarker.green < 240 || afterMarker.red > 8) {
        fail("Host draws before/after Henia did not survive explicit state rebinding");
    }
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 4; ++x) {
            pixels[static_cast<std::size_t>(y) * width + x] = {0, 0, 0, 255};
            pixels[static_cast<std::size_t>(height - 1U - y) * width + width - 1U - x]
                = {0, 0, 0, 255};
        }
    }
    if (!henia::test::matchesUiGolden(pixels, width, height)) {
        henia::test::writePpm("d3d12-ui-actual.ppm", pixels, width, height);
        fail("D3D12 output exceeded the documented golden-image tolerance");
    }

    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset D3D12 recording objects for descriptor-cache validation");
    }
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(packet, *commandList.Get(), 0, width, height)
        || FAILED(commandList->Close())) {
        fail("D3D12 descriptor-table cache validation recording failed");
    }

    Frame textureFreeFrame;
    Canvas& textureFreeCanvas = textureFreeFrame.begin();
    textureFreeCanvas.fillRect({{16.0F, 16.0F}, {32.0F, 32.0F}}, {1.0F, 1.0F, 1.0F, 1.0F});
    const RenderPacket textureFreePacket = textureFreeFrame.finish();
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset D3D12 recording objects for texture-free validation");
    }
    commandList->ResourceBarrier(1, &barrier);
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(textureFreePacket, *commandList.Get(), 0, width, height)
        || FAILED(commandList->Close())) {
        fail("D3D12 texture-free heap-isolation recording failed");
    }

    const D3D12RenderStatistics statistics = renderer.statistics();
    if (statistics.drawCalls != packet.batches().size() * 2U + 1U
        || statistics.submittedInstances != packet.instances().size() * 2U + 1U
        || statistics.textureUploads != 1 || statistics.textureUploadBatches != 1
        || statistics.instanceUploads != 2 || statistics.descriptorHeapBindings != 2
        || statistics.descriptorTableCopies != packet.batches().size()
        || statistics.descriptorTableCacheHits != packet.batches().size()
        || statistics.textureFreeFrames != 1
        || statistics.submissionFenceChecks != 2
        || statistics.submissionSlotBusyRejections != 1
        || statistics.deviceRemovalRejections != 0
        || statistics.commandListValidationFailures != expectedCommandListValidationFailures) {
        fail("D3D12 renderer statistics are incorrect");
    }

    alpha.fill(std::byte{0x80});
    const std::array<std::byte, 1> partialAlpha{std::byte{0x80}};
    if (!textures.updateRegion(atlas, {1, 1, 1, 1}, 1, partialAlpha)
        || !renderer.synchronizeTextures(textures, *queue.Get())) {
        fail("D3D12 renderer could not queue a partial texture revision");
    }
    alpha.fill(std::byte{0x40});
    if (!textures.update(atlas, 4, alpha)
        || !renderer.synchronizeTextures(textures, *queue.Get())) {
        fail("D3D12 renderer did not keep repeated revisions pending transactionally");
    }
    const D3D12RenderStatistics queuedStatistics = renderer.statistics();
    const std::uint32_t pendingRevisions = renderer.pendingTextureUploadBatches();
    if (pendingRevisions == 0
        || queuedStatistics.textureUploads + pendingRevisions != 3) {
        fail("D3D12 renderer lost a completed or pending texture revision");
    }
    if (!waitForQueue(*device.Get(), *queue.Get()) || !renderer.pollTextureUploads()
        || renderer.pendingTextureUploadBatches() != 0
        || renderer.statistics().textureUploads != 3
        || renderer.statistics().fullTextureUploads != 2
        || renderer.statistics().partialTextureUploads != 1
        || renderer.statistics().uploadedTextureBytes != 33) {
        fail("D3D12 renderer did not commit repeated revisions in fence order");
    }

    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))) {
        fail("Unable to reset D3D12 recording objects for the updated texture");
    }
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList->ResourceBarrier(1, &barrier);
    commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
    if (!renderer.record(packet, *commandList.Get(), 1, width, height)
        || FAILED(commandList->Close())) {
        fail("D3D12 renderer could not record an updated texture in another submission slot");
    }
    queue->ExecuteCommandLists(1, lists);
    if (!waitForQueue(*device.Get(), *queue.Get())) {
        fail("D3D12 updated-texture submission timed out");
    }

    const D3D12RenderStatistics updatedStatistics = renderer.statistics();
    if (updatedStatistics.drawCalls != packet.batches().size() * 3U + 1U
        || updatedStatistics.submittedInstances != packet.instances().size() * 3U + 1U
        || updatedStatistics.instanceUploads != 3
        || updatedStatistics.textureUploads != 3
        || updatedStatistics.textureUploadBatches != 3
        || updatedStatistics.failedTextureUploadBatches != 0
        || updatedStatistics.descriptorHeapBindings != 3
        || updatedStatistics.descriptorTableCopies != packet.batches().size() * 2U
        || updatedStatistics.descriptorTableCacheHits != packet.batches().size()
        || updatedStatistics.textureFreeFrames != 1) {
        fail("D3D12 repeated texture update statistics are incorrect");
    }

    if (!textures.destroy(atlas)
        || !renderer.synchronizeTextures(textures, *queue.Get())
        || renderer.statistics().retiredTextures == 0) {
        fail("D3D12 renderer did not retire a destroyed texture generation");
    }
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))
        || renderer.record(packet, *commandList.Get(), 0, width, height)) {
        fail("D3D12 renderer accepted a packet holding a destroyed texture generation");
    }
    const TextureHandle replacementAtlas = textures.create(
        TextureFormat::Alpha8, 4, 4, 4, alpha);
    if (!replacementAtlas.valid() || replacementAtlas.value() != atlas.value()
        || replacementAtlas.generation() == atlas.generation()
        || !renderer.synchronizeTextures(textures, *queue.Get())
        || !waitForQueue(*device.Get(), *queue.Get())
        || !renderer.pollTextureUploads()) {
        fail("D3D12 renderer could not synchronize a reused texture slot");
    }
    Frame replacementFrame;
    replacementFrame.begin().image(
        replacementAtlas, {{0.0F, 0.0F}, {16.0F, 16.0F}});
    const RenderPacket replacementPacket = replacementFrame.finish();
    if (!renderer.record(replacementPacket, *commandList.Get(), 0, width, height)
        || FAILED(commandList->Close())) {
        fail("D3D12 renderer rejected the replacement texture generation");
    }

    if (!textures.destroy(replacementAtlas)
        || !renderer.synchronizeTextures(textures, *queue.Get())) {
        fail("D3D12 external texture fixture could not retire its CPU predecessor");
    }
    const TextureHandle externalHandle = textures.createExternal(
        TextureFormat::Alpha8, 4, 4);
    D3D12_RESOURCE_DESC externalDescription{};
    externalDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    externalDescription.Width = 4;
    externalDescription.Height = 4;
    externalDescription.DepthOrArraySize = 1;
    externalDescription.MipLevels = 1;
    externalDescription.Format = DXGI_FORMAT_R8_UNORM;
    externalDescription.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> externalResource;
    if (FAILED(device->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &externalDescription,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr,
            IID_PPV_ARGS(&externalResource)))
        || !renderer.bindExternalTexture(textures, externalHandle, *externalResource.Get())) {
        fail("D3D12 renderer rejected a compatible external texture");
    }
    externalResource.Reset();
    Frame externalFrame;
    externalFrame.begin().image(externalHandle, {{0.0F, 0.0F}, {16.0F, 16.0F}});
    if (FAILED(allocator->Reset()) || FAILED(commandList->Reset(allocator.Get(), nullptr))
        || !renderer.record(externalFrame.finish(), *commandList.Get(), 0, width, height)
        || FAILED(commandList->Close())
        || renderer.statistics().externalTextures != 1) {
        fail("D3D12 renderer did not retain a host-provided texture safely");
    }

    const TextureHandle overflowTexture = textures.create(TextureFormat::Alpha8, 4, 4, 4, alpha);
    if (!overflowTexture.valid() || renderer.synchronizeTextures(textures, *queue.Get())
        || renderer.lastError() != "D3D12 texture store exceeds configured capacity") {
        fail("D3D12 texture bookkeeping overflow was not rejected deterministically");
    }

    if (!henia::test::verifyD3D12Validation(*device.Get())) {
        fail("D3D12 validation reported an error");
    }

    renderer.shutdown();
    if (!renderer.initialize(
            *device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, rendererConfiguration)
        || !renderer.initialized()) {
        fail("D3D12 renderer did not initialize after an orderly shutdown");
    }
    renderer.shutdown();
    std::cout << "HeniaUI D3D12 WARP test passed\n";
    return EXIT_SUCCESS;
}
