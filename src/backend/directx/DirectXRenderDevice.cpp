#include "henia/gfx/backend/directx/DirectXRenderDevice.h"

#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"

#include "../FixedError.h"
#include "GfxShaders.generated.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>

namespace henia::gfx {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kDepthStateCount = 15;
constexpr std::size_t kFaceIndexCount = 36;
constexpr std::size_t kEdgeIndexCount = 72;
constexpr auto kBoxIndices = [] {
    std::array<std::uint16_t, kFaceIndexCount + kEdgeIndexCount> result{};
    constexpr std::array<std::uint16_t, 6> pattern{0, 1, 2, 2, 3, 0};
    for (std::uint16_t face = 0; face < 6; ++face) {
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            result[static_cast<std::size_t>(face) * pattern.size() + index] =
                static_cast<std::uint16_t>(48U + face * 4U + pattern[index]);
        }
    }
    for (std::uint16_t edge = 0; edge < 12; ++edge) {
        for (std::size_t index = 0; index < pattern.size(); ++index) {
            result[kFaceIndexCount + static_cast<std::size_t>(edge) * pattern.size() + index] =
                static_cast<std::uint16_t>(edge * 4U + pattern[index]);
        }
    }
    return result;
}();

struct alignas(16) GfxFrameConstants final {
    std::array<float, 16> viewProjection{};
    std::array<float, 2> viewport{};
    float timeSeconds = 0.0F;
    float motionScale = 0.0F;
    std::uint32_t flags = 0;
    std::array<std::uint32_t, 3> padding{};
};
static_assert(sizeof(GfxFrameConstants) == 96);

[[nodiscard]] bool compileGfxShader(
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& output,
    henia::detail::FixedError& error) noexcept {
    ComPtr<ID3DBlob> errors;
    const HRESULT status = D3DCompile(
        henia::backend::d3d12::generated::gfx::kSource,
        sizeof(henia::backend::d3d12::generated::gfx::kSource) - 1U,
        "HeniaUI.DirectX.D3D11.Gfx",
        nullptr,
        nullptr,
        entry,
        target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &output,
        &errors);
    if (SUCCEEDED(status)) return true;
    if (errors != nullptr && errors->GetBufferPointer() != nullptr) {
        error.assign(
            static_cast<const char*>(errors->GetBufferPointer()),
            errors->GetBufferSize());
    } else {
        error = "D3D11 gfx shader compilation failed";
    }
    return false;
}

[[nodiscard]] bool sameComIdentity(IUnknown& left, IUnknown& right) noexcept {
    ComPtr<IUnknown> leftIdentity;
    ComPtr<IUnknown> rightIdentity;
    return SUCCEEDED(left.QueryInterface(IID_PPV_ARGS(&leftIdentity)))
        && SUCCEEDED(right.QueryInterface(IID_PPV_ARGS(&rightIdentity)))
        && leftIdentity.Get() == rightIdentity.Get();
}

[[nodiscard]] std::size_t depthStateIndex(DepthState state) noexcept {
    if (!state.enabled) return 0;
    return 1U + static_cast<std::size_t>(state.compare) * 2U
        + (state.writeEnabled ? 1U : 0U);
}

[[nodiscard]] D3D11_COMPARISON_FUNC comparisonFunction(CompareOp operation) noexcept {
    switch (operation) {
        case CompareOp::Never: return D3D11_COMPARISON_NEVER;
        case CompareOp::Less: return D3D11_COMPARISON_LESS;
        case CompareOp::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
        case CompareOp::Equal: return D3D11_COMPARISON_EQUAL;
        case CompareOp::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case CompareOp::Greater: return D3D11_COMPARISON_GREATER;
        case CompareOp::Always: return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_LESS_EQUAL;
}

class D3D11GfxRenderer final {
public:
    [[nodiscard]] bool initialize(
        ID3D11Device& requestedDevice,
        ID3D11DeviceContext& requestedContext,
        D3D11GfxConfiguration requested) noexcept {
        if (ready) {
            if (!sameComIdentity(*device.Get(), requestedDevice)
                || !sameComIdentity(*context.Get(), requestedContext)
                || requested.boxCapacity != configuration.boxCapacity
                || requested.renderTargetFormat != configuration.renderTargetFormat
                || requested.depthStencilFormat != configuration.depthStencilFormat
                || requested.sampleCount != configuration.sampleCount
                || requested.sampleQuality != configuration.sampleQuality) {
                error = "D3D11 gfx renderer is already initialized with a different owner or configuration";
                return false;
            }
            error.clear();
            return true;
        }
        std::size_t instanceBytes = 0;
        if (requested.boxCapacity == 0
            || requested.sampleCount == 0
            || requested.renderTargetFormat == DXGI_FORMAT_UNKNOWN
            || !checkedMultiply(
                requested.boxCapacity,
                sizeof(BoxInstance),
                instanceBytes)
            || instanceBytes > std::numeric_limits<UINT>::max()) {
            error = "D3D11 gfx configuration is invalid";
            return false;
        }
        ComPtr<ID3D11Device> contextDevice;
        requestedContext.GetDevice(&contextDevice);
        if (contextDevice == nullptr || !sameComIdentity(requestedDevice, *contextDevice.Get())) {
            error = "D3D11 gfx context belongs to a different device";
            return false;
        }
        if (requestedContext.GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
            error = "D3D11 gfx requires the host immediate context";
            return false;
        }
        if (requestedDevice.GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0) {
            error = "D3D11 gfx requires feature level 11_0";
            return false;
        }
        UINT renderTargetSupport = 0;
        UINT renderTargetQualityLevels = 0;
        if (FAILED(requestedDevice.CheckFormatSupport(
                requested.renderTargetFormat,
                &renderTargetSupport))
            || (renderTargetSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0
            || FAILED(requestedDevice.CheckMultisampleQualityLevels(
                requested.renderTargetFormat,
                requested.sampleCount,
                &renderTargetQualityLevels))
            || renderTargetQualityLevels == 0) {
            error = "D3D11 gfx render-target format/sample count is unsupported";
            return false;
        }
        if (requested.sampleQuality >= renderTargetQualityLevels) {
            error = "D3D11 gfx sampleQuality is outside the render-target supported range";
            return false;
        }
        if (requested.depthStencilFormat != DXGI_FORMAT_UNKNOWN) {
            UINT depthSupport = 0;
            UINT depthQualityLevels = 0;
            if (FAILED(requestedDevice.CheckFormatSupport(
                    requested.depthStencilFormat,
                    &depthSupport))
                || (depthSupport & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) == 0
                || FAILED(requestedDevice.CheckMultisampleQualityLevels(
                    requested.depthStencilFormat,
                    requested.sampleCount,
                    &depthQualityLevels))
                || depthQualityLevels == 0) {
                error = "D3D11 gfx depth format/sample count is unsupported";
                return false;
            }
            if (requested.sampleQuality >= depthQualityLevels) {
                error = "D3D11 gfx sampleQuality is outside the depth-target supported range";
                return false;
            }
        }
        if (!visibilityList.reserve(requested.boxCapacity)) {
            error.assign(visibilityList.lastError().data(), visibilityList.lastError().size());
            return false;
        }

        device = &requestedDevice;
        context = &requestedContext;
        configuration = requested;

        ComPtr<ID3DBlob> vertexBlob;
        ComPtr<ID3DBlob> pixelBlob;
        if (!compileGfxShader("vertexMain", "vs_5_0", vertexBlob, error)
            || !compileGfxShader("pixelMain", "ps_5_0", pixelBlob, error)) {
            shutdown();
            return false;
        }
        if (FAILED(device->CreateVertexShader(
                vertexBlob->GetBufferPointer(),
                vertexBlob->GetBufferSize(),
                nullptr,
                &vertexShader))
            || FAILED(device->CreatePixelShader(
                pixelBlob->GetBufferPointer(),
                pixelBlob->GetBufferSize(),
                nullptr,
                &pixelShader))) {
            error = "D3D11 gfx shader creation failed";
            shutdown();
            return false;
        }
        constexpr std::array inputs{
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_MINIMUM_WIDTH", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_MAXIMUM_HUE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_EFFECTS", 0, DXGI_FORMAT_R32_UINT, 0, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            D3D11_INPUT_ELEMENT_DESC{"INSTANCE_RESERVED", 0, DXGI_FORMAT_R32G32B32_UINT, 0, 52, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        };
        if (FAILED(device->CreateInputLayout(
                inputs.data(),
                static_cast<UINT>(inputs.size()),
                vertexBlob->GetBufferPointer(),
                vertexBlob->GetBufferSize(),
                &inputLayout))) {
            error = "D3D11 gfx input-layout creation failed";
            shutdown();
            return false;
        }

        const D3D11_BUFFER_DESC instancesDescription{
            .ByteWidth = static_cast<UINT>(instanceBytes),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        const D3D11_BUFFER_DESC constantsDescription{
            .ByteWidth = sizeof(GfxFrameConstants),
            .Usage = D3D11_USAGE_DYNAMIC,
            .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        };
        const D3D11_BUFFER_DESC indicesDescription{
            .ByteWidth = sizeof(kBoxIndices),
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_INDEX_BUFFER,
        };
        const D3D11_SUBRESOURCE_DATA indicesData{.pSysMem = kBoxIndices.data()};
        if (FAILED(device->CreateBuffer(&instancesDescription, nullptr, &instanceBuffer))
            || FAILED(device->CreateBuffer(&constantsDescription, nullptr, &frameConstants))
            || FAILED(device->CreateBuffer(&indicesDescription, &indicesData, &indexBuffer))) {
            error = "D3D11 gfx buffer creation failed";
            shutdown();
            return false;
        }

        const D3D11_RASTERIZER_DESC rasterizerDescription{
            .FillMode = D3D11_FILL_SOLID,
            .CullMode = D3D11_CULL_NONE,
            .DepthClipEnable = TRUE,
            .ScissorEnable = TRUE,
            .MultisampleEnable = configuration.sampleCount > 1 ? TRUE : FALSE,
        };
        D3D11_BLEND_DESC blendDescription{};
        D3D11_RENDER_TARGET_BLEND_DESC& blend = blendDescription.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = D3D11_BLEND_ONE;
        blend.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D11_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateRasterizerState(&rasterizerDescription, &rasterizerState))
            || FAILED(device->CreateBlendState(&blendDescription, &blendState))) {
            error = "D3D11 gfx fixed-function state creation failed";
            shutdown();
            return false;
        }
        for (std::size_t index = 0; index < depthStates.size(); ++index) {
            DepthState depth{};
            if (index != 0) {
                depth.enabled = true;
                depth.compare = static_cast<CompareOp>((index - 1U) / 2U);
                depth.writeEnabled = ((index - 1U) % 2U) != 0;
            }
            const D3D11_DEPTH_STENCIL_DESC description{
                .DepthEnable = depth.enabled ? TRUE : FALSE,
                .DepthWriteMask = depth.writeEnabled
                    ? D3D11_DEPTH_WRITE_MASK_ALL
                    : D3D11_DEPTH_WRITE_MASK_ZERO,
                .DepthFunc = comparisonFunction(depth.compare),
            };
            if (FAILED(device->CreateDepthStencilState(&description, &depthStates[index]))) {
                error = "D3D11 gfx depth-state creation failed";
                shutdown();
                return false;
            }
        }
        statistics = {};
        uploadedIdentity = 0;
        uploadedRevision = 0;
        ready = true;
        error.clear();
        return true;
    }

    [[nodiscard]] bool render(
        const InstanceBatch& batch,
        const ViewParameters& view,
        bool depthAttachmentAvailable,
        VisibilityOptions visibility) noexcept {
        ++statistics.frameAttempts;
        const std::string_view viewError = validate(view);
        const std::string_view visibilityError = validate(visibility);
        const std::string_view depthError = validate(batch.depthState());
        if (!ready || !viewError.empty() || !visibilityError.empty() || !depthError.empty()
            || batch.boxes().size() > configuration.boxCapacity) {
            ++statistics.rejectedFrames;
            error = "D3D11 gfx batch, view, visibility, or renderer state is invalid";
            return false;
        }
        if (FAILED(device->GetDeviceRemovedReason())) {
            ++statistics.rejectedFrames;
            error = "D3D11 gfx device has been removed";
            return false;
        }
        for (const BoxInstance& box : batch.boxes()) {
            if (!validate(box).empty()) {
                ++statistics.rejectedFrames;
                error = "D3D11 gfx box instance is invalid";
                return false;
            }
        }

        const bool cpuVisibility = usesCpuVisibility(visibility, batch.boxes().size());
        std::span<const BoxInstance> visibleBoxes{};
        std::uint64_t identity = batch.identity();
        std::uint64_t revision = batch.revision();
        if (cpuVisibility) {
            if (!visibilityList.update(batch, view, visibility)) {
                ++statistics.rejectedFrames;
                error.assign(visibilityList.lastError().data(), visibilityList.lastError().size());
                return false;
            }
            visibleBoxes = visibilityList.boxes();
            identity = visibilityList.identity();
            revision = visibilityList.revision();
        }
        const std::size_t count = cpuVisibility ? visibleBoxes.size() : batch.boxes().size();
        if (uploadedIdentity != identity || uploadedRevision != revision) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(
                    instanceBuffer.Get(),
                    0,
                    D3D11_MAP_WRITE_DISCARD,
                    0,
                    &mapped))
                || mapped.pData == nullptr) {
                ++statistics.rejectedFrames;
                error = "D3D11 gfx instance upload failed";
                return false;
            }
            std::byte* destination = static_cast<std::byte*>(mapped.pData);
            if (cpuVisibility) {
                std::memcpy(destination, visibleBoxes.data(), visibleBoxes.size_bytes());
            } else {
                std::size_t offset = 0;
                for (std::size_t page = 0; page < batch.boxPageCount(); ++page) {
                    const std::span<const BoxInstance> boxes = batch.boxPage(page);
                    const std::size_t bytes = boxes.size_bytes();
                    std::memcpy(destination + offset, boxes.data(), bytes);
                    offset += bytes;
                }
            }
            context->Unmap(instanceBuffer.Get(), 0);
            statistics.uploadedInstanceBytes += count * sizeof(BoxInstance);
            uploadedIdentity = identity;
            uploadedRevision = revision;
        }

        const GfxFrameConstants constants{
            .viewProjection = view.viewProjection.values,
            .viewport = {view.viewport.x, view.viewport.y},
            .timeSeconds = view.timeSeconds,
            .motionScale = view.motionScale,
            .flags = view.clipDepthRange == ClipDepthRange::MinusOneToOne ? 1U : 0U,
        };
        D3D11_MAPPED_SUBRESOURCE mappedConstants{};
        if (FAILED(context->Map(
                frameConstants.Get(),
                0,
                D3D11_MAP_WRITE_DISCARD,
                0,
                &mappedConstants))) {
            ++statistics.rejectedFrames;
            error = "D3D11 gfx frame-constant upload failed";
            return false;
        }
        std::memcpy(mappedConstants.pData, &constants, sizeof(constants));
        context->Unmap(frameConstants.Get(), 0);

        DepthState depth = batch.depthState();
        if (depth.enabled
            && (configuration.depthStencilFormat == DXGI_FORMAT_UNKNOWN
                || !depthAttachmentAvailable)) {
            depth.enabled = false;
            depth.writeEnabled = false;
            ++statistics.depthFallbacks;
        }
        const UINT stride = sizeof(BoxInstance);
        const UINT offset = 0;
        ID3D11Buffer* instances[]{instanceBuffer.Get()};
        ID3D11Buffer* constantsBuffer[]{frameConstants.Get()};
        const D3D11_VIEWPORT nativeViewport{
            .TopLeftX = 0.0F,
            .TopLeftY = 0.0F,
            .Width = view.viewport.x,
            .Height = view.viewport.y,
            .MinDepth = 0.0F,
            .MaxDepth = 1.0F,
        };
        const D3D11_RECT scissor{
            0,
            0,
            static_cast<LONG>(std::ceil(view.viewport.x)),
            static_cast<LONG>(std::ceil(view.viewport.y)),
        };
        context->IASetInputLayout(inputLayout.Get());
        context->IASetVertexBuffers(0, 1, instances, &stride, &offset);
        context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->PSSetShader(pixelShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, constantsBuffer);
        context->PSSetConstantBuffers(0, 1, constantsBuffer);
        context->RSSetState(rasterizerState.Get());
        context->RSSetViewports(1, &nativeViewport);
        context->RSSetScissorRects(1, &scissor);
        context->OMSetBlendState(blendState.Get(), nullptr, 0xFFFFFFFFU);
        context->OMSetDepthStencilState(depthStates[depthStateIndex(depth)].Get(), 0);
        if (count != 0) {
            context->DrawIndexedInstanced(
                static_cast<UINT>(kBoxIndices.size()),
                static_cast<UINT>(count),
                0,
                0,
                0);
            ++statistics.drawCalls;
            statistics.submittedInstances += count;
        }
        ++statistics.successfulFrames;
        error.clear();
        return true;
    }

    void shutdown() noexcept {
        for (ComPtr<ID3D11DepthStencilState>& state : depthStates) state.Reset();
        blendState.Reset();
        rasterizerState.Reset();
        indexBuffer.Reset();
        frameConstants.Reset();
        instanceBuffer.Reset();
        inputLayout.Reset();
        pixelShader.Reset();
        vertexShader.Reset();
        context.Reset();
        device.Reset();
        uploadedIdentity = 0;
        uploadedRevision = 0;
        ready = false;
    }

    [[nodiscard]] bool initialized() const noexcept { return ready; }
    [[nodiscard]] DirectXGfxStatistics publicStatistics() const noexcept {
        DirectXGfxStatistics result{};
        result.backend = henia::backend::directx::Api::D3D11;
        result.frameAttempts = statistics.frameAttempts;
        result.successfulFrames = statistics.successfulFrames;
        result.rejectedFrames = statistics.rejectedFrames;
        result.drawCalls = statistics.drawCalls;
        result.submittedInstances = statistics.submittedInstances;
        result.uploadedInstanceBytes = statistics.uploadedInstanceBytes;
        result.depthFallbacks = statistics.depthFallbacks;
        return result;
    }
    [[nodiscard]] std::string_view lastError() const noexcept { return error.view(); }

private:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11Buffer> instanceBuffer;
    ComPtr<ID3D11Buffer> frameConstants;
    ComPtr<ID3D11Buffer> indexBuffer;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    ComPtr<ID3D11BlendState> blendState;
    std::array<ComPtr<ID3D11DepthStencilState>, kDepthStateCount> depthStates;
    VisibilityList visibilityList;
    D3D11GfxConfiguration configuration{};
    DirectXGfxStatistics statistics{};
    henia::detail::FixedError error;
    std::uint64_t uploadedIdentity = 0;
    std::uint64_t uploadedRevision = 0;
    bool ready = false;
};

} // namespace

struct DirectXRenderDevice::Implementation final {
    std::unique_ptr<D3D11GfxRenderer> d3d11;
    std::unique_ptr<D3D12RenderDevice> d3d12;
    henia::backend::directx::Api active = henia::backend::directx::Api::None;
    henia::detail::FixedError error;

    void shutdown() noexcept {
        if (d3d11 != nullptr) d3d11->shutdown();
        if (d3d12 != nullptr) d3d12->shutdown();
        d3d11.reset();
        d3d12.reset();
        active = henia::backend::directx::Api::None;
    }
};

DirectXRenderDevice::DirectXRenderDevice() : mImplementation(std::make_unique<Implementation>()) {}
DirectXRenderDevice::~DirectXRenderDevice() { shutdown(); }

bool DirectXRenderDevice::initialize(
    ID3D11Device& device,
    ID3D11DeviceContext& context,
    D3D11GfxConfiguration configuration) noexcept {
    try {
        if (mImplementation->active != henia::backend::directx::Api::None
            && mImplementation->active != henia::backend::directx::Api::D3D11) {
            mImplementation->error = "DirectX gfx renderer is already initialized with D3D12";
            return false;
        }
        if (mImplementation->d3d11 == nullptr) {
            mImplementation->d3d11 = std::make_unique<D3D11GfxRenderer>();
        }
        if (!mImplementation->d3d11->initialize(device, context, configuration)) {
            mImplementation->error.assign(
                mImplementation->d3d11->lastError().data(),
                mImplementation->d3d11->lastError().size());
            return false;
        }
        mImplementation->active = henia::backend::directx::Api::D3D11;
        mImplementation->error.clear();
        return true;
    } catch (...) {
        mImplementation->shutdown();
        mImplementation->error = "D3D11 gfx initialization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool DirectXRenderDevice::initialize(
    ID3D12Device& device,
    D3D12GfxConfiguration configuration) noexcept {
    try {
        if (mImplementation->active != henia::backend::directx::Api::None
            && mImplementation->active != henia::backend::directx::Api::D3D12) {
            mImplementation->error = "DirectX gfx renderer is already initialized with D3D11";
            return false;
        }
        if (mImplementation->d3d12 == nullptr) {
            mImplementation->d3d12 = std::make_unique<D3D12RenderDevice>();
        }
        if (!mImplementation->d3d12->initialize(device, configuration)) {
            mImplementation->error.assign(
                mImplementation->d3d12->lastError().data(),
                mImplementation->d3d12->lastError().size());
            return false;
        }
        mImplementation->active = henia::backend::directx::Api::D3D12;
        mImplementation->error.clear();
        return true;
    } catch (...) {
        mImplementation->shutdown();
        mImplementation->error = "D3D12 gfx initialization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool DirectXRenderDevice::render(
    const InstanceBatch& batch,
    const ViewParameters& view,
    bool depthAttachmentAvailable,
    VisibilityOptions visibility) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D11
        || mImplementation->d3d11 == nullptr) {
        mImplementation->error = "DirectX gfx renderer is not using D3D11";
        return false;
    }
    const bool result = mImplementation->d3d11->render(
        batch, view, depthAttachmentAvailable, visibility);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d11->lastError().data(),
            mImplementation->d3d11->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderDevice::record(
    const InstanceBatch& batch,
    const ViewParameters& view,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "DirectX gfx renderer is not using D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->record(
        batch, view, commandList, submissionSlot, submissionReuse);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d12->lastError().data(),
            mImplementation->d3d12->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderDevice::record(
    const InstanceBatch& batch,
    const ViewParameters& view,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    VisibilityOptions visibility,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "DirectX gfx renderer is not using D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->record(
        batch, view, commandList, submissionSlot, visibility, submissionReuse);
    if (!result) {
        mImplementation->error.assign(
            mImplementation->d3d12->lastError().data(),
            mImplementation->d3d12->lastError().size());
    } else {
        mImplementation->error.clear();
    }
    return result;
}

bool DirectXRenderDevice::reportGpuTime(
    std::uint64_t sampleId,
    std::uint64_t nanoseconds) noexcept {
    if (mImplementation->active != henia::backend::directx::Api::D3D12
        || mImplementation->d3d12 == nullptr) {
        mImplementation->error = "GPU timestamp reporting is available only for D3D12";
        return false;
    }
    const bool result = mImplementation->d3d12->reportGpuTime(sampleId, nanoseconds);
    if (!result) {
        mImplementation->error = "D3D12 gfx GPU timestamp sample is unknown or already resolved";
    } else {
        mImplementation->error.clear();
    }
    return result;
}

void DirectXRenderDevice::shutdown() noexcept {
    if (mImplementation != nullptr) mImplementation->shutdown();
}

bool DirectXRenderDevice::initialized() const noexcept {
    if (mImplementation->active == henia::backend::directx::Api::D3D11) {
        return mImplementation->d3d11 != nullptr && mImplementation->d3d11->initialized();
    }
    if (mImplementation->active == henia::backend::directx::Api::D3D12) {
        return mImplementation->d3d12 != nullptr && mImplementation->d3d12->initialized();
    }
    return false;
}

henia::backend::directx::Api DirectXRenderDevice::backend() const noexcept {
    return mImplementation->active;
}

DirectXGfxStatistics DirectXRenderDevice::statistics() const noexcept {
    if (mImplementation->active == henia::backend::directx::Api::D3D11
        && mImplementation->d3d11 != nullptr) {
        return mImplementation->d3d11->publicStatistics();
    }
    DirectXGfxStatistics result{};
    result.backend = mImplementation->active;
    if (mImplementation->active == henia::backend::directx::Api::D3D12
        && mImplementation->d3d12 != nullptr) {
        result.d3d12 = mImplementation->d3d12->statistics();
        result.frameAttempts = result.d3d12.frameAttempts;
        result.successfulFrames = result.d3d12.successfulFrames;
        result.rejectedFrames = result.d3d12.rejectedFrames;
        result.drawCalls = result.d3d12.drawCalls;
        result.submittedInstances = result.d3d12.submittedInstances;
        result.uploadedInstanceBytes = result.d3d12.uploadedInstanceBytes;
        result.depthFallbacks = result.d3d12.depthFallbacks;
    }
    return result;
}

std::string_view DirectXRenderDevice::lastError() const noexcept {
    return mImplementation->error.view();
}

} // namespace henia::gfx
