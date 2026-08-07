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

### Compact 2D draw payloads

The retained and compiled streams keep paint order in one contiguous sequence,
but each entry now carries only two four-float payload rectangles. A regular
rectangle uses them as geometry and UV data, a line uses them as ordered
endpoints and neighbours, and a stroke uses them as a tight raster region and
its original logical rectangle. Advanced analytic primitives reuse the UV
rectangle and two metrics for parameters such as gradient color, arc angles,
shadow offset, independent corner radii, and nine-patch borders. This tagged
reuse removes kind-specific payloads without introducing per-kind streams,
indirection, or additional draw calls.

`DrawInstance` keeps the full four-float color and two float metrics so HDR and
additive colors do not lose precision. Its final four bytes contain primitive
kind, the eight-entry batch texture slot, cap, and packed join/adjacency bits.
OpenGL and D3D12 both consume the same 60-byte, four-byte-aligned layout through
five vertex attributes. Header and backend `static_assert`s lock its size,
offsets, standard-layout property, and shader input compatibility.

All instance colors are straight-alpha linear values. RGBA texture entries
carry resolved straight, premultiplied, or opaque alpha semantics plus linear or
sRGB transfer metadata; R8 entries are linear alpha masks. Native sRGB texture
formats decode samples before filtering/shading, and the shared shaders
normalize premultiplied samples before applying tint and perform exactly one
final premultiplication. Render-target transfer is an explicit host declaration,
not inherited state or a format guess. The complete public and backend contract
is in [Color, alpha, and texture contract](color-and-texture-contract.md).

`PacketStatistics` keeps source commands, compiled instances, and draw batches
as independent counts. `batchedInstancesBeyondFirst` is the sum of instances
after the first in each batch; it describes draw-call sharing, not compression.
Maximum instances per batch, texture slots used across textured batches,
clip/blend/texture-capacity boundary counts, and cold full-instance-upload bytes
make batch efficiency explicit. Actual backend upload bytes remain separate
because a stable immutable packet can be submitted without another upload.
`effectInstances`, `shaderVariantTransitions`, and
`effectEstimatedFragmentArea` expose effect density, adjacent shader-branch
changes, and the conservative effect-only fragment bound without splitting a
batch merely because its primitive kind changes.

### Analytic 2D geometry

Widget, text, and display-list geometry is expressed in logical UI units.
`UiCoordinateSpace` defines independent input-to-logical and
logical-to-framebuffer transforms per document/window; both backends apply the
latter only at the raster boundary and convert logical clips to physical
scissors. DPI is explicit resource metadata rather than an implicit layout
multiplier. The full host and injected-window contract is in
[Coordinate spaces, DPI, and framebuffer scaling](coordinate-spaces.md).

Analytic shape instances keep logical SDF bounds separate from raster geometry.
Filled and stroked rectangles expand their GPU geometry by a controlled two-pixel
fringe, so the outside half of the antialias transition is not clipped while
the fragment shader continues to evaluate the caller's original rectangle.
`StrokeRect` compiles to up to eight non-overlapping edge/corner regions instead
of shading its full interior. Fixed-capacity callers can use the `Frame::reserve`
and `UiDocument::reserve` overloads with separate command and expanded-instance
budgets.

Circle/ellipse, arc, capsule, rounded linear-gradient, cheap rounded-shadow,
and independent-corner border commands each compile to one 60-byte analytic
instance. Matching OpenGL GLSL and D3D12 HLSL evaluate their coverage in the
fragment stage; arcs use round end caps and shadows use a bounded Gaussian-like
falloff without an intermediate render target. A uniform-border nine-patch uses
one textured instance and a piecewise UV remap. These focused primitives retain
the existing batch and texture-table model without introducing a general path
or filter engine.

Lines are expanded by the vertex shader into oriented quads. Their width is
logical by default and therefore follows UI scaling; hosts that need a stable
physical width use `logicalLineWidthForPhysicalPixels` before recording.
Open paths expose butt, square, and round caps. Polylines expose bevel and round
joins. Adjacent segments carry their neighbor endpoints and partition coverage
by signed distance, while the incoming segment owns the bevel triangle. This
keeps translucent joins from blending twice without a stencil pass or an
offscreen mask.

When `Frame::setFragmentAreaTracking(true)` is enabled,
`PacketStatistics::estimatedFragmentArea` sums a conservative pre-clip area of
the generated quads. Tracking is opt-in because its producer-side arithmetic is
measurable. It makes fragment-work regressions observable alongside draw/instance
counts; it is deliberately not presented as a hardware occlusion or timestamp
query.

### Composable overlay effects

`Canvas::effectRect` records enabled `EffectLayer` entries in caller order.
Tint, static or host-phased animated gradient, analytic glow, soft shadow, and
rounded outline are ordinary tagged instances in the existing ordered stream.
`Canvas::sdfIcon` adds a texture-backed red-channel distance-field variant;
`Canvas::scopedClip` remains the rectangular masking primitive. Image tinting
continues to use the regular image instance. The layer list is therefore a
small deterministic paint recipe, not a retained GPU effect graph.

No effect allocates an intermediate render target or introduces an arbitrary
full-screen pass. Gradient/tint/outline geometry expands only by the analytic
AA fringe. Shadow and glow expand by three configured radii plus that fringe,
which makes their bounded fill-rate cost visible through
`effectEstimatedFragmentArea`. A rounded outline uses one composable quad and
shades its interior; `strokeRect` remains the lower-fragment alternative that
compiles to as many as eight tight regions. SDF icons consume an existing batch
texture slot. Disabling a layer emits no command, instance, upload, or fragment
work.

Animated gradients have no hidden renderer clock. The host supplies phase in
cycles; recording normalizes and quantizes it into the existing final instance
byte. Both GLSL and HLSL decode the identical 60-byte payload and select effects
inside the same uber-shader used by simple UI. Primitive-kind changes are
observable as shader-variant transitions but do not create batch or pipeline
state boundaries.

### Text shaping, fallback, and atlas growth

Text storage and cluster offsets are strict UTF-8. The dependency-free default
shaper performs codepoint lookup and same-face kerning through an ordered font
fallback chain. `TextShapingBackend` is an optional host integration point for
HarfBuzz or another complex shaper: it returns visual-order glyph IDs, advances,
offsets, and UTF-8 cluster ranges. No shaping library is linked by ASCII-only or
minimal builds.

`TextLayoutCache` stores face/glyph identity, cluster-aware bounds, line metrics,
caret stops, and hit-test/selection geometry, but no atlas UVs. The independent
`TextRenderCache` resolves the current atlas pages and creates contiguous
texture segments only when a layout is painted. `TextRunCache` retains the old
facade while owning both bounded caches. Font-face revisions invalidate stale
fallback/layout and render entries lazily without changing a stable
`FontHandle`.

`DynamicGlyphAtlas` accepts host-rasterized Alpha8 glyphs and grows by allocating
fixed-size pages. Pages never resize, so normalized UVs already present in an
immutable packet remain valid; new pages still participate in the existing
eight-texture batch table. Batch insertion plans page placement and groups all
writes per texture into one staged revision; texture pixels, shelf state, and
font metadata commit atomically or remain unchanged. `Win32FontLoader::appendGlyphs`
provides the optional GDI rasterizer bridge; the platform-neutral core does not
depend on it.

`Win32AsyncFontSet` adds a bounded on-demand path without changing that ownership
model. The owner thread walks the active locale-aware Latin, CJK, Japanese,
Korean, symbol, and emoji chain until the first eligible face, and deduplicates
jobs by face, physical raster-size variant, and scalar. Retryable raster/commit
failures use bounded exponential backoff; confirmed missing glyphs advance to
the next chain candidate. One private DirectWrite worker owns its font-face
interfaces and produces intentionally grayscale Alpha8 bitmap results. A second bounded SPSC queue
returns those results; a semaphore parks the worker when result storage is full
instead of spinning. Neither worker operation accesses `FontStore`,
`TextureStore`, `DynamicGlyphAtlas`, `TextPainter`, `UiDocument`, or a renderer.

`TextGlyphRequestBackend` lets `TextPainter` enqueue text before layout, so all
existing controls gain on-demand fallback without platform dependencies in
Core. The host calls `commitReady(budget)` on the font-set owner thread, then
synchronizes changed textures, calls `UiDocument::invalidateTypography()`, and
only then composes. Face revisions lazily reject stale layout/render cache
entries; explicit document invalidation is still required because an otherwise
stable retained tree has no reason to revisit typography. Atlas pages may be
preallocated during font-set construction so interactive commits perform only
region uploads and bounded font publication until those pages fill.
`TextFontRasterResolver` selects bounded integer physical-pixel variants close
to the final requested size, while glyph run origins are snapped in framebuffer
space without rounding unrelated UI geometry. Explicit `releaseResources()`
joins the worker before reclaiming owned dynamic glyphs, variant faces, and
atlas textures.

The built-in routing is scalar fallback, not a universal shaping engine.
Locale-specific chains select the preferred Han forms. DirectWrite emoji
outlines are flattened to the existing monochrome Alpha8 contract. Arabic,
Indic, bidirectional ordering, ligatures, variation sequences, and other
multi-codepoint behavior remain the responsibility of `TextShapingBackend`.

`TextEditorState` owns committed UTF-8, codepoint-boundary cursor/selection,
bounded undo/redo snapshots, clipboard operations, and a separate IME preedit
range. Preedit updates never mutate committed storage; commit is one undoable
replacement and cancel is lossless. `TextInput` adds pointer selection,
selection/caret/composition painting, single- or multi-line entry, shortcuts,
Insert-controlled UTF-8 overwrite mode, and fallback fonts on top of that
state.

### Theme cascade and invalidation

The document theme owns semantic colors, inherited typography, control metrics,
container spacing, and a styling-density scale. `Panel`, `Label`, `Button`,
and `NumericInput` derive their class defaults from those tokens. Their public
style structures contain `ThemeProperty<T>` values: an empty property inherits
the current document token, while a populated property is a stable local
override. Clearing a property restores inheritance without reconstructing the
widget tree.

`UiDocument::setTheme()` compares the incoming layout token set separately from
paint-only tokens. Color, border, and radius changes recursively invalidate
retained paint segments without discarding measurements. Font, size, control
dimensions, inherited padding/spacing, or density changes recursively
invalidate measurement and layout caches. Attached controls resolve the theme
during both measurement and paint; detached controls use the default theme.
The density value scales style metrics only and does not replace the explicit
host input/framebuffer coordinate-transform contract.

### Widget measurement and panel allocation

Each widget caches its measured size by the normalized pair of minimum/maximum
constraints, independently from the flag that says it still needs arranging.
A content or layout-parameter change invalidates measurement for the widget and
all ancestors. The two-entry constraint cache covers a panel's base and final
allocation queries without remeasuring stable siblings. The final measurement
context also ensures that `onArrange()` runs when needed even if the eventual
rectangle is numerically unchanged.
Constraint axes are normalized to nonnegative, ordered, finite ranges; positive
infinity maps to the largest finite float, while NaN, negative infinity, and an
inverted maximum collapse to the normalized minimum. Measurement results are
likewise clamped without relying on an invalid `std::clamp` precondition.

Rows and columns measure visible children in paint order against the remaining
main-axis space after padding, gaps, and nonnegative margins. Arrangement first
collects those constrained bases, distributes positive leftover space by
`flexGrow`, then remeasures every child with its exact final main/cross size
before arranging nested content. On the cross axis, an explicit child width
(column) or height (row) takes precedence over `stretchCrossAxis`; automatic
children stretch to the available size. Oversized content is constrained to
the panel content rectangle instead of pushing later siblings beyond it.

### Input capability and invalidation

Hit testing always descends visible, enabled containers, but returns a widget
itself only when `acceptsPointerInput()` opts it in. The base widget, `Panel`,
and `Label` are passive; `Button`, `NumericInput`, and `TextInput` explicitly accept pointer
input and keyboard focus. Custom controls make the same capability choice
instead of relying on a speculative `handleInput()` call. A pointer down first
has to be handled successfully; only then may the document focus, capture, and
press the still-attached interactive target. A down sequence without a
surviving capture consumes its later pointer-up instead of retargeting it to an
unrelated widget.

Every attached widget knows its owning document. Hiding or disabling a widget
therefore synchronously clears hover/capture/focus for its whole subtree;
detaching and root replacement use the same path. Focus identity and the local
focused flag are cleared before exactly one `FocusLost` callback is delivered,
so callbacks may safely request deferred tree mutations. `NumericInput`
commits a valid in-progress edit on every focus loss, including hide/disable;
that callback can propagate, so `setVisible()` and `setEnabled()` are not
`noexcept`. Hidden/disabled controls receive no later pointer or keyboard input.
Clicks on a passive background clear an existing focus but otherwise do not
dirty paint or publish a replacement packet.

### Production overlay controls and virtualization

The production control set covers checkbox/toggle, slider, combo box, tab bar,
scroll container, virtual list, tooltip, modal popup layer, color picker, key
binding editor, and tree view. Controls own their strings or node/item arrays
explicitly. Large lists may instead use `ValueCallback<std::string_view,
std::size_t>` as a non-owning, allocation-free label provider; the provider and
its returned view must outlive the synchronous paint call. Event callbacks use
the same context/function-pointer representation as existing controls.

Document-level Tab and Shift+Tab traversal walks the existing tree directly and
wraps without constructing a focus vector. Hidden or disabled subtrees are
skipped. A focused `KeyBindingEditor` opts into Tab only while capturing so Tab
can be bound and normal traversal resumes immediately afterward. Unhandled
wheel events bubble through interactive ancestors, allowing a nested control to
sit inside a `ScrollContainer` without forwarding wheel input manually.

Ancestor child clips are intersected and embedded into each descendant's
retained draw commands. A scroll viewport change invalidates affected
descendant paint segments; stable clipped subtrees remain reusable. The direct
label modes of `ListView` and `TreeView` keep one retained widget and emit only
fixed-height rows that overlap their viewport. Their backing item count can
therefore be much larger than command and instance counts.

`ListView::setRecycledItems()` adds lazy widget-backed rows without changing
that scaling contract. `VirtualListSource` is non-owning and supplies a unique,
stable `ListItemKey`, optional item extent, lazy factory, and rebind callback.
The pool grows only when a viewport needs more visible plus overscan slots and
does not shrink or allocate during steady-state scrolling. Physical widget
identity belongs to a pool slot; logical selection belongs to the data key.
`refreshRecycledItems()` rebuilds optional prefix extents and resolves an
existing selection against new ordering by key. Source updates are O(total
items); each scroll is O(log total items + visible items) for variable extents,
or O(visible items) for fixed extents.

Recycled children are explicitly presentation-only. `allowsChildInteraction()`
stops hit testing, focus traversal, and stale-interaction validation at the list
while leaving child layout, retained painting, and ancestor clipping intact.
Pointer and keyboard selection therefore cannot follow a recycled widget to a
different logical item. Factories and binders may throw to the host; callback
contexts and any borrowed data must outlive the list. A null factory result is
an explicit composition error rather than a partially realized list.

`PopupLayer` owns normal content, a modal backdrop, and popup content in that
paint order. Popup bounds are local to and clamped within the layer viewport.
Backdrop dismissal is handled by the layer without host hit testing. Tooltip
visibility is explicit so hosts choose hover delay and scheduling policy rather
than inheriting a hidden UI timer.

### Win32 input and message ownership

`Win32InputAdapter` observes one host-owned `HWND` message stream and never
subclasses or destroys the window. A handled left/right/middle button down calls
`SetCapture()`; the adapter tracks every pressed supported button and calls
`ReleaseCapture()` only after the last corresponding up. If capture cannot be
acquired, the document pointer sequence is cancelled immediately. An expected
`WM_CAPTURECHANGED` caused by that final release does not cancel keyboard focus.

External `WM_CAPTURECHANGED` and `WM_CANCELMODE` dispatch platform-neutral
`PointerCancel`, which clears hover/capture/pressed state while preserving
keyboard focus. `WM_KILLFOCUS`, `WM_DESTROY`, and `WM_NCDESTROY` release native
capture, discard a pending UTF-16 high surrogate, and dispatch `FocusLost` to
clear all interaction. Hosts must route destruction messages to the adapter
before invalidating its document or adapter pointer.

`WM_IME_STARTCOMPOSITION`, `WM_IME_COMPOSITION`, and
`WM_IME_ENDCOMPOSITION` become synchronous platform-neutral composition
start/update/commit/cancel events. Preedit and result strings are converted from
UTF-16 to UTF-8, including a byte-accurate composition caret. A handled result
is remembered only across its immediate UTF-16 character delivery and cleared
at composition end, preventing the committed text from being inserted twice.
`WM_CHAR` and `WM_UNICHAR` contribute committed Unicode text only. C0/C1
control codes such as Backspace, Tab, Enter, and Delete are consumed as
duplicates of their key messages, while editing and navigation remain
`KeyDown` events. This keeps input-method text separate from physical-key
semantics and prevents control bytes from entering UTF-8 editor storage.

The return value is a consumption decision. Pointer, wheel, key, and completed
text/composition messages return the document handler result. A buffered UTF-16 high
surrogate and `WM_UNICHAR`'s `UNICODE_NOCHAR` capability probe return `true`.
Capture/focus/cancellation/destruction notifications are observed but return
`false` so normal host window processing continues. Valid surrogate pairs are
combined; isolated surrogates and invalid `WM_UNICHAR` scalar values dispatch
U+FFFD. Unsupported messages return `false` unchanged.

### Widget mutation and callback ordering

`UiDocument` identifies hovered, captured, and focused widgets by stable widget
identity rather than by pointers retained across callbacks. `setRoot()`,
`removeWidget()`, and `reparentWidget()` requests made by an input or focus
callback are queued and applied in request order after the outer dispatch
returns. Invalidated requests become no-ops when the queue is drained. If a
callback throws, its queued structural changes are discarded and interaction
state is cancelled before the exception propagates.

Pointer-down asks the hit target to handle the event, changes focus, and then
establishes capture/pressed state. Focus cancellation clears the document's
focus identity and the widget's focused flag before delivering `FocusLost`.
Pointer-up delivers to the captured widget, releases its pressed/captured state,
updates hover, and only then applies deferred mutations. Removing a subtree
clears hover, capture, and focus flags before delivering `FocusLost` and
destroying the subtree.

Recursive `dispatch()` calls are rejected deterministically, return `false`,
and increment `UiDocumentStatistics::rejectedNestedDispatches`. `compose()` is
allowed during a callback and observes the current tree; queued structural
changes become visible only after the outer dispatch completes.

## 3D instance pipeline

`ShapeBatch3D` is a producer-side builder. `snapshot()` publishes shareable,
immutable 256-instance pages containing `BoxInstance` values, an identity, a
content revision, and an ordered set of non-overlapping dirty ranges. View
matrices and animation time are deliberately absent from the instance revision.

The producer page directory is copied when a published revision is still live,
but box data is copied only for pages that are changed. Initial full replacement
uses multi-page allocation slabs to keep cold-build allocation count bounded.
The snapshot exposes a segmented `BoxInstanceView`; indexed reads and iteration
are lock-free, and backends consume page spans directly. Stable snapshots share
the exact same directory/pages and allocate nothing. `copiedBoxCount()` makes
producer data-copy amplification observable in tests and benchmarks.

The box fast path uses six fixed faces and twelve fixed edges. A quantized fill
opacity plus fill/outline flags live in unused bits of `BoxInstance::effects`, so
the public/GPU instance remains 64 bytes. Each face has four projected corner
vertices and six reusable indices. Each edge has four unique strip-order vertices
and six reusable indices; the vertex shader projects the endpoints and expands
the indexed quad in viewport space. Face indices precede edge indices so a combined
source-over draw lays the translucent fill down before its outline. Consequently:

- N boxes do not produce N CPU meshes;
- N boxes in one depth/material group produce one instanced draw;
- fill-only, outline-only, and mixed boxes share the same batch and instance layout;
- line width is consistent without `GL_LINES`, D3D line primitives, or geometry shaders;
- hue cycling is driven by a frame constant and per-instance offset;
- camera and time changes do not upload the instance buffer.

An optional `VisibilityList` derives a compact stream without mutating that
source contract. It caches one conservative base AABB/mask union plus minimum
and maximum motion delta per immutable page, rebuilds and validates only changed
source revisions, and preserves ascending source indices plus complete
`BoxInstance` values. Camera changes reuse page bounds; time-only changes reuse
the entire result. A changing global motion scale evaluates each page envelope
in O(1), including negative scales, and only surviving pages receive exact box
tests. Application masks, six-plane homogeneous frustum rejection,
and optional projected-size filtering are applied without frame-path allocation.
Direct mode remains the default and bypasses this workspace. The full policy,
conservative edge margin, statistics, and measured threshold are documented in
[3D visibility and indirect submission](3d-visibility.md).

Before the perspective divide, every edge is clipped as a homogeneous segment
against `w >= 0.0001` and the six canonical clip planes. The near plane is
selected from `ViewParameters::clipDepthRange`: `z >= 0` for `ZeroToOne`, or
`z + w >= 0` for `MinusOneToOne`; the far plane is `w - z >= 0` in both
conventions. Intersections are computed in clip space, and only finite endpoints
with safe positive `w` reach viewport-space line expansion. This shortens an
edge that crosses the camera or any frustum plane instead of discarding the
whole edge or dividing by a near-zero value. OpenGL and D3D12 then perform only
their API-specific depth-range conversion on the already clipped endpoint.

After clipping, both shaders express each edge in pixel-space `along` and
`across` coordinates. Those varyings use `noperspective` interpolation; segment
length, requested half-width, color, and effects are flat. The quad extends
1.25 pixels beyond both endpoints and both sides, while the fragment shader
evaluates the signed distance to the requested screen-space rectangle. This
keeps width and the AA transition constant when endpoint clip-space `w` values
differ, and preserves the fringe of a butt cap instead of clipping it at the
endpoint.

The 3D cap/join policy is deliberately simple: every one of the twelve box
edges has an analytic butt cap, and a box corner is the deterministic
source-over overlap of its incident premultiplied edges rather than a generated
3D miter or round join. Opaque corners remain visually uniform. Translucent
corners are darker than an isolated edge because source-over coverage is
accumulated; normalizing that overlap would require joined topology or an
intermediate coverage target and is not hidden behind incorrect blend factors.
Shared OpenGL/D3D12 golden probes lock the overlap range, cap fringe, subpixel
coverage, thick width, and near-to-far width constancy.

The OpenGL backends allocate a configurable ring of complete instance buffers
during initialization and never call `glBufferData` from the render path. Every
drawn slot receives a `glFenceSync`. Before any later CPU write, the renderer
polls that fence with a zero timeout; it never waits. A cached revision may be
drawn again from an in-flight slot because it is read-only. Changed content uses
only a signaled slot, or the frame is rejected with upload-slot exhaustion
statistics. Fence failures quarantine their slots from future writes.

Both full 2D uploads and full/partial 3D uploads use this fence-owned model with
`GL_MAP_UNSYNCHRONIZED_BIT`; the bit is safe because selection has already proven
that the chosen buffer is no longer referenced by an in-flight draw. 3D dirty
ranges are uploaded separately only when the selected safe slot contains the
same batch identity at exactly the previous revision. Sparse edits therefore do
not map/copy the bounding interval between them. Otherwise the renderer performs
a full page-by-page upload and increments `fullUploadFallbacks`. Statistics also
expose uploaded bytes, slot exhaustion, and fence failures.

When CPU visibility is enabled, OpenGL uploads the compact stream as a full
visibility revision into the same fence-owned ring and keeps the ordinary
instanced draw. No OpenGL indirect or compute extension is required.

The D3D12 backend permanently maps one staging buffer per fence-owned submission
slot. Depending on the configured/automatic instance-storage strategy,
`record()` either binds it directly or copies full pages/exact dirty ranges to
the slot's GPU-local default buffer. It never waits or allocates. Both 3D
backends validate and traverse the segmented pages in place rather than
materializing a contiguous CPU vector.

For a CPU-culled gfx submission, each D3D12 submission slot also owns a mapped
draw-argument resource protected by the same host fence. The compact instance
stream follows the configured upload/default-heap strategy and
`ExecuteIndirect` consumes its visible count. Direct submissions continue to
use `DrawInstanced` and retain exact immutable dirty-range uploads.

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

Each render submission uses a full state-isolation contract by default. The 2D
and 3D backends capture and restore the current program (or bind zero when a captured,
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

Both OpenGL renderers accept a `DedicatedContext` state policy for a context
whose state is exclusively owned by the caller. In that mode they bind their
complete required pipeline state but skip synchronous host-state queries and
restoration during rendering. The default remains `Preserve`; using the
dedicated policy on a shared context is a contract error because the caller
must not expect prior bindings or enables to survive. The 2D renderer also has
an opt-in persistently mapped instance-upload ring when OpenGL 4.4 or
`ARB_buffer_storage` is available; transient map/unmap remains the baseline.

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

Full texture synchronization never redefines a renderer-visible texture in
place. Every full change is uploaded into a staged object; the whole call swaps
objects and revisions only after all candidates succeed. `updateRegions()`
groups all writes per texture into one logical revision and retains a
conservative dirty union plus rollback pixels; the corresponding one-revision
GPU update uses `glTexSubImage2D` for that rectangle. If a later upload in the
same synchronization call fails, HeniaUI restores the union before returning.
A genuinely unseen revision gap falls back to a full staged object. Immutable one-level storage is used
when `glTexStorage2D` is available; otherwise a new mutable object is populated
with `glTexImage2D`. Retired owned objects are deleted only after commit, while
borrowed external names are never deleted. OpenGL command ordering and deletion
semantics preserve work already submitted by the same owner context.

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

`Canvas::scopedClip()` returns a move-only scope tied to the exact stack token
that it pushed. Its destructor cannot pop a parent after a failed push, a manual
pop, or an out-of-order scope release; an out-of-order release remains pending
until its children leave. Valid clips whose nested intersection is empty still
occupy one stack entry, so drawing becomes a counted recording no-op and the
matching pop remains balanced. The manual `pushClip()` / `popClip()` pair remains
available for integrations that already balance it explicitly.

Canvas and retained-segment compilation reject primitives whose conservative
raster bounds do not overlap the active clip. Solid/stroked geometry includes
its analytic-AA fringe, and line bounds include half-width plus cap/join fringe,
so edge coverage is not over-culled. Empty stroke regions are removed during
expansion as well.

Clip rectangles are converted with floor for minima and ceil for maxima, then
clamped to the viewport. Empty results are rejected as no-op batches before any
instance upload or draw call. This preserves fractional analytic-AA coverage,
never passes a negative scissor extent to a backend, and gives OpenGL and D3D12
identical integer coverage. Capacity products, byte
ranges, descriptor counts, and narrowing conversions are checked before any
allocation or API call; configurations whose instance-buffer view would exceed
`GLsizeiptr` or D3D12's `UINT` byte count are rejected during initialization.

## Profiling

Every backend defines `frameAttempts` as calls to `render()`/`record()`,
`successfulFrames` as calls returning `true`, and `rejectedFrames` as calls
returning `false`; consequently `frameAttempts == successfulFrames +
rejectedFrames`. Successful UI and gfx submissions publish a profile sample
identified by sample ID, attempt ID, packet/batch identity and revision, and the
actual OpenGL upload-ring or D3D12 submission slot. Rejected attempts create a
gap in attempt IDs but never a successful sample.

`latestSample`, `cumulative`, and the fixed 60-sample `rollingAverage` are
separate views. Producer build time is counted only on the first consumer
sample for an immutable revision, while full uploads, exact dirty-range uploads,
zero-byte revision changes, and uploaded bytes are labeled independently. The
timeline retains 64 successful submissions without allocation or locking.

The host resolves a timestamp with `reportGpuTime(sampleId, nanoseconds)`.
Delayed results update the matching retained sample and `latestGpuSample`; they
never make a newer `latestSample` appear GPU-timed. Unknown, duplicate, expired,
or previous-lifetime sample IDs are rejected. A current sample with no resolved
timestamp keeps `gpuTimingAvailable == false`, including when an older result
has arrived.

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

Asynchronous Win32 font baking follows the same single-owner rule. Only its
DirectWrite worker and bounded queues are concurrent. The producer-side owner
performs requests and bounded commits; store revisions, atlas packing, texture
updates, renderer synchronization, and retained-document invalidation stay on
that owner thread. A full result queue blocks only the worker and never the UI
thread.
