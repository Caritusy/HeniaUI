# HeniaUI

HeniaUI is a compact retained UI and rendering engine for native C++ applications. It is being built around a backend-neutral display list, texture-table batching, immutable render packets, and renderer instances that can survive multiple windows, contexts, and graphics APIs.

The project is intentionally independent from ImGui and from any host application's hook, SDK, runtime, or input implementation. A host can embed the library with `add_subdirectory`, consume an installed CMake package, or build the included standalone sandbox.

> Status: usable foundation. The public API includes retained widgets, UTF-8 text, native Win32 input/font adapters, native OpenGL 3.3 and Direct3D 12 UI renderers, plus a separate `henia::gfx` instance path for large 3D box fields.

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

Shapes, images, and glyphs use one ordered UI pipeline. Each batch carries a small texture table, so alternating widget backgrounds and font glyphs can remain in one draw batch while preserving paint order. A batch boundary is introduced only for a clip, blend, pipeline, or texture-table capacity change.

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
snapshot handle while coalescing compatible commands. Rejected command and
packet-overflow counts are observable through the canvas and packet statistics;
slot count, growth, and rejected builds are observable through `Frame`.
The overload taking separate command and instance capacities is useful for
fixed-capacity analytic scenes: one `StrokeRect` command compiles to at most
eight tight edge/corner instances.

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

Top-level non-sanitizer builds also produce `HeniaUIBenchmarks`. Run it with
`--verify --iterations 25 --warmup 5 --json out/benchmark.json`; the complete
scene definitions, metrics, reference baseline, and same-runner CI comparison
policy are in [docs/benchmarks.md](docs/benchmarks.md).

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
- Nested clips are intersected at record time.
- Compatible adjacent commands are merged.
- Up to eight live textures share one batch before a new batch is opened.
- Capacity is retained between frames.
- Fixed-capacity recording and rendering paths do not allocate after initialization.
- Backend diagnostics use bounded storage and remain available after allocation failure.
- Invalid or invisible primitives do not reach the render packet.
- RTTI is not required by the library.
- OpenGL consumes an already-current context and restores the pipeline state it changes.
- D3D12 records into a host-owned command list and never waits from the frame path.
- D3D12 instance memory is split into fence-owned submission slots.
- 3D box edges are shader-expanded triangle quads with fixed pixel width; no backend depends on line-width support.
- Instance content, view/time constants, CPU upload, CPU submit, and optional host-reported GPU timing are tracked separately.
- Missing host depth attachments produce an explicit depth fallback counter rather than pretending depth testing occurred.

## Retained controls

`UiDocument` retains one stable, revisioned paint segment per widget. A dirty
leaf rebuilds only its affected branch; unrelated sibling `onPaint()` output and
compiled segment data are reused in depth-first draw order. Rebuilt/reused
subtree and segment totals are observable through `UiDocumentStatistics`.
`Panel`, `Label`, `Button`, and `NumericInput` use direct
context/function-pointer callbacks and platform-neutral input events. The Win32
adapter translates an existing host `WndProc` message stream without
subclassing or owning the window. It owns native mouse capture only for handled
pointer sequences, releases it after the last pressed button, and translates
capture loss into pointer cancellation. Hosts should forward capture, cancel,
focus-loss, and destruction messages as described in
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
depth; both OpenGL and D3D12 accept either convention.

`InstanceBatch::boxes()` is a segmented immutable view with `size()`, indexed
access, and iteration. It intentionally has no `data()` member because the
snapshot is not one contiguous CPU allocation. Upload integrations that need
contiguous pieces can iterate `boxPageCount()` / `boxPage()` without allocating.

See [docs/architecture.md](docs/architecture.md) for the ownership and threading contract.

## License

HeniaUI is available under the MIT License.
