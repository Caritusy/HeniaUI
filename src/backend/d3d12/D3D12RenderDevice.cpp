#include "henia/gfx/backend/d3d12/D3D12RenderDevice.h"
#include "henia/CheckedArithmetic.h"
#include "henia/gfx/Validation.h"

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

namespace henia::gfx {
namespace {

using Microsoft::WRL::ComPtr;

static_assert(std::is_standard_layout_v<BoxInstance>);
static_assert(sizeof(BoxInstance) == 64);

constexpr std::size_t kDepthPipelineCount = 15;

constexpr const char* kShaderSource = R"hlsl(
cbuffer FrameConstants : register(b0) {
    float4x4 viewProjection;
    float2 viewportSize;
    float timeSeconds;
    uint frameFlags;
};

struct VertexInput {
    float4 minimumAndWidth : INSTANCE_MINIMUM_WIDTH;
    float4 maximumAndHue : INSTANCE_MAXIMUM_HUE;
    float4 color : INSTANCE_COLOR;
    uint effects : INSTANCE_EFFECTS;
    uint vertexId : SV_VertexID;
};

struct PixelInput {
    float4 position : SV_Position;
    nointerpolation float4 color : COLOR;
    noperspective float edgeAcross : EDGE_ACROSS;
    noperspective float edgeAlong : EDGE_ALONG;
    nointerpolation float segmentLength : SEGMENT_LENGTH;
    nointerpolation float halfWidth : HALF_WIDTH;
    nointerpolation float hueOffset : HUE_OFFSET;
    nointerpolation uint effects : EFFECTS;
    nointerpolation float validEdge : VALID_EDGE;
};

static const int2 edges[12] = {
    int2(0, 1), int2(2, 3), int2(0, 2), int2(1, 3),
    int2(4, 5), int2(6, 7), int2(4, 6), int2(5, 7),
    int2(0, 4), int2(1, 5), int2(2, 6), int2(3, 7)
};
static const float2 quad[6] = {
    float2(0.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
    float2(0.0, -1.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

float3 corner(int code) {
    return float3(float(code & 1), float((code >> 1) & 1), float((code >> 2) & 1));
}

float planeDistance(float4 clipPoint, int planeIndex, bool zeroToOne) {
    if (planeIndex == 0) return clipPoint.w + clipPoint.x;
    if (planeIndex == 1) return clipPoint.w - clipPoint.x;
    if (planeIndex == 2) return clipPoint.w + clipPoint.y;
    if (planeIndex == 3) return clipPoint.w - clipPoint.y;
    if (planeIndex == 4) return zeroToOne ? clipPoint.z : clipPoint.w + clipPoint.z;
    if (planeIndex == 5) return clipPoint.w - clipPoint.z;
    return clipPoint.w - 0.0001;
}

bool clipAgainstPlane(
    inout float4 startClip,
    inout float4 finishClip,
    int plane,
    bool zeroToOne) {
    float startDistance = planeDistance(startClip, plane, zeroToOne);
    float finishDistance = planeDistance(finishClip, plane, zeroToOne);
    if (!isfinite(startDistance) || !isfinite(finishDistance)) return false;
    bool startInside = startDistance >= 0.0;
    bool finishInside = finishDistance >= 0.0;
    if (!startInside && !finishInside) return false;
    if (startInside && finishInside) return true;

    float denominator = startDistance - finishDistance;
    if (abs(denominator) <= 1e-20 || !isfinite(denominator)) return false;
    float amount = clamp(startDistance / denominator, 0.0, 1.0);
    if (!isfinite(amount)) return false;
    float4 clipped = lerp(startClip, finishClip, amount);
    if (!all(isfinite(clipped))) return false;
    if (startInside) {
        finishClip = clipped;
    } else {
        startClip = clipped;
    }
    return true;
}

bool clipSegment(inout float4 startClip, inout float4 finishClip, bool zeroToOne) {
    if (!all(isfinite(startClip)) || !all(isfinite(finishClip))
        || !clipAgainstPlane(startClip, finishClip, 6, zeroToOne)) {
        return false;
    }
    [unroll]
    for (int plane = 0; plane < 6; ++plane) {
        if (!clipAgainstPlane(startClip, finishClip, plane, zeroToOne)) {
            return false;
        }
    }
    return all(isfinite(startClip)) && all(isfinite(finishClip))
        && startClip.w >= 0.0001 && finishClip.w >= 0.0001;
}

PixelInput vertexMain(VertexInput input) {
    PixelInput output;
    int edgeIndex = input.vertexId / 6;
    float2 vertex = quad[input.vertexId % 6];
    float3 start = lerp(input.minimumAndWidth.xyz, input.maximumAndHue.xyz, corner(edges[edgeIndex].x));
    float3 finish = lerp(input.minimumAndWidth.xyz, input.maximumAndHue.xyz, corner(edges[edgeIndex].y));
    float4 startClip = mul(viewProjection, float4(start, 1.0));
    float4 finishClip = mul(viewProjection, float4(finish, 1.0));
    bool zeroToOne = (frameFlags & 1u) == 0u;
    output.validEdge = clipSegment(startClip, finishClip, zeroToOne) ? 1.0 : 0.0;

    output.halfWidth = max(input.minimumAndWidth.w, 0.5) * 0.5;
    float fringe = 1.25;
    float expandedWidth = output.halfWidth + fringe;
    output.color = input.color;
    output.edgeAcross = 0.0;
    output.edgeAlong = 0.0;
    output.segmentLength = 0.0;
    output.hueOffset = input.maximumAndHue.w;
    output.effects = input.effects;
    if (output.validEdge < 0.5) {
        output.position = float4(2.0, 2.0, 2.0, 1.0);
        return output;
    }

    float2 startNdc = startClip.xy / startClip.w;
    float2 finishNdc = finishClip.xy / finishClip.w;
    float2 directionPixels = (finishNdc - startNdc) * viewportSize * 0.5;
    output.segmentLength = length(directionPixels);
    float2 direction = output.segmentLength > 0.0001
        ? directionPixels / output.segmentLength
        : float2(1.0, 0.0);
    float2 normalPixels = float2(-direction.y, direction.x);
    bool finishVertex = vertex.x > 0.5;
    output.edgeAlong = finishVertex ? output.segmentLength + fringe : -fringe;
    output.edgeAcross = vertex.y * expandedWidth;
    float capOffset = finishVertex ? fringe : -fringe;
    float2 offsetPixels = direction * capOffset
        + normalPixels * expandedWidth * vertex.y;
    float2 offsetNdc = offsetPixels * 2.0 / viewportSize;
    float4 endpoint = finishVertex ? finishClip : startClip;
    endpoint.xy += offsetNdc * endpoint.w;
    if (!zeroToOne) {
        endpoint.z = endpoint.z * 0.5 + endpoint.w * 0.5;
    }
    output.position = endpoint;
    return output;
}

float3 hue(float value) {
    float3 shifted = abs(frac(value + float3(0.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return saturate(shifted - 1.0);
}

float4 pixelMain(PixelInput input) : SV_Target {
    clip(input.validEdge - 0.5);
    float2 centered = float2(
        input.edgeAlong - input.segmentLength * 0.5,
        input.edgeAcross);
    float2 outside = abs(centered) - float2(input.segmentLength * 0.5, input.halfWidth);
    float distanceToEdge = length(max(outside, 0.0))
        + min(max(outside.x, outside.y), 0.0);
    float antiAlias = max(fwidth(distanceToEdge), 0.75);
    float coverage = 1.0 - smoothstep(-antiAlias, antiAlias, distanceToEdge);
    float4 color = input.color;
    if ((input.effects & 1u) != 0u) {
        color.rgb *= hue(frac(timeSeconds * 0.08 + input.hueOffset));
    }
    color.a *= coverage;
    clip(color.a - 0.001);
    return float4(color.rgb * color.a, color.a);
}
)hlsl";

struct FrameConstants final {
    std::array<float, 16> viewProjection{};
    std::array<float, 2> viewport{};
    float timeSeconds = 0.0F;
    std::uint32_t flags = 0;
};
static_assert(sizeof(FrameConstants) == 80);

[[nodiscard]] D3D12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE type) noexcept {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

[[nodiscard]] D3D12_RESOURCE_DESC bufferDescription(std::uint64_t size) noexcept {
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = size;
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
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

[[nodiscard]] bool compileShader(
    const char* entry,
    const char* target,
    ComPtr<ID3DBlob>& output,
    henia::detail::FixedError& error) noexcept {
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        kShaderSource,
        std::strlen(kShaderSource),
        "HeniaUI.Gfx",
        nullptr,
        nullptr,
        entry,
        target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &output,
        &errors);
    if (SUCCEEDED(result)) return true;
    if (errors != nullptr && errors->GetBufferPointer() != nullptr) {
        error.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
    } else {
        error = "D3DCompile failed without gfx diagnostics";
    }
    return false;
}

[[nodiscard]] D3D12_COMPARISON_FUNC compareFunction(CompareOp operation) noexcept {
    switch (operation) {
        case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
        case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
        case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
}

[[nodiscard]] std::size_t pipelineIndex(DepthState state) noexcept {
    if (!state.enabled) return 0;
    return 1 + static_cast<std::size_t>(state.compare) * 2 + (state.writeEnabled ? 1U : 0U);
}

[[nodiscard]] D3D12_BLEND_DESC blendDescription() noexcept {
    D3D12_BLEND_DESC description{};
    D3D12_RENDER_TARGET_BLEND_DESC& target = description.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D12_BLEND_ONE;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return description;
}

[[nodiscard]] D3D12_RASTERIZER_DESC rasterizerDescription() noexcept {
    D3D12_RASTERIZER_DESC description{};
    description.FillMode = D3D12_FILL_MODE_SOLID;
    description.CullMode = D3D12_CULL_MODE_NONE;
    description.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    description.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    description.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    description.DepthClipEnable = TRUE;
    return description;
}

[[nodiscard]] D3D12_DEPTH_STENCIL_DESC depthDescription(DepthState state) noexcept {
    D3D12_DEPTH_STENCIL_DESC description{};
    description.DepthEnable = state.enabled ? TRUE : FALSE;
    description.DepthWriteMask = state.writeEnabled
        ? D3D12_DEPTH_WRITE_MASK_ALL
        : D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthFunc = compareFunction(state.compare);
    description.StencilEnable = FALSE;
    return description;
}

[[nodiscard]] std::uint64_t elapsedNanoseconds(std::chrono::steady_clock::time_point started) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count());
}

} // namespace

struct D3D12RenderDevice::Implementation final {
    struct Submission final {
        ComPtr<ID3D12Resource> uploadInstances;
        ComPtr<ID3D12Resource> gpuLocalInstances;
        std::byte* mapped = nullptr;
        std::uint64_t directUploadedIdentity = 0;
        std::uint64_t directUploadedRevision = 0;
        std::uint64_t gpuLocalUploadedIdentity = 0;
        std::uint64_t gpuLocalUploadedRevision = 0;
    };

    ComPtr<ID3D12Device> ownerDevice;
    ComPtr<ID3D12RootSignature> rootSignature;
    std::array<ComPtr<ID3D12PipelineState>, kDepthPipelineCount> pipelines;
    std::vector<Submission> submissions;
    D3D12GfxConfiguration configuration{};
    D3D12GfxStatistics statistics{};
    henia::detail::ProfileTimeline profileTimeline;
    henia::detail::FixedError error;
    std::uint32_t instanceBufferBytes = 0;
    bool adapterArchitectureKnown = false;
    bool adapterUma = true;
    bool gpuLocalResourcesEnabled = false;
    bool ready = false;

    [[nodiscard]] bool initialize(ID3D12Device& device, D3D12GfxConfiguration value);
    [[nodiscard]] bool createPipeline(
        ID3D12Device& device,
        ID3DBlob& vertexShader,
        ID3DBlob& pixelShader,
        DepthState depth) noexcept;
    [[nodiscard]] bool record(
        const InstanceBatch& batch,
        const ViewParameters& view,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept;
    [[nodiscard]] bool validateCommandList(ID3D12GraphicsCommandList& commandList) noexcept;
    [[nodiscard]] bool validateDeviceChild(
        ID3D12DeviceChild& child,
        const char* diagnostic) noexcept;
    [[nodiscard]] bool validateSubmissionReuse(
        henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept;
    void shutdown() noexcept;
};

bool D3D12RenderDevice::Implementation::initialize(
    ID3D12Device& device, D3D12GfxConfiguration value) {
    if (ready) {
        ComPtr<IUnknown> configuredIdentity;
        ComPtr<IUnknown> requestedIdentity;
        if (FAILED(ownerDevice.As(&configuredIdentity))
            || FAILED(device.QueryInterface(IID_PPV_ARGS(&requestedIdentity)))
            || configuredIdentity.Get() != requestedIdentity.Get()) {
            ++statistics.lifecycleRejections;
            error = "D3D12 gfx renderer is already initialized with a different device";
            return false;
        }
        if (value.boxCapacity != configuration.boxCapacity
            || value.submissionCapacity != configuration.submissionCapacity
            || value.renderTargetFormat != configuration.renderTargetFormat
            || value.depthStencilFormat != configuration.depthStencilFormat
            || value.sampleCount != configuration.sampleCount
            || value.instanceStorage != configuration.instanceStorage
            || value.gpuLocalInstanceThresholdBytes
                != configuration.gpuLocalInstanceThresholdBytes) {
            ++statistics.lifecycleRejections;
            error = "D3D12 gfx renderer is already initialized with a different configuration";
            return false;
        }
        if (FAILED(ownerDevice->GetDeviceRemovedReason())) {
            ++statistics.deviceRemovalRejections;
            error = "D3D12 gfx device has been removed; shutdown and recreate the renderer";
            return false;
        }
        error.clear();
        return true;
    }
    std::size_t instanceBytes = 0;
    if (value.boxCapacity == 0 || value.submissionCapacity == 0 || value.sampleCount == 0
        || value.renderTargetFormat == DXGI_FORMAT_UNKNOWN
        || !validInstanceStorageStrategy(value.instanceStorage)
        || !checkedMultiply(value.boxCapacity, sizeof(BoxInstance), instanceBytes)
        || instanceBytes > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
        error = "D3D12 gfx configuration has an invalid box capacity, sample count, or format";
        return false;
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT renderTargetSupport{
        .Format = value.renderTargetFormat,
    };
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS renderTargetSamples{
        .Format = value.renderTargetFormat,
        .SampleCount = value.sampleCount,
    };
    if (FAILED(device.CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT,
            &renderTargetSupport,
            sizeof(renderTargetSupport)))
        || (renderTargetSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0
        || FAILED(device.CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &renderTargetSamples,
            sizeof(renderTargetSamples)))
        || renderTargetSamples.NumQualityLevels == 0) {
        error = "D3D12 gfx render-target format/sample count is unsupported by the configured device";
        return false;
    }
    if (value.depthStencilFormat != DXGI_FORMAT_UNKNOWN) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT depthSupport{
            .Format = value.depthStencilFormat,
        };
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depthSamples{
            .Format = value.depthStencilFormat,
            .SampleCount = value.sampleCount,
        };
        if (FAILED(device.CheckFeatureSupport(
                D3D12_FEATURE_FORMAT_SUPPORT,
                &depthSupport,
                sizeof(depthSupport)))
            || (depthSupport.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) == 0
            || FAILED(device.CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &depthSamples,
                sizeof(depthSamples)))
            || depthSamples.NumQualityLevels == 0) {
            error = "D3D12 gfx depth format/sample count is unsupported by the configured device";
            return false;
        }
    }
    ownerDevice = &device;
    configuration = value;
    const AdapterArchitecture architecture = queryAdapterArchitecture(device);
    adapterArchitectureKnown = architecture.known;
    adapterUma = architecture.uma;
    using Strategy = henia::backend::d3d12::InstanceStorageStrategy;
    gpuLocalResourcesEnabled = value.instanceStorage == Strategy::GpuLocal
        || (value.instanceStorage == Strategy::Automatic
            && architecture.known && !architecture.uma
            && instanceBytes >= value.gpuLocalInstanceThresholdBytes);
    instanceBufferBytes = static_cast<std::uint32_t>(instanceBytes);
    submissions.resize(configuration.submissionCapacity);
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!compileShader("vertexMain", "vs_5_1", vertexShader, error)
        || !compileShader("pixelMain", "ps_5_1", pixelShader, error)) {
        return false;
    }
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister = 0;
    parameter.Constants.RegisterSpace = 0;
    parameter.Constants.Num32BitValues = sizeof(FrameConstants) / sizeof(std::uint32_t);
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = 1;
    rootDescription.pParameters = &parameter;
    rootDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> rootErrors;
    if (FAILED(D3D12SerializeRootSignature(
            &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &rootErrors))
        || FAILED(device.CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) {
        error = "D3D12 failed to create the gfx root signature";
        return false;
    }
    if (!createPipeline(device, *vertexShader.Get(), *pixelShader.Get(), {})) return false;
    if (configuration.depthStencilFormat != DXGI_FORMAT_UNKNOWN) {
        for (std::uint8_t operation = 0; operation <= static_cast<std::uint8_t>(CompareOp::Always); ++operation) {
            for (bool write : {false, true}) {
                if (!createPipeline(device, *vertexShader.Get(), *pixelShader.Get(), {
                        .enabled = true,
                        .writeEnabled = write,
                        .compare = static_cast<CompareOp>(operation),
                    })) return false;
            }
        }
    }

    const std::uint64_t bufferBytes = instanceBufferBytes;
    const D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const D3D12_RESOURCE_DESC buffer = bufferDescription(bufferBytes);
    for (Submission& submission : submissions) {
        if (FAILED(device.CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &buffer,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&submission.uploadInstances)))) {
            error = "D3D12 failed to allocate a gfx submission buffer";
            shutdown();
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(submission.uploadInstances->Map(0, &noRead, &mapped)) || mapped == nullptr) {
            error = "D3D12 failed to map a gfx submission buffer";
            shutdown();
            return false;
        }
        submission.mapped = static_cast<std::byte*>(mapped);
        if (gpuLocalResourcesEnabled
            && FAILED(device.CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &buffer,
                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                nullptr,
                IID_PPV_ARGS(&submission.gpuLocalInstances)))) {
            error = "D3D12 failed to allocate a GPU-local gfx instance buffer";
            shutdown();
            return false;
        }
    }
    statistics = {};
    profileTimeline.reset();
    statistics.adapterArchitectureKnown = adapterArchitectureKnown;
    statistics.adapterUma = adapterUma;
    if (gpuLocalResourcesEnabled) {
        statistics.gpuLocalResidentBytes = static_cast<std::uint64_t>(instanceBufferBytes)
            * configuration.submissionCapacity;
    }
    ready = true;
    error.clear();
    return true;
}

bool D3D12RenderDevice::Implementation::createPipeline(
    ID3D12Device& device, ID3DBlob& vertexShader, ID3DBlob& pixelShader, DepthState depth) noexcept {
    constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 4> inputs{{
        {"INSTANCE_MINIMUM_WIDTH", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCE_MAXIMUM_HUE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        {"INSTANCE_EFFECTS", 0, DXGI_FORMAT_R32_UINT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
    }};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature.Get();
    description.VS = {vertexShader.GetBufferPointer(), vertexShader.GetBufferSize()};
    description.PS = {pixelShader.GetBufferPointer(), pixelShader.GetBufferSize()};
    description.BlendState = blendDescription();
    description.SampleMask = UINT_MAX;
    description.RasterizerState = rasterizerDescription();
    description.DepthStencilState = depthDescription(depth);
    description.InputLayout = {inputs.data(), static_cast<UINT>(inputs.size())};
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = configuration.renderTargetFormat;
    description.DSVFormat = depth.enabled ? configuration.depthStencilFormat : DXGI_FORMAT_UNKNOWN;
    description.SampleDesc.Count = configuration.sampleCount;
    if (FAILED(device.CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&pipelines[pipelineIndex(depth)])))) {
        error = "D3D12 failed to create a gfx box pipeline";
        return false;
    }
    return true;
}

bool D3D12RenderDevice::Implementation::record(
    const InstanceBatch& batch,
    const ViewParameters& view,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    const std::uint64_t frameAttemptId = ++statistics.frameAttempts;
    const BoxInstanceView boxes = batch.boxes();
    if (!ready || submissionSlot >= submissions.size()) {
        ++statistics.rejectedFrames;
        error = "D3D12 gfx renderer or submissionSlot is unavailable";
        return false;
    }
    if (!validateCommandList(commandList)) {
        ++statistics.rejectedFrames;
        return false;
    }
    if (const std::string_view issue = validate(view); !issue.empty()) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error.assign(issue.data(), issue.size());
        return false;
    }
    if (const std::string_view issue = validate(batch.depthState()); !issue.empty()) {
        ++statistics.rejectedFrames;
        ++statistics.invalidInputFrames;
        error.assign(issue.data(), issue.size());
        return false;
    }
    if (boxes.size() > configuration.boxCapacity
        || boxes.size() > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
        ++statistics.rejectedFrames;
        ++statistics.capacityRejectedFrames;
        error = "D3D12 gfx instance count exceeds boxCapacity";
        return false;
    }
    for (std::size_t pageIndex = 0; pageIndex < batch.boxPageCount(); ++pageIndex) {
        for (const BoxInstance& box : batch.boxPage(pageIndex)) {
            if (const std::string_view issue = validate(box); !issue.empty()) {
                ++statistics.rejectedFrames;
                ++statistics.invalidInputFrames;
                error.assign(issue.data(), issue.size());
                return false;
            }
        }
    }
    if (!validateSubmissionReuse(submissionReuse)) {
        ++statistics.rejectedFrames;
        return false;
    }
    Submission& submission = submissions[submissionSlot];
    const std::size_t submittedBytes = boxes.size() * sizeof(BoxInstance);
    using Strategy = henia::backend::d3d12::InstanceStorageStrategy;
    const bool useGpuLocal = gpuLocalResourcesEnabled
        && (configuration.instanceStorage == Strategy::GpuLocal
            || submittedBytes >= configuration.gpuLocalInstanceThresholdBytes);
    std::uint64_t& uploadedIdentity = useGpuLocal
        ? submission.gpuLocalUploadedIdentity
        : submission.directUploadedIdentity;
    std::uint64_t& uploadedRevision = useGpuLocal
        ? submission.gpuLocalUploadedRevision
        : submission.directUploadedRevision;
    std::uint64_t cpuUploadNanoseconds = 0;
    std::uint64_t profileUploadedBytes = 0;
    std::uint32_t profileUploadRangeCount = 0;
    InstanceUploadKind profileUploadKind = InstanceUploadKind::None;
    if (uploadedIdentity != batch.identity() || uploadedRevision != batch.revision()) {
        const auto uploadStarted = std::chrono::steady_clock::now();
        const std::span<const DirtyRange> dirtyRanges = batch.dirtyRanges();
        bool dirtyRangesValid = !dirtyRanges.empty();
        std::size_t previousEnd = 0;
        for (const DirtyRange range : dirtyRanges) {
            const bool valid = range.count > 0 && range.offset >= previousEnd
                && range.offset <= boxes.size() && range.count <= boxes.size() - range.offset;
            if (!valid) {
                dirtyRangesValid = false;
                break;
            }
            previousEnd = range.offset + range.count;
        }
        const bool partial = uploadedIdentity == batch.identity()
            && uploadedRevision != std::numeric_limits<std::uint64_t>::max()
            && uploadedRevision + 1 == batch.revision()
            && !batch.requiresFullUpload() && dirtyRangesValid;
        const auto validByteRange = [&](DirtyRange range) noexcept {
            std::size_t offsetBytes = 0;
            std::size_t countBytes = 0;
            return checkedMultiply(range.offset, sizeof(BoxInstance), offsetBytes)
                && checkedMultiply(range.count, sizeof(BoxInstance), countBytes)
                && offsetBytes <= instanceBufferBytes
                && countBytes <= instanceBufferBytes - offsetBytes;
        };
        bool rangesValid = true;
        if (partial) {
            for (const DirtyRange range : dirtyRanges) {
                rangesValid = rangesValid && validByteRange(range);
            }
        } else if (!boxes.empty()) {
            rangesValid = validByteRange({0, boxes.size()});
        }
        if (!rangesValid) {
            ++statistics.rejectedFrames;
            ++statistics.capacityRejectedFrames;
            error = "D3D12 gfx upload byte range exceeds the instance buffer";
            return false;
        }

        std::size_t uploadedBytes = 0;
        std::uint64_t copyOperations = 0;
        const auto stageRange = [&](DirtyRange range) noexcept {
            const std::size_t offsetBytes = range.offset * sizeof(BoxInstance);
            const std::size_t countBytes = range.count * sizeof(BoxInstance);
            std::size_t sourceOffset = range.offset;
            std::size_t destinationOffset = offsetBytes;
            std::size_t remaining = range.count;
            while (remaining > 0) {
                const std::span<const BoxInstance> page = batch.boxPage(
                    sourceOffset / InstanceBatch::kBoxesPerPage);
                const std::size_t localOffset = sourceOffset % InstanceBatch::kBoxesPerPage;
                const std::size_t pageCount = std::min(remaining, page.size() - localOffset);
                const std::size_t pageBytes = pageCount * sizeof(BoxInstance);
                std::memcpy(
                    submission.mapped + destinationOffset,
                    page.data() + localOffset,
                    pageBytes);
                sourceOffset += pageCount;
                destinationOffset += pageBytes;
                remaining -= pageCount;
            }
            if (useGpuLocal && countBytes != 0) {
                commandList.CopyBufferRegion(
                    submission.gpuLocalInstances.Get(),
                    offsetBytes,
                    submission.uploadInstances.Get(),
                    offsetBytes,
                    countBytes);
                ++copyOperations;
            }
            uploadedBytes += countBytes;
        };

        const bool hasUploadRanges = partial ? !dirtyRanges.empty() : !boxes.empty();
        D3D12_RESOURCE_BARRIER transition{};
        if (useGpuLocal && hasUploadRanges) {
            transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            transition.Transition.pResource = submission.gpuLocalInstances.Get();
            transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            transition.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            commandList.ResourceBarrier(1, &transition);
        }
        if (partial) {
            for (const DirtyRange range : dirtyRanges) {
                stageRange(range);
            }
        } else if (!boxes.empty()) {
            stageRange({0, boxes.size()});
        }
        if (useGpuLocal && hasUploadRanges) {
            std::swap(transition.Transition.StateBefore, transition.Transition.StateAfter);
            commandList.ResourceBarrier(1, &transition);
            statistics.instanceCopyOperations += copyOperations;
            statistics.copiedInstanceBytes += uploadedBytes;
        }
        uploadedIdentity = batch.identity();
        uploadedRevision = batch.revision();
        if (boxes.empty()) {
            ++statistics.zeroWorkInstanceRevisions;
            profileUploadKind = InstanceUploadKind::ZeroWorkRevision;
        } else if (partial) {
            ++statistics.partialInstanceUploads;
            profileUploadKind = InstanceUploadKind::DirtyRanges;
            profileUploadRangeCount = static_cast<std::uint32_t>(dirtyRanges.size());
        } else {
            ++statistics.fullInstanceUploads;
            profileUploadKind = InstanceUploadKind::Full;
            profileUploadRangeCount = 1;
        }
        statistics.uploadedInstanceBytes += uploadedBytes;
        profileUploadedBytes = uploadedBytes;
        cpuUploadNanoseconds = elapsedNanoseconds(uploadStarted);
    }
    if (useGpuLocal) ++statistics.gpuLocalFrames;
    else ++statistics.directUploadFrames;

    DepthState depth = batch.depthState();
    if (depth.enabled && configuration.depthStencilFormat == DXGI_FORMAT_UNKNOWN) {
        depth.enabled = false;
        depth.writeEnabled = false;
        ++statistics.depthFallbacks;
    }
    ID3D12PipelineState* pipeline = pipelines[pipelineIndex(depth)].Get();
    if (pipeline == nullptr) {
        ++statistics.rejectedFrames;
        error = "D3D12 gfx depth pipeline is unavailable";
        return false;
    }
    const auto submitStarted = std::chrono::steady_clock::now();
    const FrameConstants constants{
        .viewProjection = view.viewProjection.values,
        .viewport = {view.viewport.x, view.viewport.y},
        .timeSeconds = view.timeSeconds,
        .flags = view.clipDepthRange == ClipDepthRange::MinusOneToOne ? 1U : 0U,
    };
    commandList.SetPipelineState(pipeline);
    commandList.SetGraphicsRootSignature(rootSignature.Get());
    commandList.SetGraphicsRoot32BitConstants(0, sizeof(constants) / sizeof(std::uint32_t), &constants, 0);
    const D3D12_VIEWPORT viewport{0.0F, 0.0F, view.viewport.x, view.viewport.y, 0.0F, 1.0F};
    const D3D12_RECT scissor{
        0,
        0,
        static_cast<LONG>(std::ceil(view.viewport.x)),
        static_cast<LONG>(std::ceil(view.viewport.y)),
    };
    commandList.RSSetViewports(1, &viewport);
    commandList.RSSetScissorRects(1, &scissor);
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12Resource* vertexInstances = useGpuLocal
        ? submission.gpuLocalInstances.Get()
        : submission.uploadInstances.Get();
    const D3D12_VERTEX_BUFFER_VIEW bufferView{
        vertexInstances->GetGPUVirtualAddress(),
        instanceBufferBytes,
        static_cast<UINT>(sizeof(BoxInstance)),
    };
    commandList.IASetVertexBuffers(0, 1, &bufferView);
    ++statistics.viewUpdates;
    if (!boxes.empty()) {
        commandList.DrawInstanced(72, static_cast<UINT>(boxes.size()), 0, 0);
        ++statistics.drawCalls;
        statistics.submittedInstances += boxes.size();
        if (!useGpuLocal) statistics.uploadHeapReadBytes += submittedBytes;
    }
    const std::uint64_t cpuDrawSubmitNanoseconds = elapsedNanoseconds(submitStarted);
    ++statistics.successfulFrames;
    static_cast<void>(profileTimeline.complete({
        .frameAttemptId = frameAttemptId,
        .producerIdentity = batch.identity(),
        .producerRevision = batch.revision(),
        .producerBuildNanoseconds = batch.cpuBuildNanoseconds(),
        .cpuUploadNanoseconds = cpuUploadNanoseconds,
        .cpuDrawSubmitNanoseconds = cpuDrawSubmitNanoseconds,
        .uploadedInstanceBytes = profileUploadedBytes,
        .uploadRangeCount = profileUploadRangeCount,
        .submissionSlot = submissionSlot,
        .uploadKind = profileUploadKind,
    }));
    statistics.profile = profileTimeline.profile();
    error.clear();
    return true;
}

bool D3D12RenderDevice::Implementation::validateCommandList(
    ID3D12GraphicsCommandList& commandList) noexcept {
    if (FAILED(ownerDevice->GetDeviceRemovedReason())) {
        ++statistics.deviceRemovalRejections;
        error = "D3D12 gfx device has been removed; shutdown and recreate the renderer";
        return false;
    }
    if (commandList.GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        ++statistics.commandListValidationFailures;
        error = "D3D12 gfx recording requires a DIRECT command list";
        return false;
    }
    if (!validateDeviceChild(
            commandList,
            "D3D12 gfx command list belongs to a different device")) {
        ++statistics.commandListValidationFailures;
        return false;
    }
    return true;
}

bool D3D12RenderDevice::Implementation::validateDeviceChild(
    ID3D12DeviceChild& child,
    const char* diagnostic) noexcept {
    ComPtr<ID3D12Device> childDevice;
    ComPtr<IUnknown> configuredIdentity;
    ComPtr<IUnknown> childIdentity;
    if (FAILED(child.GetDevice(IID_PPV_ARGS(&childDevice)))
        || FAILED(ownerDevice.As(&configuredIdentity))
        || FAILED(childDevice.As(&childIdentity))
        || configuredIdentity.Get() != childIdentity.Get()) {
        ++statistics.lifecycleRejections;
        error = diagnostic;
        return false;
    }
    return true;
}

bool D3D12RenderDevice::Implementation::validateSubmissionReuse(
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    if (submissionReuse.completionFence == nullptr && submissionReuse.completionValue == 0) {
        return true;
    }
    if (submissionReuse.completionFence == nullptr || submissionReuse.completionValue == 0) {
        ++statistics.lifecycleRejections;
        error = "D3D12 gfx submission reuse requires both a fence and non-zero completion value";
        return false;
    }
    ++statistics.submissionFenceChecks;
    if (!validateDeviceChild(
            *submissionReuse.completionFence,
            "D3D12 gfx submission reuse fence belongs to a different device")) {
        return false;
    }
    const std::uint64_t completed = submissionReuse.completionFence->GetCompletedValue();
    if (completed == std::numeric_limits<std::uint64_t>::max()) {
        ++statistics.deviceRemovalRejections;
        error = "D3D12 gfx submission reuse fence reported device removal";
        return false;
    }
    if (completed < submissionReuse.completionValue) {
        ++statistics.submissionSlotBusyRejections;
        error = "D3D12 gfx submission slot is still referenced by the GPU";
        return false;
    }
    return true;
}

void D3D12RenderDevice::Implementation::shutdown() noexcept {
    for (Submission& submission : submissions) {
        if (submission.uploadInstances != nullptr && submission.mapped != nullptr) {
            submission.uploadInstances->Unmap(0, nullptr);
        }
        submission.mapped = nullptr;
        submission.directUploadedIdentity = 0;
        submission.directUploadedRevision = 0;
        submission.gpuLocalUploadedIdentity = 0;
        submission.gpuLocalUploadedRevision = 0;
        submission.gpuLocalInstances.Reset();
        submission.uploadInstances.Reset();
    }
    submissions.clear();
    for (ComPtr<ID3D12PipelineState>& pipeline : pipelines) pipeline.Reset();
    rootSignature.Reset();
    ownerDevice.Reset();
    instanceBufferBytes = 0;
    adapterArchitectureKnown = false;
    adapterUma = true;
    gpuLocalResourcesEnabled = false;
    ready = false;
    statistics.gpuLocalResidentBytes = 0;
}

D3D12RenderDevice::D3D12RenderDevice() : mImplementation(std::make_unique<Implementation>()) {}
D3D12RenderDevice::~D3D12RenderDevice() { shutdown(); }
bool D3D12RenderDevice::initialize(ID3D12Device& device, D3D12GfxConfiguration configuration) noexcept {
    try {
        const bool wasReady = mImplementation->ready;
        const bool initialized = mImplementation->initialize(device, configuration);
        if (!initialized && !wasReady) {
            mImplementation->shutdown();
        }
        return initialized;
    } catch (...) {
        mImplementation->shutdown();
        mImplementation->error = "D3D12 gfx initialization exhausted CPU bookkeeping storage";
        return false;
    }
}
bool D3D12RenderDevice::record(
    const InstanceBatch& batch, const ViewParameters& view,
    ID3D12GraphicsCommandList& commandList, std::uint32_t slot,
    henia::backend::d3d12::SubmissionReuse submissionReuse) noexcept {
    return mImplementation->record(batch, view, commandList, slot, submissionReuse);
}
bool D3D12RenderDevice::reportGpuTime(
    std::uint64_t sampleId,
    std::uint64_t nanoseconds) noexcept {
    if (!mImplementation->ready) return false;
    const bool reported = mImplementation->profileTimeline.reportGpuTime(sampleId, nanoseconds);
    mImplementation->statistics.profile = mImplementation->profileTimeline.profile();
    return reported;
}
void D3D12RenderDevice::shutdown() noexcept { if (mImplementation != nullptr) mImplementation->shutdown(); }
bool D3D12RenderDevice::initialized() const noexcept { return mImplementation->ready; }
std::size_t D3D12RenderDevice::boxCapacity() const noexcept { return mImplementation->configuration.boxCapacity; }
std::uint32_t D3D12RenderDevice::submissionCapacity() const noexcept {
    return mImplementation->configuration.submissionCapacity;
}
D3D12GfxStatistics D3D12RenderDevice::statistics() const noexcept { return mImplementation->statistics; }
std::string_view D3D12RenderDevice::lastError() const noexcept { return mImplementation->error.view(); }

} // namespace henia::gfx
