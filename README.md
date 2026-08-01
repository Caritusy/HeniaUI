# HeniaUI

HeniaUI is a compact retained UI and rendering engine for native C++ applications. It is being built around a backend-neutral display list, texture-table batching, immutable render packets, and renderer instances that can survive multiple windows, contexts, and graphics APIs.

The project is intentionally independent from ImGui and from any host application's hook, SDK, runtime, or input implementation. A host can embed the library with `add_subdirectory`, consume an installed CMake package, or build the included standalone sandbox.

> Status: active engine development. The current public API records analytic shapes, images, lines, and cached UTF-8 text runs, compiles them into ordered GPU-ready batches, and submits them through native OpenGL 3.3 or Direct3D 12 renderers. Retained widgets and host adapters are the next milestone.

## Why another UI renderer?

HeniaUI is designed to avoid several common scaling traps in game overlays and native tools:

- no draw call per primitive or glyph;
- no dependency on an immediate-mode debug UI in production builds;
- no GPU-buffer destruction or growth from inside a presentation callback;
- no render-thread locks for routine UI updates;
- no rebuilding of stable layout and paint data without a dirty reason;
- no assumption that one process has only one graphics context or swap chain.

Shapes, images, and glyphs use one ordered UI pipeline. Each batch carries a small texture table, so alternating widget backgrounds and font glyphs can remain in one draw batch while preserving paint order. A batch boundary is introduced only for a clip, blend, pipeline, or texture-table capacity change.

## Current example

```cpp
#include <henia/ui/Frame.h>

henia::ui::Frame frame;
frame.reserve(4096, 32);

auto& canvas = frame.begin();
canvas.fillRect(
    {{20.0F, 20.0F}, {420.0F, 240.0F}},
    {0.03F, 0.05F, 0.08F, 1.0F},
    12.0F);

const henia::ui::RenderPacket& packet = frame.finish();
```

`Frame::reserve` establishes reusable capacities. `begin` clears logical contents without releasing memory, and `finish` compiles the ordered display list while coalescing compatible commands.

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

For use as a subproject:

```cmake
add_subdirectory(path/to/HeniaUI)
target_link_libraries(MyApplication PRIVATE HeniaUI::Core)
```

Optional Windows targets are exported as:

```cmake
target_link_libraries(MyApplication PRIVATE
    HeniaUI::Core
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
- Invalid or invisible primitives do not reach the render packet.
- RTTI is not required by the library.
- OpenGL consumes an already-current context and restores the pipeline state it changes.
- D3D12 records into a host-owned command list and never waits from the frame path.
- D3D12 instance memory is split into fence-owned submission slots.

## Roadmap

1. Retained nodes, dirty layout/paint propagation, and input routing.
2. Reusable SaaS-oriented controls and theme tokens.
3. Host adapters for multi-context OpenGL and multi-surface Direct3D 12 applications.
4. Optional debugging inspectors kept outside production targets.

## License

HeniaUI is available under the MIT License.
