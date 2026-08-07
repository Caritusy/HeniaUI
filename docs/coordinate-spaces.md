# Coordinate spaces, DPI, and framebuffer scaling

HeniaUI keeps layout independent from the pixel density and attachment size of
the host window. The contract has three explicit spaces:

- **logical UI units** are used by widgets, `Canvas`, text metrics, hit testing,
  and every `RenderPacket`;
- **host input units** are the client coordinates delivered by the embedding
  application and are mapped through `UiCoordinateSpace::inputToLogical`;
- **framebuffer pixels** are the physical attachment dimensions and are mapped
  from logical units through `UiRenderViewport::logicalToFramebuffer`.

`dpiScale` is resource-selection metadata. It does not multiply layout or
silently alter either transform. Input and framebuffer sizes may therefore be
independent: this covers Win32 DPI virtualization, supersampled or downscaled
render targets, sub-viewports, and hosts that composite UI into another target.

## Defining a document space

For a full axis-aligned target, `makeUiCoordinateSpace` derives both transforms:

```cpp
const auto space = henia::ui::makeUiCoordinateSpace(
    {800.0F, 600.0F},  // logical layout extent
    {1000.0F, 750.0F}, // units delivered by the host input API
    1200, 900,         // physical framebuffer pixels
    1.25F);            // font/resource density metadata

if (!document.setCoordinateSpace(space)) {
    HandleInvalidCoordinateSpace();
}

const auto packet = document.compose();
renderer.render(packet, document.coordinateSpace().render);
```

This example maps input at 1.25 units per logical unit and renders at 1.5
framebuffer pixels per logical unit. A point at input `(250, 187.5)` becomes
logical `(200, 150)` and framebuffer `(300, 225)`.

`AxisAlignedTransform` permits positive non-uniform scale and translation.
Translation is useful for a UI sub-viewport. Negative, zero, or non-finite
scales are rejected. Both renderers transform geometry in the vertex shader and
transform logical clips before conservative floor/ceil framebuffer scissoring.
The old width/height renderer overloads remain identity-transform shortcuts.

Changing only DPI or either transform invalidates retained paint without
remeasuring the widget tree. Changing `logicalViewport` invalidates layout.
`UiDocumentStatistics` reports coordinate-space, DPI, input-transform, and
render-transform changes separately.

## Win32 per-monitor flow

Process/thread DPI awareness remains host-owned. A standalone application may
choose Per-Monitor V2 before creating any windows:

```cpp
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
```

An injected library must not change process awareness behind the application's
back. Instead, discover the host's awareness and the coordinate units of the
messages and render target, then describe those units explicitly. Do not assume
that `WM_*BUTTON` client coordinates, `GetClientRect`, and a swap-chain buffer
have the same extent when DPI virtualization or offscreen compositing is active.

`Win32InputAdapter` applies the document's current `inputToLogical` transform to
pointer and wheel events. It observes `WM_DPICHANGED`, stores a revisioned
`Win32DpiChange`, and invokes an optional synchronous callback. The message
still returns `false`: applying the suggested window rectangle and deciding
whether the host consumes the message remain application responsibilities.

```cpp
struct WindowDpiHost {
    HWND window{};
    henia::ui::UiDocument* document{};

    void changed(const henia::ui::Win32DpiChange& change) {
        if (change.hasSuggestedWindowRect) {
            const RECT& r = change.suggestedWindowRect;
            SetWindowPos(window, nullptr, r.left, r.top,
                r.right - r.left, r.bottom - r.top,
                SWP_NOACTIVATE | SWP_NOZORDER);
        }

        RECT client{};
        GetClientRect(window, &client);
        const float scale = static_cast<float>(change.dpiY) / 96.0F;
        const henia::ui::Vec2 physical{
            static_cast<float>(client.right - client.left),
            static_cast<float>(client.bottom - client.top)};
        document->setCoordinateSpace(henia::ui::makeUiCoordinateSpace(
            {physical.x / scale, physical.y / scale},
            physical,
            static_cast<std::uint32_t>(physical.x),
            static_cast<std::uint32_t>(physical.y),
            scale));
    }
};

WindowDpiHost dpiHost{window, &document};
input.setOnDpiChanged(
    henia::ui::Callback<const henia::ui::Win32DpiChange&>::bind<
        WindowDpiHost, &WindowDpiHost::changed>(dpiHost));
```

Update each window/document independently. Apply the new coordinate space and
font selection before the next `compose`; never reuse one window's dimensions
or DPI as global state for another swap chain.

## Font atlas density without layout churn

`Win32FontScaleCache` rasterizes bounded integer-physical-pixel variants and
returns the existing stable `FontHandle` when a window returns to a previously
visited scale/size. `TextFontRasterResolver` lets layout select that variant from
the final requested logical size. Public metrics are normalized back to logical
units, so 100%, 125%, 150%, and 200% variants preserve layout while increasing
glyph texel density. At the configured variant limit the closest resident raster
is reused. New variants must be synchronized with the active renderer before
packets reference them.

Direct `Win32FontLoader` users can set `Win32FontRequest::metricsScale` to the
physical-pixels-per-logical-unit value. Pass the same value to
`appendGlyphs`; dynamic glyph bitmap dimensions stay physical while bearings,
advances, and published logical glyph size are normalized.

The cache retains variants intentionally to avoid atlas churn during repeated
monitor moves. Hosts can prewarm known sizes and explicitly release owner-created
font/texture resources at an application-defined lifecycle boundary.

## Physical strokes and antialiasing

Ordinary line widths are logical and scale with the UI. To request a stable
physical width, convert it before recording:

```cpp
const float logicalWidth = henia::ui::logicalLineWidthForPhysicalPixels(
    1.0F,
    end - start,
    document.coordinateSpace().render.logicalToFramebuffer);
canvas.line(start, end, color, logicalWidth);
```

The helper accounts for line direction under non-uniform scaling. Analytic
coverage remains evaluated in logical space, while shader derivatives track
the actual framebuffer footprint; minimum AA width is adjusted for the render
scale so fractional DPI factors do not harden or overblur edges.
