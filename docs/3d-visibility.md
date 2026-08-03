# 3D visibility and indirect submission

Large `InstanceBatch` values can opt into a reusable visibility stage without
changing the default one-upload/one-draw path. The policy is explicit:

| Mode | Behavior |
|---|---|
| `VisibilityMode::Direct` | Default. The backend validates and submits the immutable source batch directly. |
| `VisibilityMode::CpuFrustum` | Builds a stable-order compact instance stream using application masks, coarse page bounds, frustum tests, and optional projected-size filtering. |
| `VisibilityMode::Automatic` | Uses the CPU path at or above `automaticThreshold`, which defaults to 32,768 source boxes; smaller batches stay direct. |

`Automatic` is still an opt-in mode. There is no visibility-distribution test
that can be free, so HeniaUI does not silently turn culling on for every batch.
Hosts should tune the threshold with their own camera distribution and adapter.

## Use

```cpp
#include <henia/gfx/VisibilityList.h>

henia::gfx::VisibilityOptions visibility{
    .mode = henia::gfx::VisibilityMode::Automatic,
    .applicationVisibilityMask = currentLayerMask,
    .minimumProjectedExtentPixels = 1.0F,
    .automaticThreshold = 32768,
};

// OpenGL keeps its normal instanced draw after CPU compaction.
glRenderer.render(snapshot, view, hasDepthAttachment, visibility);

// D3D12 consumes the compact count through ExecuteIndirect.
d3dRenderer.record(snapshot, view, commandList, slot, visibility, reuse);
```

Application visibility groups use the existing 64-byte `BoxInstance` tail:

```cpp
box.setVisibilityMask(GameplayLayer | DebugLayer);
box.clearVisibilityMask(); // restores the all-visible compatibility behavior
```

The marker stored beside the mask distinguishes the new contract from old or
zero-initialized reserved bytes. Consequently batches produced against older
headers remain visible. A box is retained when its mask intersects
`applicationVisibilityMask`; a zero application mask hides every box.

`minimumProjectedExtentPixels == 0` disables size filtering. A positive value
rejects a box only when all eight corners have finite, safely positive clip
`w` and the largest projected AABB extent is smaller than the threshold. Boxes
crossing the eye or near plane are retained conservatively.

## CPU visibility contract

`VisibilityList::reserve()` preallocates visible indices, compact instances,
and one coarse bound per 256-instance source page. `update()` then allocates
nothing. Visible indices are ascending source indices and copied instances
preserve color, line width, hue offset, effect flags, and reserved data.

Page AABBs and their visibility-mask unions are rebuilt only for immutable
dirty pages. Camera, viewport, clip-depth convention, application mask, and
projected-size changes reuse those bounds. Animation time is not part of the
visibility key. Repeating the same visibility inputs reuses the complete compact
result and its revision.

Frustum rejection extracts the six canonical homogeneous planes once per update
and tests each AABB's maximum-distance support point. X/Y planes include a
conservative pixel margin derived from the largest line width and clip-`w`
extent in the bound, so a thick antialiased edge just outside a plane is not
discarded. Non-finite distances are retained rather than over-culled.

The compact stream has an identity namespace distinct from `InstanceBatch` and
a revision that changes only when visibility is recomputed. This lets upload
caches distinguish direct and compact data while keeping camera changes separate
from immutable content revisions.

## Backend behavior

OpenGL is the portable fallback. A changed compact stream is copied into the
existing fence-owned upload ring and submitted with the normal
`glDrawArraysInstanced` call. It requires no compute or indirect-draw extension;
the default direct path and partial immutable uploads are unchanged.

D3D12 gives every fence-owned submission slot a persistently mapped
`D3D12_DRAW_ARGUMENTS` upload resource. After slot-reuse validation, the CPU
visibility result writes one argument and the command list records
`ExecuteIndirect`. The same slot fence protects the argument resource, staging
instances, and optional GPU-local instances. Direct mode still records
`DrawInstanced`, and immutable dirty-range uploads remain available only on that
direct path. The indirect boundary can later accept compute-produced arguments;
the current implementation keeps compaction on the CPU and does not add UAV
resources or compute-queue ownership to the host contract.

Both statistics structures expose direct/culling frame counts, source and
rejected instance totals, chunk tests/rejections, result reuse, and CPU culling
nanoseconds. D3D12 additionally exposes indirect argument updates and indirect
draw calls. `submittedInstances` always reports the count that reaches the draw.

## Measured cost and threshold

The portable Release benchmark alternates two camera matrices over an immutable
batch with 75% spatially clustered offscreen boxes. It includes the validation
work that both backend paths perform and verifies zero steady-state allocations.
The 2026-08-03 local 25-iteration run produced:

| Source boxes | Direct CPU median | CPU-cull median | Submitted | Upload model |
|---:|---:|---:|---:|---:|
| 2,048 | 57.9 us | 87.8 us | 512 | 128 KiB -> 32 KiB |
| 32,768 | 944.8 us | 1,506.7 us | 8,192 | 2,048 KiB -> 512 KiB |

The CPU premium was about 30 us and 562 us respectively on that run.
Whether this breaks even depends on vertex cost, upload-memory bandwidth,
adapter architecture, and the actual rejected fraction; the portable harness
does not invent a GPU timestamp. These measurements keep `Direct` as the
library default and place the opt-in `Automatic` starting point at 32,768.
Backend `RenderProfile` GPU timestamps and the visibility statistics provide the
data needed to tune a host-specific threshold.
