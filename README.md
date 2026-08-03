# HeniaUI

HeniaUI is a compact retained UI and rendering engine for native C++ applications. It is being built around a backend-neutral display list, texture-table batching, immutable render packets, and renderer instances that can survive multiple windows, contexts, and graphics APIs.

The project is intentionally independent from ImGui and from any host application's hook, SDK, runtime, or input implementation. A host can embed the library with `add_subdirectory`, consume an installed CMake package, or build the included standalone sandbox.

> Status: usable foundation. The public API includes retained widgets, fallback/optionally-shaped UTF-8 text, editor-grade input, native Win32 input/font/IME adapters, native OpenGL 3.3 and Direct3D 12 UI renderers, plus a separate `henia::gfx` instance path for large 3D box fields.

## Why another UI renderer?

HeniaUI is designed to avoid several common scaling traps in game overlays and native tools:

- no draw call per primitive or glyph;
- no dependency on an immediate-mode debug UI in production builds;
- no GPU-buffer destruction or growth from inside a presentation callback;
- no render-thread locks for routine UI updates;
- fence-owned OpenGL upload rings with zero-timeout polling and explicit
  slot-exhaustion reporting;
- no rebuilding of stable layout and paint data without a dirty reason;
- no per-box CPU projection, antialias expansion, vertex generation, or index generation;
- no assumption that one process has only one graphics context or swap chain.

Shapes, images, and glyphs use one ordered UI pipeline. In addition to
rectangles and lines, the same compact instance stream supports analytic
circle/ellipse, arc, capsule, linear-gradient, rounded-shadow,
independent-corner border, single-instance nine-patch, and ordered local effect
layers. Tint, animated gradient, analytic glow/shadow/outline, rectangular
masking, and SDF icons stay in the same instance stream. Each
batch carries a small texture table, so alternating widget backgrounds and font
glyphs can remain in one draw batch while preserving paint order. A batch
boundary is introduced only for a clip, blend, pipeline, or texture-table
capacity change.

## Current example

```cpp
#include <array>
#include <henia/ui/Frame.h>

henia::ui::Frame frame;
frame.reserve(4096, 32, henia::ui::CapacityPolicy::Fixed);

auto& canvas = frame.begin();
canvas.fillRect(
    {{20.0F, 20.0F}, {420.0F, 240.0F}},
    {0.03F, 0.05F, 0.08F, 1.0F},
    12.0F);
canvas.gradientRect(
    {{40.0F, 40.0F}, {260.0F, 68.0F}},
    {0.12F, 0.65F, 0.95F, 1.0F},
    {0.70F, 0.20F, 0.95F, 1.0F});
const float hostAnimationPhase = 0.25F;
const std::array effects{
    henia::ui::EffectLayer{
        .kind = henia::ui::EffectLayerKind::Glow,
        .color = {0.15F, 0.55F, 1.0F, 0.45F},
        .amount = 5.0F,
    },
    henia::ui::EffectLayer{
        .kind = henia::ui::EffectLayerKind::AnimatedGradient,
        .color = {0.12F, 0.65F, 0.95F, 1.0F},
        .secondaryColor = {0.70F, 0.20F, 0.95F, 1.0F},
        .phase = hostAnimationPhase,
    },
    henia::ui::EffectLayer{
        .kind = henia::ui::EffectLayerKind::Outline,
        .color = {0.85F, 0.95F, 1.0F, 0.9F},
        .amount = 1.5F,
    },
};
canvas.effectRect({{40.0F, 100.0F}, {260.0F, 148.0F}}, 10.0F, effects);
canvas.arc(
    {{320.0F, 72.0F}, {392.0F, 144.0F}},
    -1.5707963F,
    4.712389F,
    {1.0F, 0.72F, 0.10F, 1.0F},
    4.0F);
canvas.line(
    {40.0F, 80.0F},
    {300.0F, 180.0F},
    {0.20F, 0.70F, 0.95F, 1.0F},
    2.0F,
    henia::ui::LineCap::Round);

const std::array path{
    henia::ui::Vec2{40.0F, 200.0F},
    henia::ui::Vec2{160.0F, 120.0F},
    henia::ui::Vec2{300.0F, 200.0F},
};
canvas.polyline(
    path,
    {0.95F, 0.55F, 0.20F, 0.7F},
    4.0F,
    false,
    henia::ui::LineCap::Square,
    henia::ui::LineJoin::Bevel);

henia::ui::RenderPacket packet = frame.finish();
```

`Frame::reserve` establishes reusable capacities and prewarms three immutable
snapshot slots by default. `CapacityPolicy::Fixed` makes recording, packet
compilation, and snapshot-slot exhaustion reject work without allocating or
throwing; the default `Grow` policy permits capacity or slot growth but still
converts allocation failure into rejected work. `begin` clears mutable builder
contents without releasing memory, and `finish` publishes a cheap `RenderPacket`
snapshot handle while placing compatible compiled instances into shared draw
batches. Source commands, compiled instances, batch density, texture-table use,
state boundaries, and full-upload bytes remain separate statistics; batching
does not claim to compress or eliminate instance data. Rejected command and
packet-overflow counts are observable through the canvas and packet statistics;
slot count, growth, and rejected builds are observable through `Frame`.
The overload taking separate command and instance capacities is useful for
fixed-capacity analytic scenes: one `StrokeRect` command compiles to at most
eight tight edge/corner instances.

`effectRect` emits enabled layers in caller order; setting `enabled=false`
removes a costly layer without changing the layer array. Animation is explicit:
the host advances `phase`, and the packed packet remains deterministic. Use
`scopedClip` as the rectangular mask and `sdfIcon` for an R8/RGBA distance-field
texture. Effects do not allocate an intermediate target or run a full-screen
post-process. Shadow/glow and the single-instance outline deliberately trade
extra fragment area for composability; opt-in packet statistics expose that
cost separately.

## Build

With Visual Studio 2022:

```powershell
cmake --preset vs2022
cmake --build --preset dev
ctest --preset dev
./out/build/vs2022/Debug/HeniaUISandbox.exe
```

The core library itself is platform-neutral and has no external dependencies. The preset is merely the repository's ready-to-use Windows configuration.

Windows builds also produce `HeniaUIVisualSandbox.exe`. It owns a normal Win32/WGL window, builds a Segoe UI alpha atlas through the optional Win32 platform target, and renders the complete interface through `HeniaUI::OpenGL` without ImGui.

`HeniaUIEffectsExample.exe` is the compact, interactive gallery for the
currently implemented 2D drawing, effects, text, clipping, blending, image,
nine-patch, and SDF paths:

```powershell
.\out\build\vs2022\Debug\HeniaUIEffectsExample.exe
```

Press Escape to close it. Add `--headless --snapshot` for an unattended
three-frame smoke run that writes `HeniaUIEffectsExample.bmp` in the current
directory.

Top-level non-sanitizer builds also produce `HeniaUIBenchmarks`. Run it with
`--verify --iterations 25 --warmup 5 --json out/benchmark.json`; the complete
scene definitions, metrics, reference baseline, and same-runner CI comparison
policy are in [docs/benchmarks.md](docs/benchmarks.md).
Windows builds additionally produce `HeniaUID3D12InstanceBenchmarks`, which
uses real GPU timestamp queries to compare upload-heap and GPU-local instance
storage across the available UMA and discrete adapters.

The interactive sandbox is hard-capped at 144 FPS even when a hybrid or virtual-display driver ignores the requested swap interval. `--help` exits without creating a GPU context; `--headless` runs exactly three validation frames, and `--snapshot` writes `HeniaUIVisualSandbox.bmp`.

The visual sandbox also draws 5,760 animated 3D boxes through one instanced draw. Camera movement and hue animation update only frame constants; the immutable box snapshot remains resident until content changes.

For use as a subproject:

```cmake
add_subdirectory(path/to/HeniaUI)
target_link_libraries(MyApplication PRIVATE HeniaUI::Core)
```

Optional Windows targets are exported as:

```cmake
target_link_libraries(MyApplication PRIVATE
    HeniaUI::Core
    HeniaUI::Gfx
    HeniaUI::Win32
    HeniaUI::OpenGL
    HeniaUI::D3D12)
```

## Rendering invariants

- Command ordering is preserved.
- Nested clips are intersected at record time; `Canvas::scopedClip()` balances
  only the entry it successfully pushed, and empty intersections make nested
  drawing a no-op.
- Compatible adjacent instances share a draw batch; source commands and
  compiled instance counts remain reported separately.
- Up to eight live textures share one batch before a new batch is opened.
- Capacity is retained between frames.
- Fixed-capacity recording and rendering paths do not allocate after initialization.
- Backend diagnostics use bounded storage and remain available after allocation failure.
- Invalid or invisible primitives do not reach the render packet.
- RTTI is not required by the library.
- OpenGL consumes an already-current context and restores the pipeline state it changes.
- D3D12 records into a host-owned command list and never waits from the frame path.
- D3D12 instance staging/default memory is split into fence-owned submission
  slots; automatic storage keeps UMA/small packets on upload memory and moves
  larger discrete-GPU packets to GPU-local memory.
- 3D box edges are shader-expanded triangle quads with fixed pixel width; no backend depends on line-width support.
- Instance content, view/time constants, CPU upload, CPU submit, and optional host-reported GPU timing are tracked separately.
- Missing host depth attachments produce an explicit depth fallback counter rather than pretending depth testing occurred.

## Retained controls

`UiDocument` retains one stable, revisioned paint segment per widget. A dirty
leaf rebuilds only its affected branch; unrelated sibling `onPaint()` output and
compiled segment data are reused in depth-first draw order. Rebuilt/reused
subtree and segment totals are observable through `UiDocumentStatistics`.
`Panel`, `Label`, `Button`, and `NumericInput` resolve unset style
properties from the owning document `Theme`. Assigning a style property creates
a stable widget-local override; calling `reset()` on it restores inheritance.
Color-only theme changes rebuild paint segments without layout, while inherited
font and control metrics invalidate measurement recursively.
`Panel`, `Label`, `Button`, `NumericInput`, `TextInput`, `Checkbox`, `Toggle`,
`Slider`, `ComboBox`, `TabBar`, `ScrollContainer`, `ListView`, `Tooltip`,
`PopupLayer`, `ColorPicker`, `KeyBindingEditor`, and `TreeView` use direct
context/function-pointer callbacks and platform-neutral input events. The Win32
adapter translates an existing host `WndProc` message stream without
subclassing or owning the window. It owns native mouse capture only for handled
pointer sequences, releases it after the last pressed button, and translates
capture loss into pointer cancellation. Hosts should forward capture, cancel,
focus-loss, IME composition, and destruction messages as described in
[docs/architecture.md](docs/architecture.md#win32-input-and-message-ownership).

Widget interaction uses stable identities. Root replacement, child removal, and
reparenting requested by callbacks are deferred until the outer dispatch ends;
recursive dispatch is rejected and counted, while immediate `compose()` remains
available to callbacks. The complete ordering contract is documented in
[docs/architecture.md](docs/architecture.md#widget-mutation-and-callback-ordering).

Client callback exceptions propagate through `handleInput`, `UiDocument::dispatch`,
and `Win32InputAdapter::handleMessage`; hosts that require a non-throwing native
message boundary should catch there. HeniaUI does not convert a throwing callback
into process termination.

The numeric control reserves fixed, separately centered decrement/value/increment regions, supports direct typing, wheel and key stepping, range clamping and precision, and does not depend on a debug UI library.

Tab and Shift+Tab traverse visible, enabled focus targets without allocating a
temporary focus list. Buttons and production controls expose keyboard
activation/navigation, while a capturing `KeyBindingEditor` may deliberately
consume Tab. Wheel input bubbles from a hovered child to its nearest scrollable
ancestor.

```cpp
#include <henia/ui/widget/controls/ListView.h>
#include <henia/ui/widget/controls/Toggle.h>

auto menu = std::make_unique<henia::ui::Panel>();
menu->emplaceChild<henia::ui::Toggle>("World overlay", true, toggleStyle);
auto& players = menu->emplaceChild<henia::ui::ListView>(playerNames, listStyle);
players.setOnSelectionChanged(onPlayerSelected);
```

`ScrollContainer` clips retained descendant segments to its viewport.
`ListView` and `TreeView` generate draw commands only for visible fixed-height
rows, so a common overlay does not need one widget or one paint call per data
item. For richer rows, `ListView::setRecycledItems()` accepts a non-owning
`VirtualListSource`: a stable-key callback, an optional variable-height
callback, a lazy widget factory, and a rebind callback. The list keeps only a
viewport-sized widget pool, reuses each physical widget for new logical keys,
and preserves selection by key across `refreshRecycledItems()` reorders.
Source callback contexts must outlive the list.

Recycled row widgets are presentation delegates: list hit testing, keyboard
focus, and selection remain on the `ListView`, preventing a reused widget's
identity from retaining input state for an old item. The optional extent table
is rebuilt only when the source changes; scrolling performs a binary search
plus work proportional to visible and overscan rows. `PopupLayer` owns content,
modal backdrop, and popup in explicit paint order; `Tooltip` visibility remains
host-controlled so hover delay does not introduce a hidden timer.

## Multilingual text and editing

The default text path remains strict UTF-8 codepoint lookup plus kerning and has
no external dependency. Ordered fallback chains can mix atlas textures inside
the existing batch table. Hosts that need Arabic, Indic, bidirectional, or
other complex shaping implement the small `TextShapingBackend` interface (for
example with HarfBuzz); minimal ASCII builds do not link that backend.

Layout and rendering are cached independently. Layout entries retain clusters,
caret stops, hit testing, and selection geometry, while render entries resolve
current atlas pages and UVs. `DynamicGlyphAtlas` adds rasterized glyphs through
stable fixed-size pages, and the optional Win32 loader can rasterize additional
GDI glyphs on demand.

```cpp
#include <henia/ui/text/TextEditor.h>
#include <henia/ui/widget/controls/TextInput.h>

henia::ui::MemoryTextClipboard clipboard;
henia::ui::TextInput input("配置", {
    .font = latinFont,
    .fallbackFonts = {cjkFont},
    .multiline = true,
});
input.setClipboard(&clipboard);
```

`TextEditorState` keeps committed UTF-8 separate from IME preedit text and
supports codepoint-safe cursor movement, selection, copy/cut/paste, and bounded
undo/redo. `TextInput` paints selection, caret, and composition underline and
handles the corresponding platform-neutral events. `Win32InputAdapter`
translates the native IME lifecycle and UTF-16 composition caret to these
events; `Win32Clipboard` provides an optional `CF_UNICODETEXT` bridge.

## 3D instance path

```cpp
#include <henia/gfx/ShapeBatch3D.h>

henia::gfx::ShapeBatch3D shapes;
shapes.replaceBoxes(boxes);       // only when object content changes
auto snapshot = shapes.snapshot();

// Every regular frame changes only this structure.
henia::gfx::ViewParameters view{
    .viewProjection = cameraMatrix,
    .viewport = {width, height},
    .timeSeconds = time,
};
renderDevice.render(snapshot, view); // OpenGL
// renderDevice.record(snapshot, view, commandList, fenceOwnedSlot); // D3D12
```

One box instance stores bounds, linear color, pixel line width, hue offset, and generic shader effects. A static unit-box edge topology is expanded in the vertex shader. Instance revisions and independent dirty ranges let a backend skip stable uploads or patch only changed spans. Published box storage is page-based: keeping an old snapshot alive and changing one box copies one 256-instance page rather than the complete batch.

Box edges are homogeneously clipped before the perspective divide, including
camera crossings and all six frustum planes. Set `ViewParameters::clipDepthRange`
to match whether the supplied matrix emits `[-w,+w]` or `[0,+w]` clip-space
depth; both OpenGL and D3D12 accept either convention. Pixel-space along/across
distances use non-perspective interpolation, and analytic butt-cap fringes keep
requested width and AA stable across strong depth gradients.

`InstanceBatch::boxes()` is a segmented immutable view with `size()`, indexed
access, and iteration. It intentionally has no `data()` member because the
snapshot is not one contiguous CPU allocation. Upload integrations that need
contiguous pieces can iterate `boxPageCount()` / `boxPage()` without allocating.

See [docs/architecture.md](docs/architecture.md) for the ownership and threading contract.

## License

HeniaUI is available under the MIT License.
