# HeniaUI architecture

HeniaUI deliberately separates retained 2D interface work from general-purpose 3D instance work. They can be submitted in the same host frame, but they do not share a vertex format and neither layer depends on ImGui.

## 2D retained pipeline

`UiDocument` owns a widget tree. Layout and paint dirtiness propagate to the root. A stable document returns the same immutable `RenderPacket`, so both CPU composition and backend instance upload are skipped. `Canvas` remains available as the low-level immediate recorder for custom widgets and generated diagrams.

`Frame::reserve(..., CapacityPolicy::Fixed)` establishes a no-growth recording
contract. Display-list overflow is rejected at the canvas, while packet-capacity
overflow publishes no partial packet and is reported in `PacketStatistics`.
Grow mode may expand retained vectors, but allocation failure is converted into
the same rejection path rather than escaping a `noexcept` boundary.

Text is decoded as strict UTF-8, shaped into cached text runs, and rendered from texture atlases. The Win32 font loader is optional and is not part of the platform-neutral core.
The text-run cache has a configurable maximum entry count and reuses old slots, so rapidly changing telemetry strings cannot cause unbounded process-lifetime growth.

## 3D instance pipeline

`ShapeBatch3D` is a producer-side builder. `snapshot()` publishes shareable immutable storage containing `BoxInstance` values, an identity, a content revision, and a dirty range. View matrices and animation time are deliberately absent from the instance revision.

The box fast path uses twelve fixed edges. Each edge is represented by two triangles generated from the vertex ID; the vertex shader projects the endpoints and expands them in viewport space. Consequently:

- N boxes do not produce N CPU meshes;
- N boxes in one depth/material group produce one instanced draw;
- line width is consistent without `GL_LINES`, D3D line primitives, or geometry shaders;
- hue cycling is driven by a frame constant and per-instance offset;
- camera and time changes do not upload the instance buffer.

The OpenGL backend allocates its maximum instance storage during initialization and maps only changed ranges. It never calls `glBufferData` from the render path. The D3D12 backend permanently maps one upload buffer per fence-owned submission slot. It never waits or allocates from `record()`.

## Host ownership

HeniaUI does not own hooks, windows, OpenGL contexts, swap chains, back buffers, command allocators, resource transitions, queues, or fences.

For OpenGL, the host makes the correct context current and keeps one device instance per resource-sharing context group. The renderer restores mutable GL state that it changes. The host explicitly tells `render()` whether the currently bound framebuffer has a compatible depth attachment; HeniaUI does not guess from deprecated global queries.

Backend `lastError()` storage is fixed and bounded, so reporting an allocation
failure cannot itself allocate. OpenGL and D3D12 texture/submission bookkeeping
is sized during initialization and does not grow in routine synchronization or
render submission.

For D3D12, the host binds the render/depth targets, transitions resources, supplies a recording command list, submits it, and associates its submission slot with a fence. A slot cannot be reused for different instance content until that fence completes.

Depth is generic `DepthState`, not a visibility policy. When a depth-enabled request cannot use a host depth attachment, the backend selects a depth-disabled pipeline and increments `depthFallbacks`.

## Profiling

Statistics separate producer build time, instance upload time, draw-recording/submit time, and optional GPU time. GPU timing is unavailable until the host reports a resolved timestamp sample; a zero value is therefore never silently presented as a measured GPU duration.

## Threading

The producer mutates `ShapeBatch3D` outside the render callback and publishes a new snapshot only when content changes. The callback consumes a `const InstanceBatch&` and `const RenderPacket&`. Routine submission has no event bus, `std::function`, per-primitive allocation, or lock wait.

Widget callbacks may throw. Exceptions propagate through the input-dispatch and
Win32-adapter APIs so the host can choose its own exception boundary; no callback
is invoked through a false `noexcept` promise.
