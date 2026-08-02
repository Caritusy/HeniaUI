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
- no rebuilding of stable layout and paint data without a dirty reason;
- no per-box CPU projection, antialias expansion, vertex generation, or index generation;
- no assumption that one process has only one graphics context or swap chain.

Shapes, images, and glyphs use one ordered UI pipeline. Each batch carries a small texture table, so alternating widget backgrounds and font glyphs can remain in one draw batch while preserving paint order. A batch boundary is introduced only for a clip, blend, pipeline, or texture-table capacity change.

## Current example

```cpp
#include <henia/ui/Frame.h>

henia::ui::Frame frame;
frame.reserve(4096, 32, henia::ui::CapacityPolicy::Fixed);

auto& canvas = frame.begin();
canvas.fillRect(
    {{20.0F, 20.0F}, {420.0F, 240.0F}},
    {0.03F, 0.05F, 0.08F, 1.0F},
    12.0F);

const henia::ui::RenderPacket& packet = frame.finish();
```

`Frame::reserve` establishes reusable capacities. `CapacityPolicy::Fixed` makes
recording and packet compilation reject overflow without allocating or throwing;
the default `Grow` policy permits capacity growth but still converts allocation
failure into rejected work. `begin` clears logical contents without releasing
memory, and `finish` compiles the ordered display list while coalescing compatible
commands. Rejected command and packet-overflow counts are observable through the
canvas and packet statistics.

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

`UiDocument` retains layout and paint output until a dirty reason occurs. `Panel`, `Label`, `Button`, and `NumericInput` use direct context/function-pointer callbacks and platform-neutral input events. The Win32 adapter translates an existing host `WndProc` message stream without subclassing or owning the window.

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

One box instance stores bounds, linear color, pixel line width, hue offset, and generic shader effects. A static unit-box edge topology is expanded in the vertex shader. Instance revisions and dirty ranges let a backend skip stable uploads or update only a changed range.

See [docs/architecture.md](docs/architecture.md) for the ownership and threading contract.

## License

HeniaUI is available under the MIT License.
