#include "henia/ui/backend/d3d12/D3D12Renderer.h"
#include "henia/CheckedArithmetic.h"
#include "henia/ui/Validation.h"

#include "D3D12TextureUploadTransaction.h"
#include "../FixedError.h"
#include "../ProfileTimeline.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace henia::ui {
namespace {

using Microsoft::WRL::ComPtr;

static_assert(std::is_standard_layout_v<DrawInstance>);
static_assert(offsetof(DrawInstance, uv) == offsetof(DrawInstance, bounds) + sizeof(Rect));
static_assert(offsetof(DrawInstance, color) == offsetof(DrawInstance, uv) + sizeof(Rect));
static_assert(offsetof(DrawInstance, thickness) == offsetof(DrawInstance, radius) + sizeof(float));
static_assert(offsetof(DrawInstance, lineStyle) == offsetof(DrawInstance, kind) + 3U);

constexpr const char* kShaderSource = R"hlsl(
cbuffer FrameConstants : register(b0) {
    float2 viewportSize;
};

#ifndef HENIA_TEXTURE_FREE
Texture2D textures[8] : register(t0);
SamplerState linearSampler : register(s0);
#endif

struct VertexInput {
    float4 bounds : INSTANCE_BOUNDS;
    float4 uv : INSTANCE_UV;
    float4 color : INSTANCE_COLOR;
    float2 metrics : INSTANCE_METRICS;
    uint4 style : INSTANCE_STYLE;
    uint vertexId : SV_VertexID;
};

struct PixelInput {
    float4 position : SV_Position;
    float2 pixelPosition : PIXEL_POSITION;
    float2 textureUv : TEXTURE_UV;
    float4 tintColor : TINT_COLOR;
    float4 linePoints : LINE_POINTS;
    nointerpolation float4 lineNeighbors : LINE_NEIGHBORS;
    float2 shapeMetrics : SHAPE_METRICS;
    nointerpolation uint textureSlot : TEXTURE_SLOT;
    nointerpolation uint primitiveKind : PRIMITIVE_KIND;
    nointerpolation uint lineCap : LINE_CAP;
    nointerpolation uint lineJoin : LINE_JOIN;
    nointerpolation uint lineFlags : LINE_FLAGS;
};

static const float2 corners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

PixelInput vertexMain(VertexInput input) {
    PixelInput output;
    float2 corner = corners[input.vertexId];
    uint kind = input.style.x;
    uint decodedLineJoin = input.style.w & 1;
    uint decodedLineFlags = (input.style.w >> 1) & 3;
    float2 pixel;
    if (kind == 2) {
        float2 start = input.bounds.xy;
        float2 finish = input.bounds.zw;
        float2 segment = finish - start;
        float segmentLength = length(segment);
        float2 direction = segment / segmentLength;
        float2 normal = float2(-direction.y, direction.x);
        float halfWidth = input.metrics.y * 0.5;
        uint joinMode = decodedLineJoin == 0 ? 0 : 2;
        uint startMode = (decodedLineFlags & 1) != 0 ? joinMode : input.style.z;
        uint endMode = (decodedLineFlags & 2) != 0 ? joinMode : input.style.z;
        float startExtension = (((decodedLineFlags & 1) != 0 || startMode != 0)
            ? halfWidth : 0.0) + 2.0;
        float endExtension = (((decodedLineFlags & 2) != 0 || endMode != 0)
            ? halfWidth : 0.0) + 2.0;
        float along = lerp(-startExtension, segmentLength + endExtension, corner.x);
        float across = lerp(-halfWidth - 2.0, halfWidth + 2.0, corner.y);
        pixel = start + direction * along + normal * across;
    } else if (kind == 0) {
        pixel = lerp(input.bounds.xy - 2.0, input.bounds.zw + 2.0, corner);
    } else {
        pixel = lerp(input.bounds.xy, input.bounds.zw, corner);
    }
    float2 normalized = pixel / viewportSize;
    output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
    output.pixelPosition = pixel;
    output.textureUv = lerp(input.uv.xy, input.uv.zw, corner);
    output.tintColor = input.color;
    output.linePoints = kind == 1 ? input.uv : input.bounds;
    output.lineNeighbors = input.uv;
    output.shapeMetrics = input.metrics;
    output.textureSlot = input.style.y;
    output.primitiveKind = kind;
    output.lineCap = input.style.z;
    output.lineJoin = decodedLineJoin;
    output.lineFlags = decodedLineFlags;
    return output;
}

float4 sampleTexture(uint slot, float2 uv) {
#ifdef HENIA_TEXTURE_FREE
    return 1.0;
#else
    if (slot == 0) return textures[0].Sample(linearSampler, uv);
    if (slot == 1) return textures[1].Sample(linearSampler, uv);
    if (slot == 2) return textures[2].Sample(linearSampler, uv);
    if (slot == 3) return textures[3].Sample(linearSampler, uv);
    if (slot == 4) return textures[4].Sample(linearSampler, uv);
    if (slot == 5) return textures[5].Sample(linearSampler, uv);
    if (slot == 6) return textures[6].Sample(linearSampler, uv);
    return textures[7].Sample(linearSampler, uv);
#endif
}

float roundedBoxDistance(float2 positionValue, float2 halfSize, float radius) {
    float2 q = abs(positionValue) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float boxDistance(float2 positionValue, float2 halfSize) {
    float2 q = abs(positionValue) - halfSize;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float cappedSegmentDistance(
    float2 positionValue,
    float2 start,
    float2 finish,
    uint startMode,
    uint endMode,
    float halfWidth) {
    float2 segment = finish - start;
    float segmentLength = length(segment);
    float2 direction = segment / segmentLength;
    float2 normal = float2(-direction.y, direction.x);
    float startExtension = startMode == 1 ? halfWidth : 0.0;
    float endExtension = endMode == 1 ? halfWidth : 0.0;
    float center = (segmentLength + endExtension - startExtension) * 0.5;
    float2 local = float2(
        dot(positionValue - start, direction) - center,
        dot(positionValue - start, normal));
    float distanceValue = boxDistance(
        local,
        float2((segmentLength + startExtension + endExtension) * 0.5, halfWidth));
    if (startMode == 2) distanceValue = min(distanceValue, length(positionValue - start) - halfWidth);
    if (endMode == 2) distanceValue = min(distanceValue, length(positionValue - finish) - halfWidth);
    return distanceValue;
}

float triangleDistance(float2 positionValue, float2 a, float2 b, float2 c) {
    float2 e0 = b - a;
    float2 e1 = c - b;
    float2 e2 = a - c;
    float2 v0 = positionValue - a;
    float2 v1 = positionValue - b;
    float2 v2 = positionValue - c;
    float2 pq0 = v0 - e0 * saturate(dot(v0, e0) / dot(e0, e0));
    float2 pq1 = v1 - e1 * saturate(dot(v1, e1) / dot(e1, e1));
    float2 pq2 = v2 - e2 * saturate(dot(v2, e2) / dot(e2, e2));
    float orientation = sign(e0.x * e2.y - e0.y * e2.x);
    float2 distanceValue = min(
        min(
            float2(dot(pq0, pq0), orientation * (v0.x * e0.y - v0.y * e0.x)),
            float2(dot(pq1, pq1), orientation * (v1.x * e1.y - v1.y * e1.x))),
        float2(dot(pq2, pq2), orientation * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(distanceValue.x) * sign(distanceValue.y);
}

float bevelJoinDistance(
    float2 positionValue,
    float2 before,
    float2 joint,
    float2 after,
    float halfWidth) {
    float2 incoming = normalize(joint - before);
    float2 outgoing = normalize(after - joint);
    float turn = incoming.x * outgoing.y - incoming.y * outgoing.x;
    if (abs(turn) < 0.0001) return 1e20;
    float outside = turn > 0.0 ? -1.0 : 1.0;
    float2 incomingNormal = float2(-incoming.y, incoming.x) * outside;
    float2 outgoingNormal = float2(-outgoing.y, outgoing.x) * outside;
    return triangleDistance(
        positionValue,
        joint,
        joint + incomingNormal * halfWidth,
        joint + outgoingNormal * halfWidth);
}

float4 pixelMain(PixelInput input) : SV_Target {
    float coverage = 1.0;
    float4 color = input.tintColor;

    if (input.primitiveKind == 0 || input.primitiveKind == 1) {
        float2 primitiveSize = input.linePoints.zw - input.linePoints.xy;
        float2 centered = input.pixelPosition - (input.linePoints.xy + input.linePoints.zw) * 0.5;
        float distanceToEdge = roundedBoxDistance(
            centered,
            primitiveSize * 0.5,
            min(input.shapeMetrics.x, min(primitiveSize.x, primitiveSize.y) * 0.5));
        float antiAlias = max(fwidth(distanceToEdge), 0.75);
        float outer = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
        if (input.primitiveKind == 1) {
            float innerDistance = distanceToEdge + max(input.shapeMetrics.y, 0.0);
            float inner = 1.0 - smoothstep(-antiAlias, antiAlias, innerDistance);
            coverage = max(outer - inner, 0.0);
        } else {
            coverage = outer;
        }
    } else if (input.primitiveKind == 2) {
        uint joinMode = input.lineJoin == 0 ? 0 : 2;
        bool hasPrevious = (input.lineFlags & 1) != 0;
        bool hasNext = (input.lineFlags & 2) != 0;
        uint startMode = hasPrevious ? joinMode : input.lineCap;
        uint endMode = hasNext ? joinMode : input.lineCap;
        float halfWidth = input.shapeMetrics.y * 0.5;
        float distanceToLine = cappedSegmentDistance(
            input.pixelPosition,
            input.linePoints.xy,
            input.linePoints.zw,
            startMode,
            endMode,
            halfWidth);
        if (hasNext && input.lineJoin == 0) {
            distanceToLine = min(
                distanceToLine,
                bevelJoinDistance(
                    input.pixelPosition,
                    input.linePoints.xy,
                    input.linePoints.zw,
                    input.lineNeighbors.zw,
                    halfWidth));
        }
        float antiAlias = max(fwidth(distanceToLine), 0.75);
        if (hasPrevious) {
            float previousDistance = cappedSegmentDistance(
                input.pixelPosition,
                input.lineNeighbors.xy,
                input.linePoints.xy,
                0,
                joinMode,
                halfWidth);
            if (input.lineJoin == 0) {
                previousDistance = min(
                    previousDistance,
                    bevelJoinDistance(
                        input.pixelPosition,
                        input.lineNeighbors.xy,
                        input.linePoints.xy,
                        input.linePoints.zw,
                        halfWidth));
            }
            if (previousDistance <= distanceToLine) discard;
        }
        if (hasNext) {
            float nextDistance = cappedSegmentDistance(
                input.pixelPosition,
                input.linePoints.zw,
                input.lineNeighbors.zw,
                joinMode,
                0,
                halfWidth);
            if (nextDistance < distanceToLine) discard;
        }
        coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToLine);
    } else if (input.primitiveKind == 3) {
        color *= sampleTexture(input.textureSlot, input.textureUv);
    } else if (input.primitiveKind == 4) {
        color.a *= sampleTexture(input.textureSlot, input.textureUv).r;
    }

    color.a *= coverage;
    clip(color.a - 0.001);
    return float4(color.rgb * color.a, color.a);
}
)hlsl";

[[nodiscard]] D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

[[nodiscard]] D3D12_RESOURCE_DESC bufferDescription(std::uint64_t size) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Alignment = 0;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;
    return description;
}

struct AdapterArchitecture final {
    bool known = false;
    bool uma = true;
};

[[nodiscard]] AdapterArchitecture queryAdapterArchitecture(ID3D12Device& device) noexcept {
    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1{.NodeIndex = 0};
    if (SUCCEEDED(device.CheckFeatureSupport(
            D3D12_FEATURE_ARCHITECTURE1,
            &architecture1,
            sizeof(architecture1)))) {
        return {.known = true, .uma = architecture1.UMA != FALSE};
    }
    D3D12_FEATURE_DATA_ARCHITECTURE architecture{.NodeIndex = 0};
    if (SUCCEEDED(device.CheckFeatureSupport(
            D3D12_FEATURE_ARCHITECTURE,
            &architecture,
            sizeof(architecture)))) {
        return {.known = true, .uma = architecture.UMA != FALSE};
    }
    return {};
}

[[nodiscard]] bool validInstanceStorageStrategy(
    henia::backend::d3d12::InstanceStorageStrategy strategy) noexcept {
    using Strategy = henia::backend::d3d12::InstanceStorageStrategy;
    return strategy == Strategy::Automatic
        || strategy == Strategy::DirectUpload
        || strategy == Strategy::GpuLocal;
}

[[nodiscard]] D3D12_RESOURCE_DESC textureDescription(const TextureView& view) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = view.width;
    description.Height = view.height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = view.format == TextureFormat::Alpha8
        ? DXGI_FORMAT_R8_UNORM
        : DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    return description;
}

[[nodiscard]] bool compileShader(
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& output,
    henia::detail::FixedError& error,
    const D3D_SHADER_MACRO* macros = nullptr) noexcept {
    ComPtr<ID3DBlob> errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT result = D3DCompile(
        kShaderSource,
        std::strlen(kShaderSource),
        "HeniaUI",
        macros,
        nullptr,
        entry,
        target,
        flags,
        0,
        &output,
        &errors);
    if (SUCCEEDED(result)) {
        return true;
    }
    if (errors != nullptr && errors->GetBufferPointer() != nullptr) {
        error.assign(
            static_cast<const char*>(errors->GetBufferPointer()),
            errors->GetBufferSize());
    } else {
        error = "D3DCompile failed without diagnostics";
    }
    return false;
}

[[nodiscard]] D3D12_BLEND_DESC blendDescription(bool additive) noexcept {
    D3D12_BLEND_DESC description{};
    description.AlphaToCoverageEnable = FALSE;
    description.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC& target = description.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.LogicOpEnable = FALSE;
    target.SrcBlend = D3D12_BLEND_ONE;
    target.DestBlend = additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.LogicOp = D3D12_LOGIC_OP_NOOP;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}

[[nodiscard]] D3D12_RASTERIZER_DESC rasterizerDescription() noexcept {
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = D3D12_FILL_MODE_SOLID;
    description.CullMode = D3D12_CULL_MODE_NONE;
    description.FrontCounterClockwise = FALSE;
    description.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.DepthClipEnable = TRUE;
    description.MultisampleEnable = FALSE;
    description.AntialiasedLineEnable = FALSE;
    description.ForcedSampleCount = 0;
    description.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return description;
}

[[nodiscard]] D3D12_DEPTH_STENCIL_DESC depthStencilDescription() noexcept {
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = FALSE;
    description.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    description.StencilEnable = FALSE;
    return description;
}

} // namespace

struct D3D12Renderer::Implementation final {
    struct DescriptorTableCache final {
        std::array<std::uint64_t, DrawBatch::kTextureCapacity> handles{};
        std::array<std::uint64_t, DrawBatch::kTextureCapacity> revisions{};
        bool valid = false;
    };

    struct Submission final {
        ComPtr<ID3D12Resource> uploadInstances;
        ComPtr<ID3D12Resource> gpuLocalInstances;
        std::byte* mapped = nullptr;
        std::uint64_t directUploadedIdentity = 0;
        std::uint64_t directUploadedRevision = 0;
        std::uint64_t gpuLocalUploadedIdentity = 0;
        std::uint64_t gpuLocalUploadedRevision = 0;
        std::vector<ComPtr<ID3D12Resource>> retainedTextures;
        std::vector<DescriptorTableCache> descriptorTables;
    };

    struct GpuTexture final {
        ComPtr<ID3D12Resource> resource;
        std::uint64_t handle = 0;
        std::uint64_t revision = 0;
        std::uint64_t descriptorRevision = 0;
        std::uint64_t byteSize = 0;
        bool pendingInPlace = false;
        bool external = false;
    };

    struct StagedTexture final {
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> upload;
        D3D12_SHADER_RESOURCE_VIEW_DESC shaderView{};
        std::uint32_t value = 0;
        std::uint64_t handle = 0;
        std::uint64_t revision = 0;
        std::uint64_t byteSize = 0;
        std::uint64_t uploadedBytes = 0;
        bool partial = false;
    };

    struct TextureUploadBatch final {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        detail::D3D12TextureUploadTransaction transaction;
        std::vector<StagedTexture> staged;
    };

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> textureUploadQueue;
    ComPtr<ID3D12Fence> textureUploadFence;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12RootSignature> textureFreeRootSignature;
    ComPtr<ID3D12PipelineState> alphaPipeline;
    ComPtr<ID3D12PipelineState> additivePipeline;
    ComPtr<ID3D12PipelineState> textureFreeAlphaPipeline;
    ComPtr<ID3D12PipelineState> textureFreeAdditivePipeline;
    ComPtr<ID3D12DescriptorHeap> cpuTextureHeap;
    ComPtr<ID3D12DescriptorHeap> gpuBatchHeap;
    std::vector<Submission> submissions;
    std::vector<GpuTexture> textures;
    std::vector<std::uint64_t> scheduledTextureHandles;
    std::vector<std::uint64_t> scheduledTextureRevisions;
    std::vector<TextureUploadBatch> textureUploadBatches;
    std::vector<std::uint32_t> dirtyTextures;
    D3D12RendererConfiguration configuration{};
    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_UNKNOWN;
    std::uint32_t descriptorStride = 0;
    std::uint32_t instanceBufferBytes = 0;
    std::uint32_t cpuDescriptorCapacity = 0;
    std::uint32_t gpuDescriptorCapacity = 0;
    D3D12RenderStatistics statistics{};
    henia::detail::ProfileTimeline profileTimeline;
    henia::detail::FixedError error;
    std::uint64_t nextTextureUploadFenceValue = 1;
    std::uint64_t nextDescriptorRevision = 1;
    bool adapterArchitectureKnown = false;
    bool adapterUma = true;
    bool gpuLocalResourcesEnabled = false;
    bool ready = false;

    [[nodiscard]] bool initialize(
        ID3D12Device& nativeDevice,
        DXGI_FORMAT format,
        D3D12RendererConfiguration requested);
    [[nodiscard]] bool createRootSignature() noexcept;
    [[nodiscard]] bool createPipelines() noexcept;
    [[nodiscard]] bool createDescriptorHeaps() noexcept;
    [[nodiscard]] bool createSubmissionBuffers() noexcept;
    [[nodiscard]] bool createTextureUploadBatches() noexcept;
    [[nodiscard]] bool synchronizeTextures(
        TextureStore& store,
        ID3D12CommandQueue& queue);
    [[nodiscard]] bool bindExternalTexture(
        const TextureStore& store,
        TextureHandle handle,
        ID3D12Resource& texture) noexcept;
    [[nodiscard]] bool pollTextureUploads() noexcept;
    void rollbackTextureUpload(TextureUploadBatch& batch) noexcept;
    [[nodiscard]] bool record(
        const RenderPacket& packet,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        std::uint32_t width,
        std::uint32_t height,
        henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept;
    [[nodiscard]] bool validateCommandList(ID3D12GraphicsCommandList& commandList) noexcept;
    [[nodiscard]] bool validateDeviceChild(
        ID3D12DeviceChild& child,
        const char* diagnostic) noexcept;
    [[nodiscard]] bool validateSubmissionReuse(
        henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor(std::uint32_t index) const noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE gpuCpuDescriptor(std::uint32_t index) const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor(std::uint32_t index) const noexcept;
};

bool D3D12Renderer::Implementation::initialize(
    ID3D12Device& nativeDevice,
    DXGI_FORMAT format,
    D3D12RendererConfiguration requested) {
    if (ready) {
        ComPtr<IUnknown> configuredIdentity;
        ComPtr<IUnknown> requestedIdentity;
        if (FAILED(device.As(&configuredIdentity))
            || FAILED(nativeDevice.QueryInterface(IID_PPV_ARGS(&requestedIdentity)))
            || configuredIdentity.Get() != requestedIdentity.Get()) {
            ++statistics.lifecycleRejections;
            error = "D3D12 renderer is already initialized with a different device";
            return false;
        }
        if (format != renderTargetFormat
            || requested.instanceCapacity != configuration.instanceCapacity
            || requested.submissionCapacity != configuration.submissionCapacity
            || requested.batchCapacity != configuration.batchCapacity
            || requested.textureCapacity != configuration.textureCapacity
            || requested.textureUploadBatchCapacity != configuration.textureUploadBatchCapacity
            || requested.instanceStorage != configuration.instanceStorage
            || requested.gpuLocalInstanceThresholdBytes
                != configuration.gpuLocalInstanceThresholdBytes) {
            ++statistics.lifecycleRejections;
            error = "D3D12 renderer is already initialized with a different configuration";
            return false;
        }
        if (FAILED(device->GetDeviceRemovedReason())) {
            ++statistics.deviceRemovalRejections;
            error = "D3D12 renderer device has been removed; shutdown and recreate the renderer";
            return false;
        }
        error.clear();
        return true;
    }
    std::size_t instanceBytes = 0;
    std::uint64_t descriptorFrames = 0;
    std::uint64_t gpuDescriptors = 0;
    std::uint32_t cpuDescriptors = 0;
    if (requested.instanceCapacity == 0 || requested.submissionCapacity == 0
        || requested.batchCapacity == 0 || requested.textureCapacity == 0
        || requested.textureUploadBatchCapacity == 0
        || !checkedMultiply(requested.instanceCapacity, sizeof(DrawInstance), instanceBytes)
        || instanceBytes > std::numeric_limits<std::uint32_t>::max()
        || !checkedMultiply(
            static_cast<std::uint64_t>(requested.submissionCapacity),
            static_cast<std::uint64_t>(requested.batchCapacity),
            descriptorFrames)
        || !checkedMultiply(
            descriptorFrames,
            static_cast<std::uint64_t>(DrawBatch::kTextureCapacity),
            gpuDescriptors)
        || gpuDescriptors > std::numeric_limits<std::uint32_t>::max()
        || !checkedAdd(requested.textureCapacity, 1U, cpuDescriptors)
        || !validInstanceStorageStrategy(requested.instanceStorage)
        || format == DXGI_FORMAT_UNKNOWN) {
        error = "D3D12 renderer configuration has an invalid capacity or format";
        return false;
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{.Format = format};
    if (FAILED(nativeDevice.CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT,
            &formatSupport,
            sizeof(formatSupport)))
        || (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0) {
        error = "D3D12 renderer format is not render-target compatible on the configured device";
        return false;
    }

    device = &nativeDevice;
    configuration = requested;
    const AdapterArchitecture architecture = queryAdapterArchitecture(nativeDevice);
    adapterArchitectureKnown = architecture.known;
    adapterUma = architecture.uma;
    using Strategy = henia::backend::d3d12::InstanceStorageStrategy;
    gpuLocalResourcesEnabled = requested.instanceStorage == Strategy::GpuLocal
        || (requested.instanceStorage == Strategy::Automatic
            && architecture.known && !architecture.uma
            && instanceBytes >= requested.gpuLocalInstanceThresholdBytes);
    renderTargetFormat = format;
    instanceBufferBytes = static_cast<std::uint32_t>(instanceBytes);
    cpuDescriptorCapacity = cpuDescriptors;
    gpuDescriptorCapacity = static_cast<std::uint32_t>(gpuDescriptors);
    submissions.resize(configuration.submissionCapacity);
    textures.resize(configuration.textureCapacity);
    scheduledTextureHandles.resize(configuration.textureCapacity);
    scheduledTextureRevisions.resize(configuration.textureCapacity);
    textureUploadBatches.resize(configuration.textureUploadBatchCapacity);
    dirtyTextures.reserve(configuration.textureCapacity);
    for (Submission& submission : submissions) {
        submission.retainedTextures.resize(configuration.textureCapacity);
        submission.descriptorTables.resize(configuration.batchCapacity);
    }
    for (TextureUploadBatch& batch : textureUploadBatches) {
        batch.staged.reserve(configuration.textureCapacity);
    }
    if (!createRootSignature() || !createPipelines() || !createDescriptorHeaps()
        || !createSubmissionBuffers() || !createTextureUploadBatches()) {
        shutdown();
        return false;
    }
    statistics = {};
    profileTimeline.reset();
    statistics.adapterArchitectureKnown = adapterArchitectureKnown;
    statistics.adapterUma = adapterUma;
    if (gpuLocalResourcesEnabled) {
        statistics.gpuLocalResidentBytes = static_cast<std::uint64_t>(instanceBufferBytes)
            * configuration.submissionCapacity;
    }
    nextTextureUploadFenceValue = 1;
    nextDescriptorRevision = 1;
    ready = true;
    error.clear();
    return true;
}

bool D3D12Renderer::Implementation::createRootSignature() noexcept {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = static_cast<UINT>(DrawBatch::kTextureCapacity);
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.RegisterSpace = 0;
    parameters[1].Constants.Num32BitValues = 2;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0.0F;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0F;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(
            &description,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &blob,
            &errors))) {
        if (errors != nullptr) {
            error.assign(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize());
        } else {
            error = "D3D12 root signature serialization failed";
        }
        return false;
    }
    if (FAILED(device->CreateRootSignature(
            0,
            blob->GetBufferPointer(),
            blob->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature)))) {
        error = "D3D12 root signature creation failed";
        return false;
    }

    parameters[0] = parameters[1];
    description.NumParameters = 1;
    description.pParameters = parameters.data();
    description.NumStaticSamplers = 0;
    description.pStaticSamplers = nullptr;
    blob.Reset();
    errors.Reset();
    if (FAILED(D3D12SerializeRootSignature(
            &description,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &blob,
            &errors))) {
        if (errors != nullptr) {
            error.assign(
                static_cast<const char*>(errors->GetBufferPointer()),
                errors->GetBufferSize());
        } else {
            error = "D3D12 texture-free root signature serialization failed";
        }
        return false;
    }
    if (FAILED(device->CreateRootSignature(
            0,
            blob->GetBufferPointer(),
            blob->GetBufferSize(),
            IID_PPV_ARGS(&textureFreeRootSignature)))) {
        error = "D3D12 texture-free root signature creation failed";
        return false;
    }
    return true;
}

bool D3D12Renderer::Implementation::createPipelines() noexcept {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> textureFreePixelShader;
    constexpr D3D_SHADER_MACRO textureFreeMacros[]{
        {"HENIA_TEXTURE_FREE", "1"},
        {nullptr, nullptr},
    };
    if (!compileShader("vertexMain", "vs_5_0", vertexShader, error)
        || !compileShader("pixelMain", "ps_5_0", pixelShader, error)
        || !compileShader(
            "pixelMain",
            "ps_5_0",
            textureFreePixelShader,
            error,
            textureFreeMacros)) {
        return false;
    }

    constexpr std::array inputElements{
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_BOUNDS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, bounds)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_UV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, uv)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, color)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_METRICS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, radius)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_STYLE", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, static_cast<UINT>(offsetof(DrawInstance, kind)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature.Get();
    description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    description.BlendState = blendDescription(false);
    description.SampleMask = UINT_MAX;
    description.RasterizerState = rasterizerDescription();
    description.DepthStencilState = depthStencilDescription();
    description.InputLayout = {inputElements.data(), static_cast<UINT>(inputElements.size())};
    description.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = renderTargetFormat;
    description.DSVFormat = DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = 1;
    if (FAILED(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&alphaPipeline)))) {
        error = "D3D12 alpha pipeline creation failed";
        return false;
    }
    description.BlendState = blendDescription(true);
    if (FAILED(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&additivePipeline)))) {
        error = "D3D12 additive pipeline creation failed";
        return false;
    }
    description.pRootSignature = textureFreeRootSignature.Get();
    description.PS = {
        textureFreePixelShader->GetBufferPointer(),
        textureFreePixelShader->GetBufferSize(),
    };
    description.BlendState = blendDescription(false);
    if (FAILED(device->CreateGraphicsPipelineState(
            &description,
            IID_PPV_ARGS(&textureFreeAlphaPipeline)))) {
        error = "D3D12 texture-free alpha pipeline creation failed";
        return false;
    }
    description.BlendState = blendDescription(true);
    if (FAILED(device->CreateGraphicsPipelineState(
            &description,
            IID_PPV_ARGS(&textureFreeAdditivePipeline)))) {
        error = "D3D12 texture-free additive pipeline creation failed";
        return false;
    }
    return true;
}

bool D3D12Renderer::Implementation::createDescriptorHeaps() noexcept {
    D3D12_DESCRIPTOR_HEAP_DESC cpuDescription{};
    cpuDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cpuDescription.NumDescriptors = cpuDescriptorCapacity;
    cpuDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&cpuDescription, IID_PPV_ARGS(&cpuTextureHeap)))) {
        error = "D3D12 CPU texture descriptor heap creation failed";
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC gpuDescription{};
    gpuDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    gpuDescription.NumDescriptors = gpuDescriptorCapacity;
    gpuDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&gpuDescription, IID_PPV_ARGS(&gpuBatchHeap)))) {
        error = "D3D12 shader-visible descriptor heap creation failed";
        return false;
    }
    descriptorStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC nullView{};
    nullView.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    nullView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullView.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(nullptr, &nullView, cpuDescriptor(0));
    return true;
}

bool D3D12Renderer::Implementation::createSubmissionBuffers() noexcept {
    const std::uint64_t bufferSize = instanceBufferBytes;
    const D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC description = bufferDescription(bufferSize);
    for (Submission& submission : submissions) {
        if (FAILED(device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&submission.uploadInstances)))) {
            error = "D3D12 instance upload buffer creation failed";
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(submission.uploadInstances->Map(0, &noRead, &mapped)) || mapped == nullptr) {
            error = "D3D12 instance upload buffer mapping failed";
            return false;
        }
        submission.mapped = static_cast<std::byte*>(mapped);
        if (gpuLocalResourcesEnabled
            && FAILED(device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                nullptr,
                IID_PPV_ARGS(&submission.gpuLocalInstances)))) {
            error = "D3D12 GPU-local instance buffer creation failed";
            return false;
        }
    }
    return true;
}

bool D3D12Renderer::Implementation::createTextureUploadBatches() noexcept {
    if (FAILED(device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&textureUploadFence)))) {
        error = "D3D12 texture upload fence creation failed";
        return false;
    }
    for (TextureUploadBatch& batch : textureUploadBatches) {
        if (FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&batch.allocator)))
            || FAILED(device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                batch.allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&batch.commandList)))
            || FAILED(batch.commandList->Close())) {
            error = "D3D12 texture upload command objects could not be created";
            return false;
        }
    }
    return true;
}

bool D3D12Renderer::Implementation::synchronizeTextures(
    TextureStore& store,
    ID3D12CommandQueue& queue) {
    if (!ready) {
        error = "D3D12 renderer is not initialized";
        return false;
    }
    if (!validateDeviceChild(
            queue,
            "D3D12 texture synchronization queue belongs to a different device")) {
        return false;
    }
    if (!pollTextureUploads()) {
        return false;
    }
    if (store.slotCount() > configuration.textureCapacity) {
        error = "D3D12 texture store exceeds configured capacity";
        return false;
    }
    dirtyTextures.clear();
    for (std::uint32_t index = 0; index < configuration.textureCapacity; ++index) {
        const TextureHandle handle = store.handleAt(index);
        GpuTexture& texture = textures[index];
        if (texture.resource != nullptr
            && (!handle.valid() || texture.handle != handle.packed())) {
            texture = {};
            device->CopyDescriptorsSimple(
                1,
                cpuDescriptor(index + 1U),
                cpuDescriptor(0),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            ++statistics.retiredTextures;
        }
        if (!handle.valid()) {
            scheduledTextureHandles[index] = 0;
            scheduledTextureRevisions[index] = 0;
            continue;
        }
        TextureView view = store.view(handle);
        if (view.backingPolicy == TextureBackingPolicy::ExternalGpu) {
            if (scheduledTextureHandles[index] != handle.packed()) {
                scheduledTextureHandles[index] = 0;
                scheduledTextureRevisions[index] = 0;
            }
            continue;
        }
        if (scheduledTextureHandles[index] == handle.packed()
            && scheduledTextureRevisions[index] == view.revision) {
            continue;
        }
        if (!view.backingAvailable) {
            static_cast<void>(store.ensureCpuBacking(handle));
            view = store.view(handle);
        }
        const std::uint32_t pixelBytes = view.format == TextureFormat::Alpha8 ? 1U : 4U;
        std::size_t minimumPitch = 0;
        std::size_t requiredBytes = 0;
        if (!view.handle.valid() || !view.backingAvailable || view.revision == 0
            || (view.format != TextureFormat::Alpha8 && view.format != TextureFormat::Rgba8)
            || view.width == 0 || view.height == 0
            || view.width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || view.height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || !checkedMultiply(
                static_cast<std::size_t>(view.width),
                static_cast<std::size_t>(pixelBytes),
                minimumPitch)
            || view.rowPitch < minimumPitch
            || !checkedMultiply(
                static_cast<std::size_t>(view.rowPitch),
                static_cast<std::size_t>(view.height),
                requiredBytes)
            || requiredBytes != view.pixels.size()) {
            error = "D3D12 texture metadata or CPU backing is invalid";
            return false;
        }
        dirtyTextures.push_back(index + 1U);
    }
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    for (const GpuTexture& texture : textures) {
        statistics.gpuTextureBytes += texture.byteSize;
        if (texture.resource != nullptr && texture.external) ++statistics.externalTextures;
    }
    if (dirtyTextures.empty()) {
        error.clear();
        return true;
    }

    if (queue.GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        error = "D3D12 texture synchronization requires a direct command queue";
        return false;
    }
    if (textureUploadQueue == nullptr) {
        textureUploadQueue = &queue;
    } else {
        ComPtr<IUnknown> configuredQueueIdentity;
        ComPtr<IUnknown> requestedQueueIdentity;
        if (FAILED(textureUploadQueue.As(&configuredQueueIdentity))
            || FAILED(queue.QueryInterface(IID_PPV_ARGS(&requestedQueueIdentity)))
            || configuredQueueIdentity.Get() != requestedQueueIdentity.Get()) {
            error = "D3D12 texture synchronization must use one direct command queue";
            return false;
        }
    }

    TextureUploadBatch* uploadBatch = nullptr;
    for (TextureUploadBatch& batch : textureUploadBatches) {
        if (batch.transaction.begin()) {
            uploadBatch = &batch;
            break;
        }
    }
    if (uploadBatch == nullptr) {
        error = "D3D12 texture upload batches are all in flight";
        return false;
    }
    TextureUploadBatch& batch = *uploadBatch;
    batch.staged.clear();
    if (FAILED(batch.allocator->Reset())
        || FAILED(batch.commandList->Reset(batch.allocator.Get(), nullptr))) {
        batch.transaction.abandon();
        ++statistics.failedTextureUploadBatches;
        error = "D3D12 texture upload command objects could not be reset";
        return false;
    }

    for (const std::uint32_t value : dirtyTextures) {
        const TextureHandle handle = store.handleAt(value - 1U);
        const TextureView view = store.view(handle);
        const D3D12_RESOURCE_DESC textureDesc = textureDescription(view);
        GpuTexture& current = textures[value - 1U];
        bool retainedBySubmission = false;
        for (const Submission& submission : submissions) {
            if (submission.retainedTextures[value - 1U].Get() == current.resource.Get()
                && current.resource != nullptr) {
                retainedBySubmission = true;
                break;
            }
        }
        const std::uint32_t pixelBytes = view.format == TextureFormat::Alpha8 ? 1U : 4U;
        const bool validRegion = view.dirtyRegion.width > 0 && view.dirtyRegion.height > 0
            && view.dirtyRegion.x <= view.width
            && view.dirtyRegion.width <= view.width - view.dirtyRegion.x
            && view.dirtyRegion.y <= view.height
            && view.dirtyRegion.height <= view.height - view.dirtyRegion.y;
        const bool partial = current.resource != nullptr && !current.external
            && !current.pendingInPlace && !retainedBySubmission
            && current.handle == handle.packed() && current.revision + 1U == view.revision
            && !view.fullUpdate && validRegion;
        StagedTexture staged{};
        staged.value = value;
        staged.handle = handle.packed();
        staged.revision = view.revision;
        staged.byteSize = static_cast<std::uint64_t>(view.width) * view.height * pixelBytes;
        staged.partial = partial;
        if (partial) {
            staged.resource = current.resource;
        } else {
            const D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            if (FAILED(device->CreateCommittedResource(
                    &defaultHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &textureDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr,
                    IID_PPV_ARGS(&staged.resource)))) {
                error = "D3D12 texture resource creation failed";
                rollbackTextureUpload(batch);
                return false;
            }
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rowCount = 0;
        UINT64 rowBytes = 0;
        UINT64 uploadBytes = 0;
        if (partial) {
            rowCount = view.dirtyRegion.height;
            rowBytes = static_cast<UINT64>(view.dirtyRegion.width) * pixelBytes;
            footprint.Footprint.Format = textureDesc.Format;
            footprint.Footprint.Width = view.dirtyRegion.width;
            footprint.Footprint.Height = view.dirtyRegion.height;
            footprint.Footprint.Depth = 1;
            footprint.Footprint.RowPitch = static_cast<UINT>(
                (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1U)
                & ~(static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) - 1U));
            uploadBytes = static_cast<UINT64>(footprint.Footprint.RowPitch) * rowCount;
        } else {
            device->GetCopyableFootprints(
                &textureDesc,
                0,
                1,
                0,
                &footprint,
                &rowCount,
                &rowBytes,
                &uploadBytes);
        }
        staged.uploadedBytes = rowBytes * rowCount;

        const D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC uploadDesc = bufferDescription(uploadBytes);
        if (FAILED(device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&staged.upload)))) {
            error = "D3D12 texture upload buffer creation failed";
            rollbackTextureUpload(batch);
            return false;
        }

        std::byte* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(staged.upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)))
            || mapped == nullptr) {
            error = "D3D12 texture upload mapping failed";
            rollbackTextureUpload(batch);
            return false;
        }
        for (UINT row = 0; row < rowCount; ++row) {
            const std::size_t sourceOffset = partial
                ? static_cast<std::size_t>(view.dirtyRegion.y + row) * view.rowPitch
                    + static_cast<std::size_t>(view.dirtyRegion.x) * pixelBytes
                : static_cast<std::size_t>(row) * view.rowPitch;
            std::memcpy(
                mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                view.pixels.data() + sourceOffset,
                static_cast<std::size_t>(rowBytes));
        }
        const D3D12_RANGE written{0, static_cast<SIZE_T>(uploadBytes)};
        staged.upload->Unmap(0, &written);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = staged.resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = staged.upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        if (partial) {
            D3D12_RESOURCE_BARRIER toCopy{};
            toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toCopy.Transition.pResource = staged.resource.Get();
            toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            batch.commandList->ResourceBarrier(1, &toCopy);
        }
        batch.commandList->CopyTextureRegion(
            &destination,
            partial ? view.dirtyRegion.x : 0,
            partial ? view.dirtyRegion.y : 0,
            0,
            &source,
            nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = staged.resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        batch.commandList->ResourceBarrier(1, &barrier);

        if (!partial) {
            staged.shaderView.Format = textureDesc.Format;
            staged.shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            staged.shaderView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            staged.shaderView.Texture2D.MipLevels = 1;
        }
        batch.staged.push_back(std::move(staged));
    }

    if (FAILED(batch.commandList->Close())) {
        batch.transaction.abandon();
        ++statistics.failedTextureUploadBatches;
        error = "D3D12 texture upload command list could not be closed";
        return false;
    }
    ID3D12CommandList* commandLists[]{batch.commandList.Get()};
    queue.ExecuteCommandLists(1, commandLists);
    const std::uint64_t fenceValue = nextTextureUploadFenceValue;
    if (FAILED(queue.Signal(textureUploadFence.Get(), fenceValue))) {
        for (const StagedTexture& staged : batch.staged) {
            if (!staged.partial) continue;
            GpuTexture& texture = textures[staged.value - 1U];
            if (texture.resource.Get() == staged.resource.Get()) {
                texture = {};
                device->CopyDescriptorsSimple(
                    1,
                    cpuDescriptor(staged.value),
                    cpuDescriptor(0),
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                ++statistics.retiredTextures;
            }
        }
        statistics.gpuTextureBytes = 0;
        statistics.externalTextures = 0;
        for (const GpuTexture& texture : textures) {
            statistics.gpuTextureBytes += texture.byteSize;
            if (texture.resource != nullptr && texture.external) ++statistics.externalTextures;
        }
        batch.transaction.abandon();
        ++statistics.failedTextureUploadBatches;
        error = "D3D12 texture upload fence signal failed; submitted resources were retained";
        return false;
    }
    static_cast<void>(batch.transaction.submit(fenceValue));
    ++nextTextureUploadFenceValue;
    ++statistics.textureUploadBatches;
    for (const StagedTexture& staged : batch.staged) {
        scheduledTextureHandles[staged.value - 1U] = staged.handle;
        scheduledTextureRevisions[staged.value - 1U] = staged.revision;
        if (staged.partial) {
            GpuTexture& texture = textures[staged.value - 1U];
            if (texture.resource.Get() == staged.resource.Get()) texture.pendingInPlace = true;
        }
    }

    error.clear();
    return true;
}

void D3D12Renderer::Implementation::rollbackTextureUpload(TextureUploadBatch& batch) noexcept {
    // Reset is legal only after Close. If Close itself fails, retain the batch
    // until shutdown because the command-list state can no longer be trusted.
    if (SUCCEEDED(batch.commandList->Close())) {
        batch.staged.clear();
        batch.transaction.rollback();
    } else {
        batch.transaction.abandon();
    }
    ++statistics.failedTextureUploadBatches;
}

bool D3D12Renderer::Implementation::pollTextureUploads() noexcept {
    if (!ready && textureUploadFence == nullptr) {
        error = "D3D12 renderer is not initialized";
        return false;
    }
    const std::uint64_t completedFence = textureUploadFence->GetCompletedValue();
    if (completedFence == std::numeric_limits<std::uint64_t>::max()) {
        error = "D3D12 texture upload fence reported device removal";
        return false;
    }

    while (true) {
        TextureUploadBatch* completedBatch = nullptr;
        for (TextureUploadBatch& batch : textureUploadBatches) {
            if (batch.transaction.completed(completedFence)
                && (completedBatch == nullptr
                    || batch.transaction.fenceValue()
                        < completedBatch->transaction.fenceValue())) {
                completedBatch = &batch;
            }
        }
        if (completedBatch == nullptr) {
            break;
        }

        for (StagedTexture& staged : completedBatch->staged) {
            ++statistics.textureUploads;
            statistics.uploadedTextureBytes += staged.uploadedBytes;
            if (staged.partial) {
                ++statistics.partialTextureUploads;
            } else {
                ++statistics.fullTextureUploads;
            }
            GpuTexture& texture = textures[staged.value - 1U];
            const bool stillScheduled = scheduledTextureHandles[staged.value - 1U]
                    == staged.handle
                && scheduledTextureRevisions[staged.value - 1U] == staged.revision;
            if (!stillScheduled) {
                if (staged.partial && texture.resource.Get() == staged.resource.Get()) {
                    texture.pendingInPlace = false;
                }
                continue;
            }
            if (staged.partial) {
                if (texture.resource.Get() != staged.resource.Get()
                    || texture.handle != staged.handle) {
                    scheduledTextureHandles[staged.value - 1U] = 0;
                    scheduledTextureRevisions[staged.value - 1U] = 0;
                    continue;
                }
                texture.pendingInPlace = false;
            } else {
                device->CreateShaderResourceView(
                    staged.resource.Get(),
                    &staged.shaderView,
                    cpuDescriptor(staged.value));
                if (texture.resource != nullptr
                    && texture.resource.Get() != staged.resource.Get()) {
                    ++statistics.retiredTextures;
                }
                texture.resource = std::move(staged.resource);
                texture.byteSize = staged.byteSize;
                texture.external = false;
            }
            texture.handle = staged.handle;
            texture.revision = staged.revision;
            texture.descriptorRevision = nextDescriptorRevision++;
        }
        completedBatch->staged.clear();
        static_cast<void>(completedBatch->transaction.release(completedFence));
    }
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    for (const GpuTexture& texture : textures) {
        statistics.gpuTextureBytes += texture.byteSize;
        if (texture.resource != nullptr && texture.external) ++statistics.externalTextures;
    }
    error.clear();
    return true;
}

bool D3D12Renderer::Implementation::bindExternalTexture(
    const TextureStore& store,
    TextureHandle handle,
    ID3D12Resource& nativeTexture) noexcept {
    if (!ready) {
        error = "D3D12 renderer is not initialized";
        return false;
    }
    const TextureView view = store.view(handle);
    if (!view.handle.valid() || view.backingPolicy != TextureBackingPolicy::ExternalGpu
        || handle.value() > textures.size()) {
        error = "D3D12 external texture binding is invalid";
        return false;
    }
    if (!validateDeviceChild(
            nativeTexture,
            "D3D12 external texture belongs to a different device")) return false;
    const D3D12_RESOURCE_DESC description = nativeTexture.GetDesc();
    const DXGI_FORMAT expectedFormat = view.format == TextureFormat::Alpha8
        ? DXGI_FORMAT_R8_UNORM
        : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D
        || description.Width != view.width || description.Height != view.height
        || description.DepthOrArraySize != 1 || description.MipLevels != 1
        || description.Format != expectedFormat || description.SampleDesc.Count != 1
        || description.SampleDesc.Quality != 0
        || (description.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
        error = "D3D12 external texture metadata does not match its TextureStore entry";
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC shaderView{};
    shaderView.Format = expectedFormat;
    shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shaderView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shaderView.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(&nativeTexture, &shaderView, cpuDescriptor(handle.value()));
    GpuTexture& texture = textures[handle.value() - 1U];
    if (texture.resource != nullptr && texture.resource.Get() != &nativeTexture) {
        ++statistics.retiredTextures;
    }
    texture = {
        .resource = &nativeTexture,
        .handle = handle.packed(),
        .revision = view.revision,
        .descriptorRevision = nextDescriptorRevision++,
        .byteSize = static_cast<std::uint64_t>(view.rowPitch) * view.height,
        .pendingInPlace = false,
        .external = true,
    };
    scheduledTextureHandles[handle.value() - 1U] = handle.packed();
    scheduledTextureRevisions[handle.value() - 1U] = view.revision;
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
    for (const GpuTexture& current : textures) {
        statistics.gpuTextureBytes += current.byteSize;
        if (current.resource != nullptr && current.external) ++statistics.externalTextures;
    }
    error.clear();
    return true;
}

bool D3D12Renderer::Implementation::record(
    const RenderPacket& packet,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    std::uint32_t width,
    std::uint32_t height,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    const std::uint64_t frameAttemptId = ++statistics.frameAttempts;
    if (!ready || submissionSlot >= submissions.size()) {
        ++statistics.rejectedFrames;
        error = "D3D12 renderer or submissionSlot is unavailable";
        return false;
    }
    if (!validateCommandList(commandList)) {
        ++statistics.rejectedFrames;
        return false;
    }
    if (width == 0 || height == 0
        || width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error = "viewportWidth/viewportHeight is outside the LONG range";
        return false;
    }
    if (packet.instances().size() > configuration.instanceCapacity
        || packet.batches().size() > configuration.batchCapacity) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "D3D12 render packet exceeds a configured frame capacity";
        return false;
    }
    std::size_t packetBytes = 0;
    if (!checkedMultiply(packet.instances().size(), sizeof(DrawInstance), packetBytes)
        || packetBytes > instanceBufferBytes) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "D3D12 render packet byte range exceeds the instance buffer";
        return false;
    }
    bool usesTextures = false;
    bool hasVisibleBatches = false;
    for (const DrawBatch& batch : packet.batches()) {
        const std::size_t first = batch.firstInstance;
        const std::size_t count = batch.instanceCount;
        if (batch.textureCount > DrawBatch::kTextureCapacity
            || first > packet.instances().size() || count > packet.instances().size() - first) {
            ++statistics.rejectedFrames;
            ++statistics.invalidInputFrames;
            error = "Render packet batch instance/texture range is invalid";
            return false;
        }
        if (batch.clip.enabled) {
            if (!validateRect(batch.clip.area, "clip.area").empty()) {
                ++statistics.rejectedFrames;
                ++statistics.invalidInputFrames;
                error = "clip.area is invalid";
                return false;
            }
            ScissorRect scissor{};
            const bool visible = batch.instanceCount != 0
                && makeScissorRect(batch.clip.area, width, height, scissor);
            hasVisibleBatches = hasVisibleBatches || visible;
            usesTextures = usesTextures || (visible && batch.textureCount != 0);
        } else {
            hasVisibleBatches = hasVisibleBatches || batch.instanceCount != 0;
            usesTextures = usesTextures || (batch.instanceCount != 0 && batch.textureCount != 0);
        }
    }
    if (!pollTextureUploads()) {
        ++statistics.rejectedFrames;
        return false;
    }
    if (!validateSubmissionReuse(submissionReuse)) {
        ++statistics.rejectedFrames;
        return false;
    }
    Submission& submission = submissions[submissionSlot];
    if (packet.instances().empty() || packet.batches().empty() || !hasVisibleBatches) {
        for (ComPtr<ID3D12Resource>& retained : submission.retainedTextures) {
            retained.Reset();
        }
        ++statistics.successfulFrames;
        static_cast<void>(profileTimeline.complete({
            .frameAttemptId = frameAttemptId,
            .producerIdentity = packet.identity(),
            .producerRevision = packet.revision(),
            .producerBuildNanoseconds = packet.cpuBuildNanoseconds(),
            .submissionSlot = submissionSlot,
            .uploadKind = InstanceUploadKind::ZeroWorkRevision,
        }));
        statistics.profile = profileTimeline.profile();
        error.clear();
        return true;
    }
    for (const DrawBatch& batch : packet.batches()) {
        if (batch.clip.enabled) {
            ScissorRect scissor{};
            if (!makeScissorRect(batch.clip.area, width, height, scissor)) continue;
        }
        for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
            const TextureHandle handle = batch.textures[slot];
            if (!handle.valid() || handle.value() > textures.size()
                || textures[handle.value() - 1U].resource == nullptr
                || textures[handle.value() - 1U].handle != handle.packed()
                || textures[handle.value() - 1U].pendingInPlace) {
                ++statistics.rejectedFrames;
                error = "D3D12 render packet references an unsynchronized texture";
                return false;
            }
        }
    }

    // Reaching record means the host has declared this slot's preceding fence
    // complete. Release its old texture generation and retain every generation
    // whose descriptor is copied for the new submission.
    for (ComPtr<ID3D12Resource>& retained : submission.retainedTextures) {
        retained.Reset();
    }
    for (const DrawBatch& batch : packet.batches()) {
        if (batch.clip.enabled) {
            ScissorRect scissor{};
            if (!makeScissorRect(batch.clip.area, width, height, scissor)) continue;
        }
        for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
            const std::uint32_t index = batch.textures[slot].value() - 1U;
            submission.retainedTextures[index] = textures[index].resource;
        }
    }
    using Strategy = henia::backend::d3d12::InstanceStorageStrategy;
    const bool useGpuLocal = gpuLocalResourcesEnabled
        && (configuration.instanceStorage == Strategy::GpuLocal
            || packetBytes >= configuration.gpuLocalInstanceThresholdBytes);
    std::uint64_t& uploadedIdentity = useGpuLocal
        ? submission.gpuLocalUploadedIdentity
        : submission.directUploadedIdentity;
    std::uint64_t& uploadedRevision = useGpuLocal
        ? submission.gpuLocalUploadedRevision
        : submission.directUploadedRevision;
    std::uint64_t cpuUploadNanoseconds = 0;
    bool uploadedInstances = false;
    if (uploadedIdentity != packet.identity() || uploadedRevision != packet.revision()) {
        const auto uploadStarted = std::chrono::steady_clock::now();
        std::memcpy(submission.mapped, packet.instances().data(), packetBytes);
        if (useGpuLocal && packetBytes != 0) {
            D3D12_RESOURCE_BARRIER transition{};
            transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            transition.Transition.pResource = submission.gpuLocalInstances.Get();
            transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            transition.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            commandList.ResourceBarrier(1, &transition);
            commandList.CopyBufferRegion(
                submission.gpuLocalInstances.Get(),
                0,
                submission.uploadInstances.Get(),
                0,
                packetBytes);
            std::swap(transition.Transition.StateBefore, transition.Transition.StateAfter);
            commandList.ResourceBarrier(1, &transition);
            ++statistics.instanceCopyOperations;
            statistics.copiedInstanceBytes += packetBytes;
        }
        uploadedIdentity = packet.identity();
        uploadedRevision = packet.revision();
        ++statistics.instanceUploads;
        statistics.uploadedInstanceBytes += packetBytes;
        uploadedInstances = true;
        cpuUploadNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - uploadStarted).count());
    }
    if (useGpuLocal) ++statistics.gpuLocalFrames;
    else ++statistics.directUploadFrames;

    const auto submitStarted = std::chrono::steady_clock::now();
    const std::array viewportConstants{static_cast<float>(width), static_cast<float>(height)};
    if (usesTextures) {
        ID3D12DescriptorHeap* heaps[]{gpuBatchHeap.Get()};
        commandList.SetDescriptorHeaps(1, heaps);
        commandList.SetGraphicsRootSignature(rootSignature.Get());
        commandList.SetGraphicsRoot32BitConstants(1, 2, viewportConstants.data(), 0);
        ++statistics.descriptorHeapBindings;
    } else {
        commandList.SetGraphicsRootSignature(textureFreeRootSignature.Get());
        commandList.SetGraphicsRoot32BitConstants(0, 2, viewportConstants.data(), 0);
        ++statistics.textureFreeFrames;
    }
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D12Resource* vertexInstances = useGpuLocal
        ? submission.gpuLocalInstances.Get()
        : submission.uploadInstances.Get();
    const D3D12_VERTEX_BUFFER_VIEW instanceView{
        vertexInstances->GetGPUVirtualAddress(),
        instanceBufferBytes,
        static_cast<UINT>(sizeof(DrawInstance)),
    };
    commandList.IASetVertexBuffers(0, 1, &instanceView);
    const D3D12_VIEWPORT viewport{
        0.0F,
        0.0F,
        static_cast<float>(width),
        static_cast<float>(height),
        0.0F,
        1.0F,
    };
    commandList.RSSetViewports(1, &viewport);

    BlendMode activeBlend = static_cast<BlendMode>(0xFF);
    for (std::size_t batchIndex = 0; batchIndex < packet.batches().size(); ++batchIndex) {
        const DrawBatch& batch = packet.batches()[batchIndex];
        if (batch.instanceCount == 0) {
            continue;
        }
        ScissorRect converted{};
        if (batch.clip.enabled
            && !makeScissorRect(batch.clip.area, width, height, converted)) {
            continue;
        }
        if (activeBlend != batch.blend) {
            ID3D12PipelineState* pipeline = nullptr;
            if (usesTextures) {
                pipeline = batch.blend == BlendMode::Additive
                    ? additivePipeline.Get()
                    : alphaPipeline.Get();
            } else {
                pipeline = batch.blend == BlendMode::Additive
                    ? textureFreeAdditivePipeline.Get()
                    : textureFreeAlphaPipeline.Get();
            }
            commandList.SetPipelineState(pipeline);
            activeBlend = batch.blend;
        }

        if (usesTextures) {
            const std::uint64_t tableIndexValue = (
                static_cast<std::uint64_t>(submissionSlot) * configuration.batchCapacity + batchIndex)
                * DrawBatch::kTextureCapacity;
            const std::uint32_t tableIndex = static_cast<std::uint32_t>(tableIndexValue);
            DescriptorTableCache& cache = submission.descriptorTables[batchIndex];
            std::array<std::uint64_t, DrawBatch::kTextureCapacity> handles{};
            std::array<std::uint32_t, DrawBatch::kTextureCapacity> values{};
            std::array<std::uint64_t, DrawBatch::kTextureCapacity> revisions{};
            for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
                handles[slot] = batch.textures[slot].packed();
                values[slot] = batch.textures[slot].value();
                revisions[slot] = textures[values[slot] - 1U].descriptorRevision;
            }
            if (!cache.valid || cache.handles != handles || cache.revisions != revisions) {
                for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
                    device->CopyDescriptorsSimple(
                        1,
                        gpuCpuDescriptor(tableIndex + slot),
                        cpuDescriptor(values[slot]),
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                }
                cache.handles = handles;
                cache.revisions = revisions;
                cache.valid = true;
                ++statistics.descriptorTableCopies;
            } else {
                ++statistics.descriptorTableCacheHits;
            }
            commandList.SetGraphicsRootDescriptorTable(0, gpuDescriptor(tableIndex));
        }

        D3D12_RECT scissor{};
        if (batch.clip.enabled) {
            scissor = {converted.left, converted.top, converted.right, converted.bottom};
        } else {
            scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        }
        commandList.RSSetScissorRects(1, &scissor);
        commandList.DrawInstanced(6, batch.instanceCount, 0, batch.firstInstance);
        ++statistics.drawCalls;
        statistics.submittedInstances += batch.instanceCount;
        if (!useGpuLocal) {
            statistics.uploadHeapReadBytes +=
                static_cast<std::uint64_t>(batch.instanceCount) * sizeof(DrawInstance);
        }
    }

    const std::uint64_t cpuDrawSubmitNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - submitStarted).count());
    ++statistics.successfulFrames;
    static_cast<void>(profileTimeline.complete({
        .frameAttemptId = frameAttemptId,
        .producerIdentity = packet.identity(),
        .producerRevision = packet.revision(),
        .producerBuildNanoseconds = packet.cpuBuildNanoseconds(),
        .cpuUploadNanoseconds = cpuUploadNanoseconds,
        .cpuDrawSubmitNanoseconds = cpuDrawSubmitNanoseconds,
        .uploadedInstanceBytes = uploadedInstances ? packetBytes : 0,
        .uploadRangeCount = uploadedInstances ? 1U : 0U,
        .submissionSlot = submissionSlot,
        .uploadKind = uploadedInstances
            ? InstanceUploadKind::Full
            : InstanceUploadKind::None,
    }));
    statistics.profile = profileTimeline.profile();
    error.clear();
    return true;
}

bool D3D12Renderer::Implementation::validateCommandList(
    ID3D12GraphicsCommandList& commandList) noexcept {
    if (FAILED(device->GetDeviceRemovedReason())) {
        ++statistics.deviceRemovalRejections;
        error = "D3D12 renderer device has been removed; shutdown and recreate the renderer";
        return false;
    }
    if (commandList.GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ++statistics.commandListValidationFailures;
        error = "D3D12 UI recording requires a DIRECT command list";
        return false;
    }
    if (!validateDeviceChild(
            commandList,
            "D3D12 UI command list belongs to a different device")) {
        ++statistics.commandListValidationFailures;
        return false;
    }
    return true;
}

bool D3D12Renderer::Implementation::validateDeviceChild(
    ID3D12DeviceChild& child,
    const char* diagnostic) noexcept {
    ComPtr<ID3D12Device> childDevice;
    ComPtr<IUnknown> configuredIdentity;
    ComPtr<IUnknown> childIdentity;
    if (FAILED(child.GetDevice(IID_PPV_ARGS(&childDevice)))
        || FAILED(device.As(&configuredIdentity))
        || FAILED(childDevice.As(&childIdentity))
        || configuredIdentity.Get() != childIdentity.Get()) {
        ++statistics.lifecycleRejections;
        error = diagnostic;
        return false;
    }
    return true;
}

bool D3D12Renderer::Implementation::validateSubmissionReuse(
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    if (submissionReuse.completionFence == nullptr && submissionReuse.completionValue == 0) {
        return true;
    }
    if (submissionReuse.completionFence == nullptr || submissionReuse.completionValue == 0) {
        ++statistics.lifecycleRejections;
        error = "D3D12 submission reuse requires both a fence and non-zero completion value";
        return false;
    }
    ++statistics.submissionFenceChecks;
    if (!validateDeviceChild(
            *submissionReuse.completionFence,
            "D3D12 submission reuse fence belongs to a different device")) {
        return false;
    }
    const std::uint64_t completed = submissionReuse.completionFence->GetCompletedValue();
    if (completed == std::numeric_limits<std::uint64_t>::max()) {
        ++statistics.deviceRemovalRejections;
        error = "D3D12 submission reuse fence reported device removal";
        return false;
    }
    if (completed < submissionReuse.completionValue) {
        ++statistics.submissionSlotBusyRejections;
        error = "D3D12 submission slot is still referenced by the GPU";
        return false;
    }
    return true;
}

void D3D12Renderer::Implementation::shutdown() noexcept {
    for (Submission& submission : submissions) {
        if (submission.uploadInstances != nullptr && submission.mapped != nullptr) {
            submission.uploadInstances->Unmap(0, nullptr);
        }
        submission.mapped = nullptr;
        submission.directUploadedIdentity = 0;
        submission.directUploadedRevision = 0;
        submission.gpuLocalUploadedIdentity = 0;
        submission.gpuLocalUploadedRevision = 0;
        submission.retainedTextures.clear();
        submission.gpuLocalInstances.Reset();
        submission.uploadInstances.Reset();
    }
    submissions.clear();
    textures.clear();
    scheduledTextureHandles.clear();
    scheduledTextureRevisions.clear();
    dirtyTextures.clear();
    textureUploadBatches.clear();
    textureUploadFence.Reset();
    textureUploadQueue.Reset();
    gpuBatchHeap.Reset();
    cpuTextureHeap.Reset();
    additivePipeline.Reset();
    alphaPipeline.Reset();
    textureFreeAdditivePipeline.Reset();
    textureFreeAlphaPipeline.Reset();
    textureFreeRootSignature.Reset();
    rootSignature.Reset();
    device.Reset();
    configuration = {};
    renderTargetFormat = DXGI_FORMAT_UNKNOWN;
    descriptorStride = 0;
    instanceBufferBytes = 0;
    cpuDescriptorCapacity = 0;
    gpuDescriptorCapacity = 0;
    nextTextureUploadFenceValue = 1;
    nextDescriptorRevision = 1;
    adapterArchitectureKnown = false;
    adapterUma = true;
    gpuLocalResourcesEnabled = false;
    ready = false;
    statistics.gpuLocalResidentBytes = 0;
    statistics.gpuTextureBytes = 0;
    statistics.externalTextures = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Renderer::Implementation::cpuDescriptor(std::uint32_t index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuTextureHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * descriptorStride;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Renderer::Implementation::gpuCpuDescriptor(std::uint32_t index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = gpuBatchHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * descriptorStride;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Renderer::Implementation::gpuDescriptor(std::uint32_t index) const noexcept {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = gpuBatchHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * descriptorStride;
    return handle;
}

D3D12Renderer::D3D12Renderer() : mImplementation(std::make_unique<Implementation>()) {}

D3D12Renderer::~D3D12Renderer() { mImplementation->shutdown(); }

bool D3D12Renderer::initialize(
    ID3D12Device& device,
    DXGI_FORMAT renderTargetFormat,
    D3D12RendererConfiguration configuration) noexcept {
    try {
        return mImplementation->initialize(device, renderTargetFormat, configuration);
    } catch (...) {
        mImplementation->shutdown();
        mImplementation->error = "D3D12 renderer initialization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool D3D12Renderer::synchronizeTextures(
    TextureStore& textures,
    ID3D12CommandQueue& directQueue) noexcept {
    try {
        return mImplementation->synchronizeTextures(textures, directQueue);
    } catch (...) {
        mImplementation->error = "D3D12 texture synchronization exhausted CPU bookkeeping storage";
        return false;
    }
}

bool D3D12Renderer::bindExternalTexture(
    const TextureStore& textures,
    TextureHandle handle,
    ID3D12Resource& texture) noexcept {
    return mImplementation->bindExternalTexture(textures, handle, texture);
}

bool D3D12Renderer::pollTextureUploads() noexcept {
    return mImplementation->pollTextureUploads();
}

bool D3D12Renderer::record(
    const RenderPacket& packet,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    return mImplementation->record(
        packet,
        commandList,
        submissionSlot,
        viewportWidth,
        viewportHeight,
        submissionReuse);
}

bool D3D12Renderer::reportGpuTime(
    std::uint64_t sampleId,
    std::uint64_t nanoseconds) noexcept {
    if (!mImplementation->ready) return false;
    const bool reported = mImplementation->profileTimeline.reportGpuTime(sampleId, nanoseconds);
    mImplementation->statistics.profile = mImplementation->profileTimeline.profile();
    return reported;
}

void D3D12Renderer::shutdown() noexcept { mImplementation->shutdown(); }

bool D3D12Renderer::initialized() const noexcept { return mImplementation->ready; }

std::size_t D3D12Renderer::instanceCapacity() const noexcept {
    return mImplementation->configuration.instanceCapacity;
}

std::uint32_t D3D12Renderer::submissionCapacity() const noexcept {
    return mImplementation->configuration.submissionCapacity;
}

std::uint32_t D3D12Renderer::pendingTextureUploadBatches() const noexcept {
    std::uint32_t pending = 0;
    for (const Implementation::TextureUploadBatch& batch : mImplementation->textureUploadBatches) {
        if (batch.transaction.state() == detail::D3D12TextureUploadState::Pending) {
            ++pending;
        }
    }
    return pending;
}

D3D12RenderStatistics D3D12Renderer::statistics() const noexcept {
    return mImplementation->statistics;
}

std::string_view D3D12Renderer::lastError() const noexcept { return mImplementation->error.view(); }

} // namespace henia::ui
