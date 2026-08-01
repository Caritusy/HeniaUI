#include "henia/ui/backend/d3d12/D3D12Renderer.h"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace henia::ui {
namespace {

using Microsoft::WRL::ComPtr;

static_assert(std::is_standard_layout_v<DrawInstance>);
static_assert(offsetof(DrawInstance, pointB) == offsetof(DrawInstance, pointA) + sizeof(Vec2));
static_assert(offsetof(DrawInstance, thickness) == offsetof(DrawInstance, radius) + sizeof(float));

constexpr const char* kShaderSource = R"hlsl(
cbuffer FrameConstants : register(b0) {
    float2 viewportSize;
};

Texture2D textures[8] : register(t0);
SamplerState linearSampler : register(s0);

struct VertexInput {
    float4 bounds : INSTANCE_BOUNDS;
    float4 uv : INSTANCE_UV;
    float4 points : INSTANCE_POINTS;
    float4 color : INSTANCE_COLOR;
    float2 metrics : INSTANCE_METRICS;
    uint textureSlot : INSTANCE_TEXTURE_SLOT;
    uint kind : INSTANCE_KIND;
    uint vertexId : SV_VertexID;
};

struct PixelInput {
    float4 position : SV_Position;
    float2 pixelPosition : PIXEL_POSITION;
    float2 localPosition : LOCAL_POSITION;
    float2 primitiveSize : PRIMITIVE_SIZE;
    float2 textureUv : TEXTURE_UV;
    float4 tintColor : TINT_COLOR;
    float4 linePoints : LINE_POINTS;
    float2 shapeMetrics : SHAPE_METRICS;
    nointerpolation uint textureSlot : TEXTURE_SLOT;
    nointerpolation uint primitiveKind : PRIMITIVE_KIND;
};

static const float2 corners[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
};

PixelInput vertexMain(VertexInput input) {
    PixelInput output;
    float2 corner = corners[input.vertexId];
    float2 pixel = lerp(input.bounds.xy, input.bounds.zw, corner);
    float2 normalized = pixel / viewportSize;
    output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
    output.pixelPosition = pixel;
    output.localPosition = corner;
    output.primitiveSize = input.bounds.zw - input.bounds.xy;
    output.textureUv = lerp(input.uv.xy, input.uv.zw, corner);
    output.tintColor = input.color;
    output.linePoints = input.points;
    output.shapeMetrics = input.metrics;
    output.textureSlot = input.textureSlot;
    output.primitiveKind = input.kind;
    return output;
}

float4 sampleTexture(uint slot, float2 uv) {
    if (slot == 0) return textures[0].Sample(linearSampler, uv);
    if (slot == 1) return textures[1].Sample(linearSampler, uv);
    if (slot == 2) return textures[2].Sample(linearSampler, uv);
    if (slot == 3) return textures[3].Sample(linearSampler, uv);
    if (slot == 4) return textures[4].Sample(linearSampler, uv);
    if (slot == 5) return textures[5].Sample(linearSampler, uv);
    if (slot == 6) return textures[6].Sample(linearSampler, uv);
    return textures[7].Sample(linearSampler, uv);
}

float roundedBoxDistance(float2 positionValue, float2 halfSize, float radius) {
    float2 q = abs(positionValue) - halfSize + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float segmentDistance(float2 positionValue, float2 start, float2 finish) {
    float2 segment = finish - start;
    float denominator = max(dot(segment, segment), 0.0001);
    float projection = saturate(dot(positionValue - start, segment) / denominator);
    return length(positionValue - (start + projection * segment));
}

float4 pixelMain(PixelInput input) : SV_Target {
    float coverage = 1.0;
    float4 color = input.tintColor;

    if (input.primitiveKind == 0 || input.primitiveKind == 1) {
        float2 centered = (input.localPosition - 0.5) * input.primitiveSize;
        float distanceToEdge = roundedBoxDistance(
            centered,
            input.primitiveSize * 0.5,
            min(input.shapeMetrics.x, min(input.primitiveSize.x, input.primitiveSize.y) * 0.5));
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
        float distanceToLine = segmentDistance(input.pixelPosition, input.linePoints.xy, input.linePoints.zw);
        float antiAlias = max(fwidth(distanceToLine), 0.75);
        coverage = 1.0 - smoothstep(
            input.shapeMetrics.y * 0.5 - antiAlias,
            input.shapeMetrics.y * 0.5 + antiAlias,
            distanceToLine);
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
    std::string& error) noexcept {
    ComPtr<ID3DBlob> errors;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    const HRESULT result = D3DCompile(
        kShaderSource,
        std::strlen(kShaderSource),
        "HeniaUI",
        nullptr,
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

[[nodiscard]] bool waitForQueue(ID3D12Device& device, ID3D12CommandQueue& queue) noexcept {
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        return false;
    }
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr) {
        return false;
    }
    const bool signaled = SUCCEEDED(queue.Signal(fence.Get(), 1));
    const bool armed = signaled && SUCCEEDED(fence->SetEventOnCompletion(1, eventHandle));
    const bool waited = armed && WaitForSingleObject(eventHandle, 10000) == WAIT_OBJECT_0;
    CloseHandle(eventHandle);
    return waited;
}

} // namespace

struct D3D12Renderer::Implementation final {
    struct Submission final {
        ComPtr<ID3D12Resource> instances;
        std::byte* mapped = nullptr;
        std::uint64_t uploadedIdentity = 0;
        std::uint64_t uploadedRevision = 0;
    };

    struct GpuTexture final {
        ComPtr<ID3D12Resource> resource;
        std::uint64_t revision = 0;
    };

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> alphaPipeline;
    ComPtr<ID3D12PipelineState> additivePipeline;
    ComPtr<ID3D12DescriptorHeap> cpuTextureHeap;
    ComPtr<ID3D12DescriptorHeap> gpuBatchHeap;
    std::vector<Submission> submissions;
    std::vector<GpuTexture> textures;
    D3D12RendererConfiguration configuration{};
    DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_UNKNOWN;
    std::uint32_t descriptorStride = 0;
    D3D12RenderStatistics statistics{};
    std::string error;
    bool ready = false;

    [[nodiscard]] bool initialize(
        ID3D12Device& nativeDevice,
        DXGI_FORMAT format,
        D3D12RendererConfiguration requested) noexcept;
    [[nodiscard]] bool createRootSignature() noexcept;
    [[nodiscard]] bool createPipelines() noexcept;
    [[nodiscard]] bool createDescriptorHeaps() noexcept;
    [[nodiscard]] bool createSubmissionBuffers() noexcept;
    [[nodiscard]] bool synchronizeTextures(
        const TextureStore& store,
        ID3D12CommandQueue& queue) noexcept;
    [[nodiscard]] bool record(
        const RenderPacket& packet,
        ID3D12GraphicsCommandList& commandList,
        std::uint32_t submissionSlot,
        std::uint32_t width,
        std::uint32_t height) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptor(std::uint32_t index) const noexcept;
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE gpuCpuDescriptor(std::uint32_t index) const noexcept;
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptor(std::uint32_t index) const noexcept;
};

bool D3D12Renderer::Implementation::initialize(
    ID3D12Device& nativeDevice,
    DXGI_FORMAT format,
    D3D12RendererConfiguration requested) noexcept {
    if (ready) {
        return true;
    }
    const std::uint64_t gpuDescriptors = static_cast<std::uint64_t>(requested.submissionCapacity)
        * requested.batchCapacity * DrawBatch::kTextureCapacity;
    if (requested.instanceCapacity == 0 || requested.submissionCapacity == 0
        || requested.batchCapacity == 0 || requested.textureCapacity == 0
        || gpuDescriptors > std::numeric_limits<std::uint32_t>::max()
        || format == DXGI_FORMAT_UNKNOWN) {
        error = "Invalid D3D12 renderer configuration";
        return false;
    }

    device = &nativeDevice;
    configuration = requested;
    renderTargetFormat = format;
    if (!createRootSignature() || !createPipelines() || !createDescriptorHeaps()
        || !createSubmissionBuffers()) {
        shutdown();
        return false;
    }
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
    return true;
}

bool D3D12Renderer::Implementation::createPipelines() noexcept {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    if (!compileShader("vertexMain", "vs_5_0", vertexShader, error)
        || !compileShader("pixelMain", "ps_5_0", pixelShader, error)) {
        return false;
    }

    constexpr std::array inputElements{
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_BOUNDS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, bounds)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_UV", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, uv)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_POINTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, pointA)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, color)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_METRICS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(DrawInstance, radius)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_TEXTURE_SLOT", 0, DXGI_FORMAT_R32_UINT, 0, static_cast<UINT>(offsetof(DrawInstance, textureSlot)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        D3D12_INPUT_ELEMENT_DESC{"INSTANCE_KIND", 0, DXGI_FORMAT_R8_UINT, 0, static_cast<UINT>(offsetof(DrawInstance, kind)), D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
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
    return true;
}

bool D3D12Renderer::Implementation::createDescriptorHeaps() noexcept {
    D3D12_DESCRIPTOR_HEAP_DESC cpuDescription{};
    cpuDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cpuDescription.NumDescriptors = configuration.textureCapacity + 1U;
    cpuDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&cpuDescription, IID_PPV_ARGS(&cpuTextureHeap)))) {
        error = "D3D12 CPU texture descriptor heap creation failed";
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC gpuDescription{};
    gpuDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    gpuDescription.NumDescriptors = configuration.submissionCapacity
        * configuration.batchCapacity * static_cast<UINT>(DrawBatch::kTextureCapacity);
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
    const std::uint64_t bufferSize = configuration.instanceCapacity * sizeof(DrawInstance);
    const D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = bufferDescription(bufferSize);
    submissions.resize(configuration.submissionCapacity);
    for (Submission& submission : submissions) {
        if (FAILED(device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&submission.instances)))) {
            error = "D3D12 instance upload buffer creation failed";
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(submission.instances->Map(0, &noRead, &mapped)) || mapped == nullptr) {
            error = "D3D12 instance upload buffer mapping failed";
            return false;
        }
        submission.mapped = static_cast<std::byte*>(mapped);
    }
    return true;
}

bool D3D12Renderer::Implementation::synchronizeTextures(
    const TextureStore& store,
    ID3D12CommandQueue& queue) noexcept {
    if (!ready || store.size() > configuration.textureCapacity) {
        error = "D3D12 texture store exceeds configured capacity";
        return false;
    }
    if (textures.size() < store.size()) {
        textures.resize(store.size());
    }

    std::vector<std::uint32_t> dirty;
    dirty.reserve(store.size());
    for (std::uint32_t value = 1; value <= store.size(); ++value) {
        const TextureView view = store.view(TextureHandle{value});
        if (view.handle.valid() && textures[value - 1U].revision != view.revision) {
            dirty.push_back(value);
        }
    }
    if (dirty.empty()) {
        error.clear();
        return true;
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
        error = "D3D12 texture upload command objects could not be created";
        return false;
    }

    std::vector<ComPtr<ID3D12Resource>> uploads;
    uploads.reserve(dirty.size());
    for (const std::uint32_t value : dirty) {
        const TextureView view = store.view(TextureHandle{value});
        const D3D12_RESOURCE_DESC textureDesc = textureDescription(view);
        const D3D12_HEAP_PROPERTIES defaultHeap = heapProperties(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> texture;
        if (FAILED(device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &textureDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&texture)))) {
            error = "D3D12 texture resource creation failed";
            return false;
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rowCount = 0;
        UINT64 rowBytes = 0;
        UINT64 uploadBytes = 0;
        device->GetCopyableFootprints(
            &textureDesc,
            0,
            1,
            0,
            &footprint,
            &rowCount,
            &rowBytes,
            &uploadBytes);

        const D3D12_HEAP_PROPERTIES uploadHeap = heapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC uploadDesc = bufferDescription(uploadBytes);
        ComPtr<ID3D12Resource> upload;
        if (FAILED(device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload)))) {
            error = "D3D12 texture upload buffer creation failed";
            return false;
        }

        std::byte* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped))) || mapped == nullptr) {
            error = "D3D12 texture upload mapping failed";
            return false;
        }
        for (UINT row = 0; row < rowCount; ++row) {
            std::memcpy(
                mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                view.pixels.data() + static_cast<std::size_t>(row) * view.rowPitch,
                static_cast<std::size_t>(rowBytes));
        }
        const D3D12_RANGE written{0, static_cast<SIZE_T>(uploadBytes)};
        upload->Unmap(0, &written);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_SHADER_RESOURCE_VIEW_DESC shaderView{};
        shaderView.Format = textureDesc.Format;
        shaderView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        shaderView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shaderView.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(texture.Get(), &shaderView, cpuDescriptor(value));

        textures[value - 1U].resource = std::move(texture);
        textures[value - 1U].revision = view.revision;
        uploads.push_back(std::move(upload));
    }

    if (FAILED(commandList->Close())) {
        error = "D3D12 texture upload command list could not be closed";
        return false;
    }
    ID3D12CommandList* commandLists[]{commandList.Get()};
    queue.ExecuteCommandLists(1, commandLists);
    if (!waitForQueue(*device.Get(), queue)) {
        error = "D3D12 texture upload queue did not become idle";
        return false;
    }

    statistics.textureUploads += dirty.size();
    error.clear();
    return true;
}

bool D3D12Renderer::Implementation::record(
    const RenderPacket& packet,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    if (!ready || submissionSlot >= submissions.size() || width == 0 || height == 0
        || packet.instances().size() > configuration.instanceCapacity
        || packet.batches().size() > configuration.batchCapacity) {
        ++statistics.rejectedFrames;
        error = "D3D12 render packet exceeds a configured frame capacity";
        return false;
    }
    if (packet.instances().empty() || packet.batches().empty()) {
        ++statistics.recordedFrames;
        error.clear();
        return true;
    }
    for (const DrawBatch& batch : packet.batches()) {
        for (std::uint32_t slot = 0; slot < batch.textureCount; ++slot) {
            const TextureHandle handle = batch.textures[slot];
            if (!handle.valid() || handle.value() > textures.size()
                || textures[handle.value() - 1U].resource == nullptr) {
                ++statistics.rejectedFrames;
                error = "D3D12 render packet references an unsynchronized texture";
                return false;
            }
        }
    }

    Submission& submission = submissions[submissionSlot];
    if (submission.uploadedIdentity != packet.identity() || submission.uploadedRevision != packet.revision()) {
        std::memcpy(submission.mapped, packet.instances().data(), packet.instances().size_bytes());
        submission.uploadedIdentity = packet.identity();
        submission.uploadedRevision = packet.revision();
        ++statistics.instanceUploads;
    }

    ID3D12DescriptorHeap* heaps[]{gpuBatchHeap.Get()};
    commandList.SetDescriptorHeaps(1, heaps);
    commandList.SetGraphicsRootSignature(rootSignature.Get());
    const std::array viewportConstants{static_cast<float>(width), static_cast<float>(height)};
    commandList.SetGraphicsRoot32BitConstants(1, 2, viewportConstants.data(), 0);
    commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const D3D12_VERTEX_BUFFER_VIEW instanceView{
        submission.instances->GetGPUVirtualAddress(),
        static_cast<UINT>(configuration.instanceCapacity * sizeof(DrawInstance)),
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
        if (activeBlend != batch.blend) {
            commandList.SetPipelineState(
                batch.blend == BlendMode::Additive ? additivePipeline.Get() : alphaPipeline.Get());
            activeBlend = batch.blend;
        }

        const std::uint32_t tableIndex = (
            submissionSlot * configuration.batchCapacity + static_cast<std::uint32_t>(batchIndex))
            * static_cast<std::uint32_t>(DrawBatch::kTextureCapacity);
        for (std::uint32_t slot = 0; slot < DrawBatch::kTextureCapacity; ++slot) {
            const std::uint32_t source = slot < batch.textureCount
                ? batch.textures[slot].value()
                : 0U;
            device->CopyDescriptorsSimple(
                1,
                gpuCpuDescriptor(tableIndex + slot),
                cpuDescriptor(source),
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        commandList.SetGraphicsRootDescriptorTable(0, gpuDescriptor(tableIndex));

        D3D12_RECT scissor{};
        if (batch.clip.enabled) {
            scissor.left = std::max(static_cast<LONG>(batch.clip.area.min.x), 0L);
            scissor.top = std::max(static_cast<LONG>(batch.clip.area.min.y), 0L);
            scissor.right = std::min(static_cast<LONG>(batch.clip.area.max.x), static_cast<LONG>(width));
            scissor.bottom = std::min(static_cast<LONG>(batch.clip.area.max.y), static_cast<LONG>(height));
        } else {
            scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        }
        commandList.RSSetScissorRects(1, &scissor);
        commandList.DrawInstanced(6, batch.instanceCount, 0, batch.firstInstance);
        ++statistics.drawCalls;
        statistics.submittedInstances += batch.instanceCount;
    }

    ++statistics.recordedFrames;
    error.clear();
    return true;
}

void D3D12Renderer::Implementation::shutdown() noexcept {
    for (Submission& submission : submissions) {
        if (submission.instances != nullptr && submission.mapped != nullptr) {
            submission.instances->Unmap(0, nullptr);
        }
        submission.mapped = nullptr;
        submission.uploadedIdentity = 0;
        submission.uploadedRevision = 0;
        submission.instances.Reset();
    }
    submissions.clear();
    textures.clear();
    gpuBatchHeap.Reset();
    cpuTextureHeap.Reset();
    additivePipeline.Reset();
    alphaPipeline.Reset();
    rootSignature.Reset();
    device.Reset();
    configuration = {};
    renderTargetFormat = DXGI_FORMAT_UNKNOWN;
    descriptorStride = 0;
    ready = false;
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

D3D12Renderer::D3D12Renderer(D3D12Renderer&&) noexcept = default;

D3D12Renderer& D3D12Renderer::operator=(D3D12Renderer&&) noexcept = default;

bool D3D12Renderer::initialize(
    ID3D12Device& device,
    DXGI_FORMAT renderTargetFormat,
    D3D12RendererConfiguration configuration) noexcept {
    return mImplementation->initialize(device, renderTargetFormat, configuration);
}

bool D3D12Renderer::synchronizeTextures(
    const TextureStore& textures,
    ID3D12CommandQueue& directQueue) noexcept {
    return mImplementation->synchronizeTextures(textures, directQueue);
}

bool D3D12Renderer::record(
    const RenderPacket& packet,
    ID3D12GraphicsCommandList& commandList,
    std::uint32_t submissionSlot,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight) noexcept {
    return mImplementation->record(
        packet,
        commandList,
        submissionSlot,
        viewportWidth,
        viewportHeight);
}

void D3D12Renderer::shutdown() noexcept { mImplementation->shutdown(); }

bool D3D12Renderer::initialized() const noexcept { return mImplementation->ready; }

std::size_t D3D12Renderer::instanceCapacity() const noexcept {
    return mImplementation->configuration.instanceCapacity;
}

std::uint32_t D3D12Renderer::submissionCapacity() const noexcept {
    return mImplementation->configuration.submissionCapacity;
}

D3D12RenderStatistics D3D12Renderer::statistics() const noexcept {
    return mImplementation->statistics;
}

std::string_view D3D12Renderer::lastError() const noexcept { return mImplementation->error; }

} // namespace henia::ui
