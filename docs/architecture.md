# HeniaUI architecture

HeniaUI deliberately separates retained 2D interface work from general-purpose 3D instance work. They can be submitted in the same host frame, but they do not share a vertex format and neither layer depends on ImGui.

## 2D retained pipeline

`UiDocument` owns a widget tree. A widget keeps an independently revisioned
local display-list segment; paint invalidation propagates only an aggregate
"dirty descendant" bit through its ancestors. Composition follows dirty
branches, calls `onPaint()` only for dirty widgets, and skips a stable sibling
subtree as one retained segment range. Layout invalidation still reaches the
root when a descendant's desired size can affect its ancestors, but cached
measurement and unchanged-frame checks avoid remeasuring or arranging stable
branches. Visibility and child-order changes rebuild the segment topology while
reusing local segments whose frame and paint revision remain valid.

Segment views stay in depth-first paint order, so partial rebuilds preserve
clipping and draw order without concatenating stable `DrawCommand` vectors.
`BatchCompiler` caches validated backend-neutral instances by segment identity
and revision, then rebuilds only the global batch/texture-table envelope needed
by each immutable packet. A stable document returns a cheap handle to the same
`RenderPacket` storage, so both CPU composition and backend instance upload are
skipped. Null-root and nonpositive-viewport documents likewise retain one empty
snapshot until their visible output can change. `Canvas` remains available as
the low-level immediate recorder for custom widgets and generated diagrams.

`RenderPacketBuilder` is the single-producer mutable compile target;
`RenderPacket` is a copyable, immutable snapshot handle. A builder owns a pool of
three prewarmed storage slots by default. Atomic reader counts keep a published
slot unavailable until every snapshot handle has released it, allowing producer
composition and render-thread reads to overlap without locks or data races.
Grow mode adds a slot only when all warmed slots are held. Fixed mode rejects the
new build deterministically and keeps the last published packet; `UiDocument`
keeps paint dirtiness set so the composition is retried. Each publication has a
storage identity and a pool-monotonic revision. Stable retained frames reuse both,
which preserves backend upload-revision caching and provides the lifetime model
needed by future subtree caching. Snapshot spans remain valid only while their
`RenderPacket` handle is alive; handles keep the underlying pool alive even after
the producing `Frame` is destroyed. The host still supplies the synchronization
that publishes a handle between threads.

`Frame::reserve(..., CapacityPolicy::Fixed)` establishes a no-growth recording
contract. Display-list overflow is rejected at the canvas, while packet-capacity
overflow publishes no partial packet and is reported in `PacketStatistics`.
Grow mode may expand retained vectors, but allocation failure is converted into
the same rejection path rather than escaping a `noexcept` boundary.

Text is decoded as strict UTF-8, shaped into cached text runs, and rendered from texture atlases. The Win32 font loader is optional and is not part of the platform-neutral core.
The text-run cache has a configurable maximum entry count and reuses old slots, so rapidly changing telemetry strings cannot cause unbounded process-lifetime growth.

### Widget mutation and callback ordering

`UiDocument` identifies hovered, captured, and focused widgets by stable widget
identity rather than by pointers retained across callbacks. `setRoot()`,
`removeWidget()`, and `reparentWidget()` requests made by an input or focus
callback are queued and applied in request order after the outer dispatch
returns. Invalidated requests become no-ops when the queue is drained. If a
callback throws, its queued structural changes are discarded and interaction
state is cancelled before the exception propagates.

Pointer-down establishes capture and then changes focus before delivering the
pointer event. Focus cancellation clears the document's focus identity and the
widget's focused flag before delivering `FocusLost`. Pointer-up delivers to the
captured widget, releases its pressed/captured state, updates hover, and only
then applies deferred mutations. Removing a subtree clears hover, capture, and
focus flags before delivering `FocusLost` and destroying the subtree.

Recursive `dispatch()` calls are rejected deterministically, return `false`,
and increment `UiDocumentStatistics::rejectedNestedDispatches`. `compose()` is
allowed during a callback and observes the current tree; queued structural
changes become visible only after the outer dispatch completes.

## 3D instance pipeline

`ShapeBatch3D` is a producer-side builder. `snapshot()` publishes shareable immutable storage containing `BoxInstance` values, an identity, a content revision, and a dirty range. View matrices and animation time are deliberately absent from the instance revision.

The box fast path uses twelve fixed edges. Each edge is represented by two triangles generated from the vertex ID; the vertex shader projects the endpoints and expands them in viewport space. Consequently:

- N boxes do not produce N CPU meshes;
- N boxes in one depth/material group produce one instanced draw;
- line width is consistent without `GL_LINES`, D3D line primitives, or geometry shaders;
- hue cycling is driven by a frame constant and per-instance offset;
- camera and time changes do not upload the instance buffer.

The OpenGL backends allocate a configurable ring of complete instance buffers
during initialization and never call `glBufferData` from the render path. Every
drawn slot receives a `glFenceSync`. Before any later CPU write, the renderer
polls that fence with a zero timeout; it never waits. A cached revision may be
drawn again from an in-flight slot because it is read-only. Changed content uses
only a signaled slot, or the frame is rejected with upload-slot exhaustion
statistics. Fence failures quarantine their slots from future writes.

Both full 2D uploads and full/partial 3D uploads use this fence-owned model with
`GL_MAP_UNSYNCHRONIZED_BIT`; the bit is safe because selection has already proven
that the chosen buffer is no longer referenced by an in-flight draw. A 3D dirty
range is uploaded partially only when the selected safe slot contains the same
batch identity at exactly the previous revision. Otherwise the renderer performs
a full upload and increments `fullUploadFallbacks`. Statistics also expose
uploaded bytes, slot exhaustion, and fence failures.

The D3D12 backend permanently maps one upload buffer per fence-owned submission
slot. It never waits or allocates from `record()`.

## Host ownership

HeniaUI does not own hooks, windows, OpenGL contexts, swap chains, back buffers, command allocators, resource transitions, queues, or fences.

The complete initialization, multi-instance, context/device-loss, destruction,
submission-fence, resize, and recreation rules are documented in
[Renderer ownership and recreation](resource-lifetime.md).

Performance changes use the fixed scenes and CPU/upload/memory accounting in
[Renderer benchmark methodology](benchmarks.md). Pull requests compare the base
and candidate executables on the same runner instead of treating workstation
nanoseconds as portable constants.

For OpenGL, the host keeps the exact context passed to `initialize()` current for
every renderer call that can access GL objects. HeniaUI deliberately validates
the `HGLRC` rather than assuming that another context belongs to the same share
group, because WGL provides no portable share-group identity query. Calls on a
different or missing context fail before issuing GL work. `shutdown()` returns
`false` and preserves the GL objects in that case, so the host can make the owner
context current and retry. The destructor cannot perform that retry; hosts must
therefore call and check `shutdown()` before destroying a live renderer. If the
context is permanently lost first, `abandon()` clears stale names without GL
calls and permits initialization on a replacement context.

Each render submission uses a full state-isolation contract. The 2D and 3D
backends capture and restore the current program (or bind zero when a captured,
pending-delete program disappears), VAO, array-buffer binding, viewport,
scissor box, blend enable/factors/equations, depth/cull/stencil enables,
front/back polygon modes, draw-buffer-zero color mask, framebuffer sRGB,
rasterizer discard, color logic operation, dither, multisampling, sample-alpha
controls, and sample coverage. The 2D path additionally isolates texture and
sampler bindings for texture units 0 through 7. The 3D path additionally
isolates the depth function, depth write mask, and depth range. Texture
synchronization isolates the texture binding plus pixel-unpack buffer,
alignment, and row length. Initialization also preserves the host VAO and
array-buffer bindings.

OpenGL error flags cannot be restored. Immediately before an isolated render or
destruction boundary, HeniaUI drains pre-existing host errors, counts them in
`ignoredHostErrors`, and attributes only later errors to its own work. State
restoration failures are reported separately. The host explicitly tells
`render()` whether the currently bound framebuffer has a compatible depth
attachment; HeniaUI does not guess from deprecated global queries.

OpenGL initialization is transactional: shader/program creation, VAO/VBO name
creation, every buffer-storage allocation, and vertex-layout setup are checked
before `initialized()` can become true. A failure destroys every name allocated
by that attempt and preserves a stage-specific diagnostic containing the GL
error and resource or upload-slot identifier.

Texture synchronization never redefines a renderer-visible texture in place.
Every changed texture is uploaded into a staged object; the whole call swaps
objects and revisions only after all candidates succeed. Any failure deletes all
candidates and leaves every previous object/revision usable and retryable.
Immutable one-level storage plus `glTexSubImage2D` is used when
`glTexStorage2D` is available; otherwise a newly created mutable object is
populated with `glTexImage2D`. Retired objects are deleted only after the atomic
swap. OpenGL deletion semantics retain storage needed by already submitted
commands, so an update never mutates content referenced by an in-flight draw.
This policy can transiently require both the old and replacement storage; an
allocation failure rolls back rather than sacrificing the valid texture.

Backend `lastError()` storage is fixed and bounded, so reporting an allocation
failure cannot itself allocate. OpenGL and D3D12 texture/submission bookkeeping
is sized during initialization and does not grow in routine synchronization or
render submission.

For D3D12, the host binds the render/depth targets, transitions resources, supplies a recording command list, submits it, and associates its submission slot with a fence. A slot cannot be reused for different instance content until that fence completes.

D3D12 command-list state cannot be queried and restored generically. Both
renderers therefore expose a clobber-and-rebind contract: they validate a direct
list from the initialization device, overwrite their documented graphics state,
and require the host to bind its full draw state afterward. Textured UI packets
bind the renderer's private shader-visible heap; texture-free packets use a
heap-free root signature and PSOs. Per-slot descriptor tables are recopied only
when their texture handles or committed revisions change. The exact state list,
attachment expectations, dedicated-list containment option, and before/after
example are in [D3D12 command-list integration](d3d12-integration.md).

Depth is generic `DepthState`, not a visibility policy. When a depth-enabled request cannot use a host depth attachment, the backend selects a depth-disabled pipeline and increments `depthFallbacks`.

## Input validation and numeric policy

Public 2D rectangles and 3D boxes use a reject-inverted policy; HeniaUI does not
silently swap endpoints. All recorded coordinates, colors, line metrics, view
matrices, viewport dimensions, and animation constants must be finite. Colors
may intentionally use HDR or out-of-range channel values, so finite channels are
not clamped. `tryPerspective()` and `tryLookAt()` report invalid or degenerate
parameters explicitly; the convenience matrix-returning forms produce a finite
identity matrix when those checks fail.

Canvas and batch compilation validate before publishing GPU constants, and each
backend repeats a complete preflight before mapping buffers or recording draw
commands. `lastError()` names the rejected field. Invalid-input counters are
separate from capacity-exhaustion counters while the aggregate rejection count
is retained for compatibility.

Clip rectangles are converted once with floor for minima and ceil for maxima,
then clamped to the viewport. This preserves fractional analytic-AA coverage and
gives OpenGL and D3D12 identical integer coverage. Capacity products, byte
ranges, descriptor counts, and narrowing conversions are checked before any
allocation or API call; configurations whose instance-buffer view would exceed
`GLsizeiptr` or D3D12's `UINT` byte count are rejected during initialization.

## Profiling

Statistics separate producer build time, instance upload time, draw-recording/submit time, and optional GPU time. GPU timing is unavailable until the host reports a resolved timestamp sample; a zero value is therefore never silently presented as a measured GPU duration.

## Threading

The producer mutates `ShapeBatch3D` or `RenderPacketBuilder` outside the render
callback and publishes a new immutable snapshot only when content changes. The
callback consumes a `const InstanceBatch&`, owns a `RenderPacket` snapshot handle,
and passes it to a backend by const reference. Routine submission has no event
bus, `std::function`, per-primitive allocation, or lock wait. Packet handles may
be copied across threads through the host's normal publication primitive; all
access through a published handle is read-only.

Widget callbacks may throw. Exceptions propagate through the input-dispatch and
Win32-adapter APIs so the host can choose its own exception boundary; no callback
is invoked through a false `noexcept` promise.
