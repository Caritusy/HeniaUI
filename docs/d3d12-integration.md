# D3D12 command-list integration

HeniaUI records into an open, host-owned `DIRECT` command list. The list must
come from the same `ID3D12Device` used to initialize the renderer. Bundles,
compute/copy lists, and lists from another device are rejected before commands
are emitted.

## Ownership and attachment contract

The host owns command allocators, list reset/close, resource transitions,
`OMSetRenderTargets`, clears, queue execution, and fence completion. Before a UI
recording call, the host must bind a render target whose format matches the
format passed to `D3D12Renderer::initialize()` and whose sample count and
quality match `D3D12RendererConfiguration::sampleCount` and `sampleQuality`.
`D3D12RendererConfiguration::targetColorSpace` must also match that format:
linear targets use `*_UNORM`, while sRGB targets use `*_UNORM_SRGB`.
Initialization rejects a mismatch. Shader output is premultiplied linear light;
the sRGB RTV performs the output transfer. See
[Color, alpha, and texture contract](color-and-texture-contract.md).

Before a gfx recording call, the host must bind an RT matching
`D3D12GfxConfiguration::renderTargetFormat`, `sampleCount`, and `sampleQuality`.
When a compatible DS target is available, it must match `depthStencilFormat`
and the same sample layout. The host still passes availability through its
normal depth policy; HeniaUI never queries OM bindings or transitions
attachments.

Pass `backend::d3d12::SubmissionReuse` when the renderer should verify a slot's
previous fence before touching its mapped upload buffer or retained textures.
The detailed lifetime and device-reset flow is in
[Renderer ownership and recreation](resource-lifetime.md).

Initialization verifies that the configured device supports the RT/DS formats,
sample counts, and quality indices. A quality value is valid when it is less
than the `NumQualityLevels` reported for the configured format and count. D3D12
exposes no query for the command list's current OM formats or sample layout, so
the final attachment match remains an explicit host obligation.

## Multisampled composition

Both D3D12 renderer families use the terms `sampleCount` and `sampleQuality`.
The defaults are the single-sample layout `{1, 0}`. Initialization queries
`D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS`; unsupported counts and out-of-range
quality indices fail before any PSO or renderer resource is created. Every PSO,
including the UI alpha/additive and texture-free variants, is created for the
configured sample layout. That layout is also part of renderer reinitialization
identity and every pipeline-library cache name.

For pre-resolve composition, initialize the UI renderer with the exact sample
count and quality of the host MSAA color attachment, bind that attachment, and
record UI before the host resolve. The UI output then participates in the same
resolve as the scene. For post-resolve composition, resolve the scene first,
initialize UI for `{1, 0}`, and bind the single-sample destination before UI
recording. HeniaUI never inserts attachment transitions or calls
`ResolveSubresource`; those operations and their ordering remain host-owned.

MSAA PSOs enable multisample rasterization and keep the sample mask fully open.
Alpha-to-coverage is deliberately disabled: the UI shaders already produce
premultiplied analytic coverage, and no measured workload currently justifies
changing that blend contract. A future alpha-to-coverage option would require
separate visual and performance evidence rather than silently changing the
default.

## Shader packages and pipeline libraries

The D3D12 target compiles `shaders/d3d12/ui.hlsl` and
`shaders/d3d12/gfx.hlsl` during the CMake build, then embeds the validated DXBC
in `HeniaUID3D12`. Release initialization consumes only that bytecode and does
not link or load `d3dcompiler`. A shader syntax error therefore fails the build
with the original source path and compiler diagnostic instead of surfacing in
the host process.

`backend::d3d12::shaderPackageInfo()` exposes the deterministic package hash,
byte counts, and whether developer runtime compilation is active. The hash
covers the HLSL source, compiler profile/defines, and every embedded blob.
Repeated build-time compilation is checked for byte-for-byte reproducibility.
Source distributions include the HLSL; installed packages need only the static
library because all required runtime artifacts are embedded.

For local shader iteration, configure
`HENIAUI_D3D12_RUNTIME_SHADER_COMPILATION=ON`. Only the Debug configuration then
recompiles the embedded source during renderer initialization and links
`d3dcompiler`; Release remains on the precompiled path.

Both renderer configurations accept an optional host-owned
`ID3D12PipelineLibrary`. The library is borrowed only for `initialize()` and
must belong to the same device. Cache names include the shader package hash,
renderer family, RT/DS formats, sample count, sample quality, blend variant, and
depth variant, so incompatible PSOs cannot alias. Cache hits, misses, stores,
and failed stores are reported in renderer statistics. HeniaUI does not
serialize the library; the host may call `GetSerializedSize()`/`Serialize()`
and persist the bytes using its own adapter/driver invalidation policy.

## Instance-storage strategy

Both D3D12 configurations expose `instanceStorage` and
`gpuLocalInstanceThresholdBytes`. `DirectUpload` binds the slot's persistently
mapped upload resource directly. `GpuLocal` stages changed data in that same
fence-owned slot, records `CopyBufferRegion` into a default-heap resource, and
binds the default resource for drawing. `Automatic` is the default: it queries
`D3D12_FEATURE_ARCHITECTURE1` (falling back to `ARCHITECTURE`), uses direct
upload on UMA or when architecture is unknown, and uses GPU-local storage on a
discrete adapter only when submitted instance bytes reach the 64 KiB default
threshold. The strategy and threshold are explicit overrides, so hosts can
apply measurements from their own adapters and workloads.

Every submission slot owns its staging and optional default resource. A changed
GPU-local revision records `VERTEX_AND_CONSTANT_BUFFER -> COPY_DEST`, exact
buffer copies, then the reverse transition on the host command list. A stable
revision records neither a copy nor a transition. Gfx `InstanceBatch` dirty
ranges become independent `CopyBufferRegion` calls; an immutable UI packet
revision is copied as one contiguous range. `SubmissionReuse` validation occurs
before staging writes or transitions, so the host fence contract prevents both
resources from being overwritten while in flight.

With `VisibilityMode::CpuFrustum` (or an activated `Automatic` policy), the
submission slot's mapped indirect-argument resource is updated only after the
same reuse check and `ExecuteIndirect` consumes the compact instance count.
That argument resource is part of the slot lifetime. Direct mode continues to
record `DrawInstanced`; CPU visibility and the OpenGL fallback are described in
[3D visibility and indirect submission](3d-visibility.md).

Statistics distinguish CPU staging writes (`uploadedInstanceBytes`), copy
commands/bytes (`instanceCopyOperations`/`copiedInstanceBytes`), vertex data
read from upload memory (`uploadHeapReadBytes`), allocated default-heap capacity
(`gpuLocalResidentBytes`), and frames selected for each strategy. Architecture
availability and the UMA decision are also reported.
Gfx visibility statistics separately report direct/culling frames, rejected
instances, chunk work, result reuse, argument updates, and indirect draws.

## State overwritten by `record()`

Both renderers overwrite and do not restore:

- graphics root signature and root parameters;
- graphics pipeline state;
- IA primitive topology and vertex-buffer slot 0;
- viewport 0 and scissor rectangle 0.

The UI renderer additionally binds its shader-visible CBV/SRV/UAV descriptor
heap for any packet containing textures and sets descriptor table 0. This can
invalidate the host's descriptor tables, including tables unrelated to HeniaUI.
The host must rebind all shader-visible heaps it needs and then set every root
descriptor/table/constant used by its next draw. A packet containing only
geometry uses a separate heap-free root signature/PSO and does not call
`SetDescriptorHeaps`, but it still overwrites the other listed state.

Neither renderer changes render/depth-target bindings, attachment transitions,
index-buffer binding, blend factor, or stencil reference. Hosts should bind
their complete known-good draw state rather than depend on this shorter
unchanged list.

## Safe before/after recording

```cpp
// Host owns transitions and attachments.
TransitionToRenderTarget(commandList, colorTarget, depthTarget);
commandList.OMSetRenderTargets(1, &rtv, FALSE, hasDepth ? &dsv : nullptr);

BindHostGraphicsState(commandList); // heaps, root state, PSO, IA, RS
RecordHostDrawBefore(commandList);

if (!uiRenderer.record(packet, commandList, slot, width, height)) {
    HandleUiRecordingFailure(uiRenderer.lastError());
}

// Mandatory before any later host draw. SetDescriptorHeaps comes before root
// descriptor tables because a heap change invalidates tables from the old heap.
BindHostDescriptorHeaps(commandList);
BindHostRootSignatureAndEveryRootArgument(commandList);
BindHostPipelineAndInputAssembly(commandList);
BindHostViewportAndScissor(commandList);
RecordHostDrawAfter(commandList);
```

The width/height overload is an identity-transform shortcut. When layout units
differ from attachment pixels, pass `UiRenderViewport` instead; geometry and
logical clip rectangles then share its axis-aligned scale/translation. See
[Coordinate spaces, DPI, and framebuffer scaling](coordinate-spaces.md).

For the strongest containment, record HeniaUI into a dedicated direct command
list between host passes. The host still owns its allocator and fence slot, but
closing the HeniaUI list creates a natural state boundary. HeniaUI does not
currently support bundle recording because its heap and root-state requirements
do not compose safely with arbitrary bundle inheritance.

## Descriptor-table reuse

The UI renderer owns a fixed shader-visible heap sized at initialization. Each
submission slot and batch index has a stable table. The table is recopied only
when a texture handle or committed texture revision changes; otherwise the
existing table is reused. Statistics expose heap bindings, table copies, and
cache hits. A custom host descriptor allocator is intentionally not accepted by
the current API: using a private fixed heap makes slot/fence ownership explicit,
while the mandatory post-Henia host rebind keeps integration deterministic.
