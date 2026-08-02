# Renderer benchmark methodology

`HeniaUIBenchmarks` is a platform-neutral, deterministic workload harness. It
measures the same scene sizes on every run and emits both a readable table and a
versioned JSON document suitable for CI comparison.

## Scenarios

| Scenario | Workload |
|---|---|
| `imdrawlist_cpu_tessellation` | ImDrawList-style CPU expansion of 4,096 rounded rectangles into 32-edge fans, vertices, and indices. This is an explicit model, not a linked copy of Dear ImGui. |
| `henia_many_primitives` | The same 4,096 rounded rectangles recorded as Henia draw instances and packet-compiled. |
| `analytic_2d_fragment_bounds` | A 1 px viewport diagonal plus a large rounded rectangle stroke, tracking the conservative fragment-area bound of their generated quads. |
| `retained_static_ui` | A 128-label retained widget tree returning its unchanged immutable packet. |
| `full_repaint_dynamic_ui` | One animated label color followed by the previous full-tree paint and packet-compilation path. |
| `retained_dynamic_dirty_ui` | The same animation through `UiDocument`; stable sibling ranges and compiled segments are reused. |
| `text_heavy_ui` | 160 telemetry rows / 4,270 cached glyph instances. |
| `large_3d_full_build` | Cold construction and snapshot of 32,768 3D boxes. |
| `large_3d_dirty_update` | One changed box while the previous 32,768-box immutable snapshot remains live. |
| `paged_3d_stable_snapshot_100k` | Stable publication of a 100,000-box paged snapshot. |
| `paged_3d_one_edit_100k` | One edit in 100,000 boxes while the prior snapshot remains live. |
| `paged_3d_clustered_edits_100k` | 32 adjacent edits contained by one 256-instance page. |
| `paged_3d_sparse_edits_100k` | Four distant edits in four pages, retained as four upload ranges. |

`large_3d_full_build` remains the full-replacement control. The paged scenarios
separate stable, single, clustered, and sparse publication costs and keep the
previous immutable revision live during every changed iteration.

## Metrics

Every scenario records median and p95 total CPU time plus separate layout,
paint/build, and packet-compilation medians when those phases exist. A scoped
executable-wide allocation tracker reports median allocation count, median
allocated bytes, and peak live transient bytes without modifying the library's
allocators.
`copied_box_instances` records producer-side box data copied into replacement or
COW pages, independently of directory metadata and GPU upload bytes.

Each JSON metric group carries an explicit `measurement_scope`: CPU values are
per-iteration distributions, cache counters are cumulative over measured
iterations, batching is the latest packet snapshot, GPU work is per iteration,
and memory is a resident snapshot. These labels prevent cumulative counters
from being compared as if they were frame samples.

GPU-work fields are deterministic submission facts: draw calls, submitted
instances, steady-state upload bytes, cold upload bytes, configured GPU-buffer
bytes, texture bytes, and the conservative pre-clip pixel area of generated
quads. The area is a deterministic fragment-work upper bound, not a hardware
occlusion query. The portable harness does not create a graphics
device, so `timestamp_available` is false and `timestamp_ns` is zero. A host
benchmark with timestamp queries can populate the same fields from
`RenderProfile::latestSample.gpuNanoseconds`; HeniaUI never substitutes CPU time
for a GPU timestamp. Backend cumulative and rolling profile fields are not
written into these per-iteration portable fields.

Packet-level batching diagnostics use the same literal definitions as the
console sandbox: source commands and compiled instances are never conflated;
instances beyond the first in a batch measure draw-call sharing; texture-table
utilization excludes texture-free batches; and clip, blend, and full texture
table transitions are counted separately. No batching ratio is labeled as
command or instance compression. Each JSON scenario includes a `batching`
object; `available` is false for comparison models and 3D workloads that do not
produce a 2D `RenderPacket`.

`cpu_resident_bytes` is the capacity-backed memory directly attributable to the
scene's display list, packet/instance storage, or tessellated vectors. It is
reported separately from transient allocation peaks and does not claim to be a
whole-process working-set measurement.

The JSON `draw_layout` object records command, compiled-instance, per-glyph
command/upload sizes, and the vertex-attribute count. Text-cache hits and misses
remain explicit scenario metrics. Hardware cache-miss counters and graphics
timestamps are intentionally not synthesized: the portable CI harness uses
capacity-backed resident bytes as its cache-pressure proxy and upload bytes plus
vertex-attribute count as deterministic shader-input facts. Backend hosts may
add real timestamp queries through `RenderProfile` when comparing GPU shader
time on one device.

## Reproduce locally

Visual Studio 2022 / x64 Release:

```powershell
cmake --preset vs2022
cmake --build --preset release --target HeniaUIBenchmarks
./out/build/vs2022/Release/HeniaUIBenchmarks.exe `
  --verify --iterations 25 --warmup 5 --json out/benchmark.json
```

Linux / Ninja Release:

```bash
cmake -S . -B out/bench -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DHENIAUI_BUILD_TESTS=OFF -DHENIAUI_BUILD_SANDBOX=OFF \
  -DHENIAUI_BUILD_BENCHMARKS=ON
cmake --build out/bench --target HeniaUIBenchmarks
./out/bench/HeniaUIBenchmarks \
  --verify --iterations 25 --warmup 5 --json out/benchmark.json
```

`--verify` checks architecture-level invariants that should not depend on CPU
frequency: the retained path allocates/uploads nothing on stable frames, all
4,096 primitives remain one draw, text-cache misses stay zero after prewarm,
instance upload bytes remain materially below tessellated bytes, one dirty UI
leaf rebuilds one retained segment and is faster than the paired full repaint,
the diagonal/stroke scene stays below one eighth of its former axis-aligned/full-
interior fragment bound, and a one-box 3D update advertises one instance rather
than a full GPU upload. The 100k paged cases additionally require zero stable
allocations, at most one copied page for single/clustered edits, four copied
pages for four sparse edits, and exact 64 B / 2,048 B / 256 B upload ranges.

To compare two runs captured on the same machine:

```bash
python3 tools/compare_benchmarks.py base.json candidate.json \
  --max-time-regression-percent 35 \
  --max-memory-regression-percent 5
```

The comparator rejects increased draw/upload work, allocated-byte or resident-
memory growth over the selected budget, a cache-hit-rate loss greater than one
percentage point, and CPU regressions that exceed both 35% and 10 µs. Allocation
count increases are reported but accepted when total allocated bytes stay within
the same memory budget; this permits bounded page metadata allocations while
still rejecting allocation-volume regressions.
The absolute floor prevents sub-microsecond retained lookups from failing on
timer noise.

## D3D12 instance-storage sweep

Windows non-sanitizer builds include a hardware benchmark that records GPU
timestamp queries around the instance copies and draws. It enumerates one
adapter per hardware ID and runs 64-instance (4 KiB) and 32,768-instance
(2 MiB) workloads in static and full-replacement modes with forced direct,
forced GPU-local, and automatic selection:

Its GPU/record medians are per measured frame. Copied bytes, upload-heap read
bytes, and strategy frame counts are explicitly labeled as cumulative totals
over the measured window; resident bytes are a post-window snapshot.

```powershell
cmake --build --preset release --target HeniaUID3D12InstanceBenchmarks
./out/build/vs2022/Release/HeniaUID3D12InstanceBenchmarks.exe `
  --iterations 25 --warmup 5
```

Recorded on 2026-08-02 on the same workstation, using an RTX 5060 Laptop GPU
and Intel UHD Graphics. Values are median GPU timestamp microseconds; each row
used 25 measured frames after 5 warmups:

| Adapter | Architecture | Workload | Direct upload | GPU-local | Automatic choice |
|---|---|---|---:|---:|---|
| RTX 5060 Laptop | discrete | small static | 2.30 | 1.47 | direct (below 64 KiB) |
| RTX 5060 Laptop | discrete | small dynamic | 3.78 | 2.91 | direct (below 64 KiB) |
| RTX 5060 Laptop | discrete | large static | 1,745.86 | 209.60 | GPU-local |
| RTX 5060 Laptop | discrete | large dynamic | 1,745.60 | 908.19 | GPU-local |
| Intel UHD | UMA | small static | 18.18 | 26.51 | direct |
| Intel UHD | UMA | small dynamic | 18.28 | 22.92 | direct |
| Intel UHD | UMA | large static | 11,783.39 | 11,382.29 | direct |
| Intel UHD | UMA | large dynamic | 11,429.84 | 11,145.05 | direct |

The discrete large-static result removes repeated PCIe-visible vertex reads
after warmup; the dynamic result still pays a 2 MiB copy each frame but is
faster on this adapter. UMA has no separate GPU-local memory advantage and its
forced copy path increases CPU recording cost, so the default stays direct.
The 64 KiB threshold deliberately keeps small discrete workloads direct despite
their tiny timestamp differences, avoiding extra resource/copy complexity for
microsecond-scale work. These measurements justify the default selector for
this hardware pair; the public override exists because adapter and workload
results are not universal.

## CI behavior

Pull requests run a dedicated `benchmark-regression` job. The base SHA and
candidate SHA are built in Release mode on the same GitHub runner, executed
sequentially with identical iteration counts, compared, summarized in the job
page, and uploaded as JSON artifacts. The first change that introduces the
harness bootstraps by comparing two candidate runs; subsequent pull requests use
the actual base executable. AddressSanitizer builds omit this executable because
allocator interposition and sanitizer overhead are not meaningful benchmark
inputs; correctness remains covered by the normal sanitizer test matrix.

## Initial local baseline

Recorded on 2026-08-02 with MSVC Release/x64 on a 13th Gen Intel Core
i7-13650HX, 25 measured iterations after 5 warmups:

| Scenario | Median µs | p95 µs | Median allocations | Upload KiB | CPU resident KiB | GPU buffer KiB |
|---|---:|---:|---:|---:|---:|---:|
| ImDrawList-style tessellation | 2,436.1 | 4,679.0 | 0 | 4,176.0 | 4,176.0 | 4,176.0 |
| Henia many primitives | 389.0 | 593.7 | 0 | 320.0 | 720.3 | 320.0 |
| Retained static UI | <0.1 | 0.1 | 0 | 0.0 | 322.1 | 320.0 |
| Full-repaint dynamic UI | 136.2 | 139.2 | 0 | 87.0 | 722.1 | 320.0 |
| Retained-segment dynamic UI | 14.5 | 14.6 | 0 | 87.0 | 322.1 | 320.0 |
| Text-heavy UI | 527.2 | 573.1 | 0 | 333.6 | 769.3 | 340.0 |
| Large 3D full build | 1,841.5 | 2,590.4 | 2 | 2,048.0 | 4,096.0 | 2,048.0 |
| Large 3D dirty update | 606.9 | 900.1 | 2 | 0.0625 | 4,096.0 | 2,048.0 |

These numbers are evidence for this machine, not universal pass/fail constants.
CI uses same-runner relative comparison and deterministic work/memory fields.
For the paired 128-label animation, retained subtree composition reduced median
CPU time by 89.4% while preserving the exact submitted instance workload and
performing zero steady-state allocations.

The #17 tight-geometry capture uses a 1,920 x 1,080 diagonal and a 1,720 x 880
rounded stroke. Their former axis-aligned/full-interior quads bounded 3,612,641
fragment pixels. The oriented line plus eight non-overlapping stroke regions
bound 42,763 pixels, a 98.8% reduction, while remaining one draw batch. The
Release capture compiled the two commands to nine 80-byte instances in 0.6 us
median with zero steady-state allocations.

For #19, base `0888076` and the paged candidate were built and run consecutively
on the same machine. A live-snapshot edit in 32,768 boxes fell from 610.9 us to
2.9 us (99.5%) while transient allocated bytes fell from 2,097,231 to 20,896;
the producer copied 256 boxes and the backend upload remained 64 bytes. Cold
32,768-box replacement moved from 1,942.7 us to 2,370.6 us (+22.0%, inside the
35% budget) while allocated bytes changed by only +0.26%.

At 100,000 boxes, stable snapshot publication measured 0.1 us with zero
allocations. One edit measured 6.7 us / one copied page / 64 upload bytes;
32 clustered edits measured 17.5 us / one copied page / 2,048 upload bytes;
four distant edits measured 8.5 us / four copied pages / 256 upload bytes.

## Compact 2D layout capture (#22)

Recorded on the same MSVC Release/x64 workstation on 2026-08-02, with 25
measured iterations after 5 warmups. The base and candidate executables ran
consecutively and passed `tools/compare_benchmarks.py`.

| Payload | Before | After | Reduction |
|---|---:|---:|---:|
| `DrawCommand` (all kinds, including one glyph) | 104 B | 88 B | 15.4% |
| `DrawInstance` / one glyph upload | 80 B | 60 B | 25.0% |
| Vertex attributes | 7 | 5 | 28.6% |

| Scenario | Packet compile before / after | CPU resident before / after | Upload before / after | Text cache misses | Draws |
|---|---:|---:|---:|---:|---:|
| 4,096 rounded rectangles | 235.4 / 193.3 us | 753,936 / 606,480 B | 327,680 / 245,760 B | 0 | 1 / 1 |
| Full dynamic widget repaint | 85.2 / 70.0 us | 755,840 / 608,384 B | 89,040 / 66,780 B | 32 prewarm misses | 1 / 1 |
| 4,270-glyph text UI | 257.5 / 268.8 us | 805,120 / 648,448 B | 341,600 / 256,200 B | 0 | 1 / 1 |

The deterministic improvements are a 25% upload reduction and roughly 19.5%
less capacity-backed CPU storage in both rectangle-heavy and text-heavy cases,
with unchanged instance counts and draw calls. The text compile sample moved
4.4% on this short capture while the rectangle samples improved; the comparator
accepted the full suite. OpenGL and D3D12 output tests cover the extra join/flag
decode and the removed attributes. The portable harness continues to report GPU
timestamps unavailable rather than labeling a CPU proxy as shader time.
