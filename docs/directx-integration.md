# Unified DirectX integration

Windows consumers should link `HeniaUI::DirectX` and include the headers under
`henia/*/backend/directx`. The existing `HeniaUI::D3D12` target and headers stay
available for source compatibility, but new integrations can keep DirectX as
the only backend name at their application boundary.

## Capability selection

Call `henia::backend::directx::probe()` before creating the host render device.
The probe enumerates hardware adapters first, tests D3D12 feature level 11_0,
then tests the D3D11 feature-level 11 path. When `allowSoftware` is true, WARP
is the final compatibility fallback. The returned `selected` value is therefore
deterministic:

1. `Api::D3D12` when a D3D12 device can be created;
2. `Api::D3D11` when D3D12 is unavailable but D3D11 feature level 11 is usable;
3. `Api::None` when neither path can create a device.

The temporary probe devices and contexts are released before `probe()` returns.
The result also reports both availability flags, feature levels, HRESULT values,
whether a software adapter participated, and a bounded diagnostic. A host can
disable WARP by passing `{.allowSoftware = false}`.

```cpp
const auto capabilities = henia::backend::directx::probe();
switch (capabilities.selected) {
case henia::backend::directx::Api::D3D12:
    // Create and retain the application's D3D12 device and submission objects.
    break;
case henia::backend::directx::Api::D3D11:
    // Create and retain the application's D3D11 device and immediate context.
    break;
default:
    // Report capabilities.diagnostic and do not start GPU rendering.
    break;
}
```

## Unified renderers

`henia::ui::DirectXRenderer` and `henia::gfx::DirectXRenderDevice` expose
overloaded `initialize()` functions for host-owned D3D11 and D3D12 devices.
`backend()` reports the concrete path. An initialized object cannot change APIs;
call `shutdown()` before recreating it for a different device or backend.

The D3D12 overloads delegate to the existing renderers without changing their
submission-slot, descriptor, texture-upload, or fence contracts. UI uses
`record()` with an open host `DIRECT` command list. Gfx does the same and keeps
the existing optional CPU-visibility path.

The D3D11 overloads retain the host device and immediate context. UI and gfx use
`render()` and draw into the render/depth targets currently bound by the host.
They compile the shared HLSL source to SM5.0 during initialization, allocate
only private shader/state/instance resources, and submit instanced draws. The
D3D11 configurations declare the exact RT/DS formats, sample count, and sample
quality; initialization validates those capabilities before creating pipeline
state. UI textures created from `TextureStore` are synchronized by
`synchronizeTextures(TextureStore&)`. External D3D11 textures use an
`ExternalGpu` store entry and an explicitly supplied `ID3D11ShaderResourceView`.

## Ownership and state

Neither path creates or owns a window, swap chain, back buffer, render target,
command queue, command allocator, fence, or presentation loop. The capability
probe is the only operation that creates temporary devices.

D3D12 state behavior is documented in [D3D12 command-list integration](d3d12-integration.md).
The D3D11 path overwrites and does not restore:

- IA input layout, topology, index buffer for gfx, and vertex-buffer slot 0;
- VS/PS programs and constant-buffer slot 0;
- UI PS constant-buffer slot 1, SRV slots 0-7, and sampler slot 0;
- rasterizer state, viewport 0, and scissor rectangle 0;
- blend state and depth-stencil state.

The host must bind the intended RT/DS attachments before `render()` and rebind
its complete graphics state before any later host draw. `render()` does not
query attachment formats. They must match the formats supplied at
initialization. When gfx requests depth but the configuration or the explicit
`depthAttachmentAvailable` argument says depth is unavailable, D3D11 selects a
depth-disabled state and increments `depthFallbacks`.

## Compatibility tests

`HeniaUI.DirectX` creates D3D11 WARP targets, renders retained UI and an
instanced filled box, copies both targets to staging textures, and checks output
pixels and statistics. It also initializes both unified renderers against a
D3D12 WARP device. Synthetic selector cases lock D3D12 priority, D3D11 fallback,
and the unavailable result independently from the current machine.
