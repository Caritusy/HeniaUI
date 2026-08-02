# D3D12 command-list integration

HeniaUI records into an open, host-owned `DIRECT` command list. The list must
come from the same `ID3D12Device` used to initialize the renderer. Bundles,
compute/copy lists, and lists from another device are rejected before commands
are emitted.

## Ownership and attachment contract

The host owns command allocators, list reset/close, resource transitions,
`OMSetRenderTargets`, clears, queue execution, and fence completion. Before a UI
recording call, the host must bind a render target whose format matches the
format passed to `D3D12Renderer::initialize()` and whose sample count is one.

Before a gfx recording call, the host must bind an RT matching
`D3D12GfxConfiguration::renderTargetFormat` and `sampleCount`. When a compatible
DS target is available, it must match `depthStencilFormat` and the same sample
count. The host still passes availability through its normal depth policy;
HeniaUI never queries OM bindings or transitions attachments.

Pass `backend::d3d12::SubmissionReuse` when the renderer should verify a slot's
previous fence before touching its mapped upload buffer or retained textures.
The detailed lifetime and device-reset flow is in
[Renderer ownership and recreation](resource-lifetime.md).

Initialization verifies that the configured device supports the RT/DS formats
and sample counts. D3D12 exposes no query for the command list's current OM
formats, so the final attachment match remains an explicit host obligation.

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

Statistics distinguish CPU staging writes (`uploadedInstanceBytes`), copy
commands/bytes (`instanceCopyOperations`/`copiedInstanceBytes`), vertex data
read from upload memory (`uploadHeapReadBytes`), allocated default-heap capacity
(`gpuLocalResidentBytes`), and frames selected for each strategy. Architecture
availability and the UMA decision are also reported.

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
