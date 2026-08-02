# Renderer ownership and recreation

HeniaUI renderers own only the GPU objects they create. They do not own native
windows, OpenGL contexts, D3D12 devices, queues, swap chains, attachments,
command allocators, or host fences. Renderer instances have no shared mutable
graphics globals: multiple instances are valid when each call satisfies that
instance's context/device contract.

Calling `initialize()` again is idempotent only for the same owner and the exact
same configuration. A different context, device, capacity, target format, or
sample count is rejected while the existing resources remain usable. Perform an
orderly `shutdown()` before initializing with a changed configuration.

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

Each submission slot owns a persistently mapped upload buffer and, for UI,
texture generations referenced by that slot's descriptor tables. Before reusing
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
have reached the requested value. This check protects instance uploads and UI
texture-generation retention; the host still owns fence signaling and waiting.

UI texture synchronization uses one direct queue identity for the renderer's
lifetime. Upload buffers, staged textures, command allocators, and command lists
remain retained in an internal batch until its internal fence completes.
`pollTextureUploads()` never waits. Before `shutdown()`, the host must stop
recording, complete every render-slot fence, make the texture-upload queue idle,
and poll completed uploads. Destroy renderers before releasing their device.

## Resize, format change, and device reset

- OpenGL renderers do not retain a drawable or framebuffer. A resize on the same
  context needs only the new viewport dimensions on the next render call.
- D3D12 UI does not retain swap-chain buffers. A resize with the same RT format
  and sample count needs new host attachments/transitions and new viewport
  dimensions, but no renderer recreation.
- A D3D12 RT/DS format or sample-count change requires host GPU quiescence,
  `shutdown()`, and `initialize()` with the new configuration.
- On D3D12 device removal, stop all calls, release/reset host work, call
  `shutdown()`, create the replacement device/queue/swap chain, and initialize
  new renderer resources. Packet snapshots and CPU-side texture stores remain
  host data and may be synchronized again.
- On permanent OpenGL context loss, call `abandon()` rather than `shutdown()`,
  create/make-current the replacement context, then initialize and synchronize
  resources again.

The API never assumes that a resource name from one context or a device child
from one D3D12 device is valid in another renderer instance.
