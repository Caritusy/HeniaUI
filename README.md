# HeniaUI

<p align="center">
  <strong>Retained UI and GPU-instanced rendering for native C++23 applications.</strong>
</p>

<p align="center">
  Backend-neutral core · host-owned graphics · predictable frame costs · no ImGui dependency
</p>

<p align="center">
  <a href="https://github.com/Caritusy/HeniaUI/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/Caritusy/HeniaUI/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/Caritusy/HeniaUI/releases/tag/v0.1.0"><img alt="Release v0.1.0" src="https://img.shields.io/badge/release-v0.1.0-2ea44f.svg"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/badge/license-MIT-blue.svg"></a>
  <img alt="C++23" src="https://img.shields.io/badge/C%2B%2B-23-00599C.svg">
</p>

<p align="center">
  <a href="#is-heniaui-a-fit">Fit</a> ·
  <a href="#quick-start">Quick Start</a> ·
  <a href="#choose-a-backend">Backends</a> ·
  <a href="#integration-contract">Integration</a> ·
  <a href="#testing-and-quality">Testing</a> ·
  <a href="#documentation">Documentation</a>
</p>

<p align="center">
  <img src="docs/media/widget-gallery.png" alt="HeniaUI retained widget gallery" width="100%">
</p>

HeniaUI is an independent C++23 library for native tools, editors, launchers,
overlays, and real-time visualization. A retained widget tree or a low-level
`Canvas` display list is compiled into an immutable `RenderPacket`; generic 3D
boxes are published as an immutable `InstanceBatch`. The host submits those
snapshots through an already-owned graphics context or command list.

The library does not create or own a window, swap chain, back buffer, OpenGL
context, D3D device, command queue, allocator, fence, or presentation loop. It
does not install hooks, require RTTI, or depend on a host application's input
implementation. The platform-neutral `Core` and `Gfx` targets depend only on
the C++ standard library.

> **Release status:** `v0.1.0` is the first tracked release. HeniaUI is still
> pre-1.0: the ownership and rendering contracts are tested, while public APIs
> may continue to evolve before 1.0.

## Is HeniaUI a fit?

Use HeniaUI when the application needs:

- a retained widget tree with local invalidation, keyboard focus, pointer
  capture, scrolling, clipboard, IME composition, and UTF-8 editing;
- a small immediate recording surface for custom controls, diagrams, and
  analytic 2D primitives;
- immutable producer-to-render snapshots that can be published between threads;
- high-volume GPU-instanced boxes whose object content is stable while camera,
  viewport, and animation constants change;
- an OpenGL 3.3 renderer or a unified DirectX entry point that probes D3D12 and
  falls back to D3D11/WARP on Windows.

HeniaUI is not a windowing toolkit, scene graph, text-shaping suite, or complete
engine. The host supplies the event loop, native resources, synchronization,
complex text shaping integration, and presentation policy.

## Capabilities at a glance

| Layer | What it provides |
| --- | --- |
| `Core` | Retained widgets, `UiDocument`, `Canvas`, display lists, immutable packets, textures, UTF-8 text and input state |
| `Gfx` | Immutable instance batches, visibility policy, generic 3D box data, depth state, producer statistics |
| `Win32` | Host `WndProc` translation, clipboard/IME, GDI font loading, bounded DirectWrite fallback rasterization |
| `OpenGL` | Host-context UI renderer and 3D device for compatible OpenGL 3.3+ contexts |
| `DirectX` | One public Windows backend name with D3D12-first probing and D3D11 rendering fallback |
| `D3D12` | The existing explicit D3D12 renderer/device target for integrations that choose D3D12 directly |

The 2D and 3D paths share packet ownership and host contracts, but keep their
own instance formats and batching rules. This lets each path stay small and
avoids forcing UI primitives into a 3D vertex representation.

## Demos

The Windows sandbox programs are built with `HENIAUI_BUILD_SANDBOX=ON`:

| Program | Shows |
| --- | --- |
| `HeniaUIWidgetGallery` | Retained controls, text/numeric editing, lists, trees, scrolling, selection, tooltips, and popups |
| `HeniaUIEffectsExample` | Analytic shapes, gradients, tint, glow, shadows, borders, lines, nine-patches, masks, and ordered effects |
| `HeniaUIVisualSandbox` | Retained controls alongside 5,760 animated GPU-instanced boxes |

```powershell
.\out\build\vs2022\Release\HeniaUIWidgetGallery.exe
.\out\build\vs2022\Release\HeniaUIEffectsExample.exe
.\out\build\vs2022\Release\HeniaUIVisualSandbox.exe
```

For visual review without an interactive window, the gallery supports
`--headless --snapshot` and writes a bitmap in the current directory.

<p align="center">
  <img src="docs/media/effects-gallery.png" alt="HeniaUI analytic effects gallery" width="100%">
</p>

<p align="center">
  <img src="docs/media/gpu-instance-field.png" alt="HeniaUI GPU-instanced 3D box field" width="100%">
</p>

## Quick Start

### Build the repository

The checked-in Visual Studio 2022 preset builds libraries, tests, examples, and
benchmarks on Windows:

```powershell
cmake --preset vs2022
cmake --build --preset dev --parallel
ctest --preset dev

cmake --build --preset release --parallel
ctest --preset release
```

The portable smoke executable exercises recording, batching, retained packet
reuse, and statistics without opening a window:

```powershell
.\out\build\vs2022\Release\HeniaUISandbox.exe
```

For a minimal single-configuration build, disable the optional programs:

```bash
cmake -S . -B out/build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DHENIAUI_BUILD_TESTS=OFF \
  -DHENIAUI_BUILD_SANDBOX=OFF
cmake --build out/build/release --parallel
```

### Record a packet

This is a complete producer-side example. A backend consumes the resulting
immutable packet later; HeniaUI does not choose when or where to present it.

```cpp
#include <henia/ui/Frame.h>

henia::ui::Frame frame;
frame.reserve(4096, 32, henia::ui::CapacityPolicy::Fixed);

henia::ui::Canvas& canvas = frame.begin();
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

henia::ui::RenderPacket packet = frame.finish();
if (!frame.lastBuildPublished()) {
    // Fixed-capacity recording rejected the frame; keep the last good packet.
}
```

`Frame::reserve` prewarms command, instance, batch, and snapshot storage. Fixed
capacity rejects overflow deterministically and never publishes a partial
packet. `CapacityPolicy::Grow` permits retained storage to expand and reports
growth through the packet statistics.

### Add HeniaUI to an application

As a subproject, link only the layers used by the host:

```cmake
add_subdirectory(external/HeniaUI)

target_link_libraries(MyApplication PRIVATE
    HeniaUI::Core
    HeniaUI::Gfx
    HeniaUI::Win32       # Windows input/font adapter, when needed
    HeniaUI::DirectX     # or HeniaUI::OpenGL on Windows
)
```

When HeniaUI is included as a subproject, tests, sandboxes, and benchmarks are
off by default. A packaged install uses the same names:

```powershell
cmake --install out\build\vs2022 --config Release --prefix out\install
```

```cmake
find_package(HeniaUI 0.1 CONFIG REQUIRED)
target_link_libraries(MyApplication PRIVATE HeniaUI::Core HeniaUI::DirectX)
```

### Targets and options

| Target | Availability | Responsibility |
| --- | --- | --- |
| `HeniaUI::Core` | All platforms | Retained UI, display lists, text, textures, widgets |
| `HeniaUI::Gfx` | All platforms | Generic 3D instance data and producer-side shape batches |
| `HeniaUI::Win32` | Windows | Font, clipboard, input, capture, and IME adapters |
| `HeniaUI::OpenGL` | Windows | OpenGL 3.3+ UI renderer and 3D render device |
| `HeniaUI::DirectX` | Windows | Unified D3D12/D3D11 UI and 3D renderers |
| `HeniaUI::D3D12` | Windows | Explicit D3D12 UI renderer and 3D render device |

Important CMake options are `HENIAUI_BUILD_TESTS`,
`HENIAUI_BUILD_SANDBOX`, `HENIAUI_BUILD_BENCHMARKS`,
`HENIAUI_ENABLE_ADDRESS_SANITIZER`,
`HENIAUI_ENABLE_UNDEFINED_SANITIZER`, and
`HENIAUI_D3D12_RUNTIME_SHADER_COMPILATION` (Debug-only).

## Choose a backend

### OpenGL

Link `HeniaUI::OpenGL` and pass an already-current compatible OpenGL 3.3+
context to the renderer. The default state policy captures and restores the
state HeniaUI changes. `DedicatedContext` is available when the host owns a
separate context and wants to avoid that capture/restore walk. The host remains
responsible for making the context current, selecting the framebuffer, and
presenting.

### Unified DirectX

New Windows integrations should link `HeniaUI::DirectX` and include headers
under `henia/*/backend/directx`. Probe before creating the long-lived host
device:

```cpp
#include <henia/backend/directx/DirectXBackend.h>

const auto capabilities = henia::backend::directx::probe();
switch (capabilities.selected) {
case henia::backend::directx::Api::D3D12:
    // Create the host-owned D3D12 device, queue, command list, and fences.
    break;
case henia::backend::directx::Api::D3D11:
    // Create the host-owned D3D11 device and immediate context.
    break;
default:
    // Report capabilities.diagnostic and keep GPU rendering disabled.
    break;
}
```

`probe()` tests hardware adapters in deterministic order, tries D3D12 feature
level 11_0 first, then D3D11 feature level 11, and optionally uses WARP as a
compatibility path. It creates only temporary probe devices. The result
contains both availability flags, selected API, feature levels, adapter type,
HRESULTs, and a bounded diagnostic.

`henia::ui::DirectXRenderer` and
`henia::gfx::DirectXRenderDevice` expose D3D11 and D3D12 initialization paths.
The selected API is fixed for the lifetime of an initialized object; shut it
down before recreating it against a different host device. D3D12 records into
the host's open direct command list. D3D11 renders through the host's immediate
context and the currently bound render/depth targets.

The legacy `HeniaUI::D3D12` target remains available for applications that want
to select D3D12 explicitly. Its fence-owned upload and command-list contract is
unchanged.

## Integration contract

HeniaUI owns CPU-side widget trees, immutable snapshots, and the GPU resources
it creates for its renderer. The host owns native lifecycle and synchronization.

- **Producer and render threads:** compose or build a packet on one producer;
  publish the `RenderPacket` with the host's normal synchronization; consume it
  read-only on the render thread. A packet's spans remain valid while its handle
  is alive.
- **OpenGL:** keep the initialization context current, bind the intended target,
  and restore any host state needed after rendering. Resources belong to the
  sharing group of that context.
- **D3D12:** bind render/depth targets and perform resource transitions before
  `record()`. Submit the host command list and associate the submission slot
  with a host fence before that slot is reused. HeniaUI cannot query and restore
  arbitrary D3D12 state, so rebind the host state before later draws.
- **D3D11:** bind compatible render/depth targets before `render()`. The
  immediate context state is overwritten and not restored; rebind the host
  pipeline before later draws.
- **Depth:** if a requested depth attachment is unavailable, the renderer uses
  an explicit depth-disabled fallback and increments the depth fallback counter.

Resize, device recreation, context loss, texture ownership, and shutdown rules
are collected in [Resource lifetime](docs/resource-lifetime.md).

## Usage model

### Retained widgets

`UiDocument` owns a widget tree and a `TextPainter` supplied by the host. Built-in
controls include `Panel`, `Label`, `Button`, `NumericInput`, `TextInput`,
`Checkbox`, `Toggle`, `Slider`, `ComboBox`, `TabBar`, `ScrollContainer`,
`ListView`, `TreeView`, `Tooltip`, `PopupLayer`, `ColorPicker`, `ColorPanel`,
and `KeyBindingEditor`.

Each widget has independent layout and paint revisions. A local change rebuilds
only the affected retained branch; stable siblings remain in depth-first paint
order and reuse their compiled instances. Call `setViewport()`, update the
explicit coordinate space, dispatch host input, then call `compose()` once per
producer frame.

### Canvas and effects

`Canvas` records rectangles, circles, ellipses, arcs, capsules, lines and
polylines, gradients, images, glyphs, nine-patches, SDF icons, clips, and
ordered tint/glow/shadow/outline layers. The batch compiler keeps one ordered
stream, validates input, and groups compatible adjacent work with a table of up
to eight textures per batch. Effects do not require an intermediate render
target or a full-screen pass.

### Text and fonts

The default shaper provides strict UTF-8 lookup, same-face kerning, locale-aware
fallback, bounded layout/render caches, caret geometry, selection, and editing.
`Win32AsyncFontSet` adds bounded DirectWrite rasterization for Latin, CJK, kana,
Hangul, symbols, and monochrome Alpha8 emoji fallback. The owner thread commits
completed glyphs, synchronizes textures, calls `UiDocument::invalidateTypography`,
and composes again. Arabic, Indic, bidirectional text, ligatures, variation
sequences, and other multi-codepoint shaping require a host
`TextShapingBackend`.

### GPU-instanced 3D

`ShapeBatch3D` stores 64-byte `BoxInstance` records and publishes an
`InstanceBatch` snapshot. The GPU expands fixed faces and edges in the vertex
shader, so fill-only, outline-only, and combined boxes stay on the instanced
path. Replace or update boxes only when object content changes; camera,
viewport, time, and motion constants live in `ViewParameters` and do not force
content rebuilds. Optional CPU visibility filtering reuses immutable page bounds.

```cpp
henia::gfx::ShapeBatch3D shapes;
henia::gfx::BoxInstance box;
box.setFillOpacity(0.25F);
box.setOutlineEnabled(true);
shapes.addBox(box);                 // object-content change
const auto snapshot = shapes.snapshot();

henia::gfx::ViewParameters view;
view.viewport = {width, height};
view.timeSeconds = timeSeconds;
// A backend consumes snapshot and view using the host-owned device.
```

## Performance model

The library exposes observable counters instead of hiding frame costs:

- retained layout, paint, and composition reuse;
- immutable packet identity and revision reuse;
- source commands, instances, batches, texture-table boundaries, and full cold
  upload bytes;
- producer build, CPU upload, CPU submit/record, draw, rejection, and depth
  fallback statistics;
- optional conservative fragment-area and effect-variant estimates;
- 3D content revisions, dirty ranges, visibility rejection, and instance draw
  counts.

Routine submission does not dispatch an event bus, wait on a lock, allocate per
primitive, or grow a resource container. Fixed-capacity callers can turn
allocation and overflow into deterministic rejection counters. See
[Benchmarks](docs/benchmarks.md) before comparing results across machines.

## Testing and quality

The local Windows matrix is:

```powershell
cmake --preset vs2022
cmake --build --preset dev --parallel
ctest --preset dev
cmake --build --preset release --parallel
ctest --preset release
```

CI additionally covers Windows Debug/Release and Linux builds, MSVC and Clang
AddressSanitizer, Clang UBSan, zero-static-TLS checks, D3D12 debug-layer and
GPU-validation paths, shader reproducibility, installed-package consumption,
OpenGL output validation, DirectX WARP output tests, and benchmark regression
gates. The DirectX tests lock D3D12 priority, D3D11 fallback, unavailable
selection, and both unified renderer paths independently of the current GPU.

Examples and CLI-only checks:

```powershell
.\out\build\vs2022\Release\HeniaUIEffectsExample.exe --help
.\out\build\vs2022\Release\HeniaUIWidgetGallery.exe --help
```

The visual programs can also render a headless snapshot for controlled manual
inspection; do not start the interactive sandbox from a build script.

## Documentation

| Guide | Contents |
| --- | --- |
| [Architecture](docs/architecture.md) | Retained composition, immutable publication, threading, input, renderer ownership |
| [Coordinate spaces and DPI](docs/coordinate-spaces.md) | Logical input, layout units, framebuffer transforms, Per-Monitor V2 integration |
| [Color and texture contract](docs/color-and-texture-contract.md) | Alpha modes, linear/sRGB transfer, texture synchronization |
| [Unified DirectX integration](docs/directx-integration.md) | D3D12 probing, D3D11 fallback, ownership, state behavior, compatibility tests |
| [D3D12 command-list integration](docs/d3d12-integration.md) | Formats, descriptors, shader packages, pipeline libraries, fences |
| [Resource lifetime](docs/resource-lifetime.md) | Context/device recreation, external resources, shutdown, abandon, fence ownership |
| [3D visibility](docs/3d-visibility.md) | Frustum, mask, size filtering, and indirect submission |
| [Benchmarks](docs/benchmarks.md) | Fixed scenes, metrics, baselines, and regression policy |

## Support and contributions

Open a [GitHub Issue](https://github.com/Caritusy/HeniaUI/issues) for a bug,
feature request, or roadmap question. Include the backend, OS, build
configuration, host ownership boundary, and the smallest input or packet that
reproduces the behavior.

Changes should preserve standalone `add_subdirectory` and installed-package
usage, backend-neutral semantics, immutable snapshot contracts, host ownership,
warning-clean C++23 builds, and the zero-TLS policy. Add focused tests for
behavior and performance contracts.

## License

HeniaUI is available under the [MIT License](LICENSE).
