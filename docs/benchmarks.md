# Renderer benchmark methodology

`HeniaUIBenchmarks` is a platform-neutral, deterministic workload harness. It
measures the same scene sizes on every run and emits both a readable table and a
versioned JSON document suitable for CI comparison.

## Scenarios

| Scenario | Workload |
|---|---|
| `imdrawlist_cpu_tessellation` | ImDrawList-style CPU expansion of 4,096 rounded rectangles into 32-edge fans, vertices, and indices. This is an explicit model, not a linked copy of Dear ImGui. |
| `henia_many_primitives` | The same 4,096 rounded rectangles recorded as Henia draw instances and packet-compiled. |
| `retained_static_ui` | A 128-label retained widget tree returning its unchanged immutable packet. |
| `full_repaint_dynamic_ui` | One animated label color followed by the previous full-tree paint and packet-compilation path. |
| `retained_dynamic_dirty_ui` | The same animation through `UiDocument`; stable sibling ranges and compiled segments are reused. |
| `text_heavy_ui` | 160 telemetry rows / 4,270 cached glyph instances. |
| `large_3d_full_build` | Cold construction and snapshot of 32,768 3D boxes. |
| `large_3d_dirty_update` | One changed box while the previous 32,768-box immutable snapshot remains live. |

The final dirty-3D scenario intentionally exposes the current full-vector COW
cost even though its backend upload range is one 64-byte instance. It is the
baseline for #19 rather than an optimized target disguised as a benchmark.

## Metrics

Every scenario records median and p95 total CPU time plus separate layout,
paint/build, and packet-compilation medians when those phases exist. A scoped
executable-wide allocation tracker reports median allocation count, median
allocated bytes, and peak live transient bytes without modifying the library's
allocators.

GPU-work fields are deterministic submission facts: draw calls, submitted
instances, steady-state upload bytes, cold upload bytes, configured GPU-buffer
bytes, and texture bytes. The portable harness does not create a graphics
device, so `timestamp_available` is false and `timestamp_ns` is zero. A host
benchmark with timestamp queries can populate the same fields from
`RenderProfile::gpuNanoseconds`; HeniaUI never substitutes CPU time for a GPU
timestamp.

`cpu_resident_bytes` is the capacity-backed memory directly attributable to the
scene's display list, packet/instance storage, or tessellated vectors. It is
reported separately from transient allocation peaks and does not claim to be a
whole-process working-set measurement.

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
and a one-box 3D update advertises one instance rather than a full GPU upload.

To compare two runs captured on the same machine:

```bash
python3 tools/compare_benchmarks.py base.json candidate.json \
  --max-time-regression-percent 35 \
  --max-memory-regression-percent 5
```

The comparator rejects increased draw/upload work, increased median allocation
count, a cache-hit-rate loss greater than one percentage point, memory growth
over the selected budget, and CPU regressions that exceed both 35% and 10 µs.
The absolute floor prevents sub-microsecond retained lookups from failing on
timer noise.

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
