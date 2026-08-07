# Renderer ownership and recreation

HeniaUI renderers own the GPU objects they create and OpenGL texture names the
host explicitly transfers. D3D12 external textures are retained by COM; borrowed
OpenGL names are never deleted. Renderers do not own native windows, OpenGL
contexts, D3D12 devices, queues, swap chains, attachments, command allocators,
or host fences. Renderer instances have no shared mutable graphics globals:
multiple instances are valid when each call satisfies that instance's
context/device contract.

Calling `initialize()` again is idempotent only for the same owner and the exact
same configuration. A different context, device, capacity, target format, or
sample count/quality is rejected while the existing resources remain usable.
Perform an orderly `shutdown()` before initializing with a changed
configuration.

## CPU resource handles and backing

`TextureHandle` and `FontHandle` contain a one-based slot plus a generation.
Destroying a store entry releases its owned CPU storage and advances the slot's
generation before making the slot reusable. A packet, text cache entry, or host
object that still contains the old handle therefore remains a valid immutable
snapshot, but the store and synchronized renderers reject it as stale. `size()`
counts live entries; `slotCount()` is the bounded high-water mark used to size
renderer bookkeeping. Handles remain four-byte values with 16-bit slot and
generation fields (up to 65,535 live entries per store). A slot that reaches the
maximum generation is retired instead of wrapping, so an old handle can never
become valid again.

Every renderer instance observes store destruction or slot reuse independently
on its next `synchronizeTextures()` call. OpenGL retires its old object in that
call. D3D12 removes the old object from new descriptor tables while submission
slots keep COM references until their host-proved fences complete. A host must
synchronize every live renderer that consumes a store before treating a
destruction or update as globally visible.

`TextureCreateOptions::backingPolicy` defines CPU ownership:

- `Retained` keeps pixels until update or destruction.
- `DiscardAfterUpload` permits explicit `discardCpuBacking()` after every
  consuming renderer has synchronized. A recreated renderer reports the
  missing backing until `restoreCpuBacking()` supplies the same logical
  revision again.
- `Regenerable` also permits discard and lets `ensureCpuBacking()` invoke the
  registered callback after context/device recreation.
- `ExternalGpu` is created with `createExternal()` and has no CPU pixels.
  Bind a matching native object separately on each renderer.

Alpha representation and transfer metadata are immutable for a texture handle.
`TextureAlphaMode::FormatDefault` is resolved at creation, and the resolved mode
plus `TextureColorSpace` remain visible through `TextureView` across updates,
discard/restoration, and regeneration. Recreate the entry when those semantics
change. Native format selection and external-resource validation follow
[Color, alpha, and texture contract](color-and-texture-contract.md).

Use `updateRegion()` for one rectangular edit or `updateRegions()` for an atomic
logical batch. The store stages all writes, groups them per texture, advances
each affected texture once, and retains the conservative dirty union plus its
previous pixels for transactional backend rollback. Repeated glyph writes made
before synchronization therefore remain one partial upload instead of forcing
a full texture replacement. A renderer with a genuine revision gap still falls
back to the current full CPU image. `restoreCpuBacking()` does not create a new
revision; use `update()` when the logical texture content changes.

`TextureStore::statistics()` reports live/reusable slots, retained CPU bytes,
discarded backings, external entries, and regeneration outcomes. Renderer
statistics report current logical GPU bytes, external bindings, full/partial
upload counts and bytes, and retired objects. `FontStore::storageBytes()`
reports its CPU allocation footprint.

## OpenGL context lifetime

Every OpenGL object belongs to the exact `HGLRC` current during `initialize()`.
That context must be current for synchronization, rendering, and `shutdown()`;
another context is rejected even if the host believes it belongs to the same
share group. HeniaUI never creates, switches, presents, or stores a process-wide
current context.

Normal destruction order is:

1. stop issuing work through the renderer;
2. make its owner context current on the calling thread;
3. call `shutdown()` and require `true`;
4. destroy the context and its surface/window.

If the context was permanently lost or destroyed before step 3, it is no longer
legal to call GL deletion functions for its object names. Call `abandon()` to
drop those stale names without GL calls, then initialize the same renderer on a
replacement context. `abandon()` must not be used for an accessible live
context because it deliberately leaves that context's objects allocated until
the context itself is destroyed.

OpenGL instance-upload rings own a fence per slot. Changed content uses only a
slot whose fence has completed; exhaustion returns `false` rather than waiting
or overwriting in-flight mapped data. Shutdown deletes fences before buffers and
then pipeline objects while the owner context is current.

## D3D12 device and submission lifetime

A D3D12 renderer retains the initialization device. Command lists, texture
upload queues, and optional submission-reuse fences are checked against that
device by COM identity. Device removal is rejected with a recreate diagnostic;
HeniaUI does not attempt to rebuild host-owned swap-chain state automatically.

Each submission slot owns a persistently mapped upload buffer and, for gfx, a
mapped indirect-draw argument buffer. UI slots additionally retain texture
generations referenced by their descriptor tables. Before reusing
a slot, the host must know that every previously submitted command list using it
has completed. The optional `SubmissionReuse` argument makes that proof
checkable at the point immediately before HeniaUI writes or releases slot-owned
resources:

```cpp
henia::backend::d3d12::SubmissionReuse reuse{
    .completionFence = frameFence.Get(),
    .completionValue = slot.lastSubmittedFenceValue,
};

if (!renderer.record(packet, commandList, slot.index, width, height, reuse)) {
    // A busy slot is left untouched; select another slot or try next frame.
    HandleHeniaError(renderer.lastError());
}
```

The fence is borrowed only for the call. A null fence with value zero explicitly
declares that the slot is new or that the host proved completion by another
mechanism. Supplying only one of the two fields is invalid. A non-null fence must
belong to the initialization device, must not report device removal, and must
have reached the requested value. This check protects instance uploads, gfx
indirect arguments, and UI texture-generation retention; the host still owns
fence signaling and waiting.

UI texture synchronization uses one direct queue identity for the renderer's
lifetime. Upload buffers, staged textures, command allocators, and command lists
remain retained in an internal batch until its internal fence completes.
`pollTextureUploads()` never waits. Before `shutdown()`, the host must stop
recording, complete every render-slot fence, make the texture-upload queue idle,
and poll completed uploads. Destroy renderers before releasing their device.

A one-revision region edit uses in-place `CopyTextureRegion` only when no
submission slot retains the current resource. Recording that handle is rejected
until the upload fence completes. If an in-flight submission still owns the
resource, synchronization creates and fully populates a replacement instead;
the old COM resource remains alive with the submission. Completed staged work
commits only when its packed handle and revision are still current, so a
destroyed/reused slot cannot resurrect an old generation.

`bindExternalTexture()` retains a host-provided `ID3D12Resource` and validates
its device, dimensions, transfer-derived format, and sample layout. The host owns transitions and
must keep it in `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` whenever HeniaUI
records it.

## Resize, format change, and device reset

- OpenGL renderers do not retain a drawable or framebuffer. A resize on the same
  context needs only the new viewport dimensions on the next render call.
- D3D12 UI does not retain swap-chain buffers. A resize with the same RT format
  and sample layout needs new host attachments/transitions and new viewport
  dimensions, but no renderer recreation.
- A D3D12 RT/DS format, sample-count, or sample-quality change requires host GPU
  quiescence, `shutdown()`, and `initialize()` with the new configuration.
- On D3D12 device removal, stop all calls, release/reset host work, call
  `shutdown()`, create the replacement device/queue/swap chain, and initialize
  new renderer resources. Packet snapshots and CPU-side texture stores remain
  host data and may be synchronized again.
- On permanent OpenGL context loss, call `abandon()` rather than `shutdown()`,
  create/make-current the replacement context, then initialize and synchronize
  resources again.

The API never assumes that a resource name from one context or a device child
from one D3D12 device is valid in another renderer instance.

OpenGL `bindExternalTexture()` accepts either borrowed or transferred ownership.
Shutdown and replacement never delete a borrowed name; a transferred name is
deleted with renderer-owned textures. The object must be a live `GL_TEXTURE_2D`
in the renderer's exact owner context, with storage matching the external store
entry.
