# Color, alpha, and texture contract

HeniaUI performs every UI shading operation in linear light and submits
premultiplied-alpha fragments to the fixed-function blend stage. These rules are
part of the public API; hosts and asset loaders must not infer them from a
particular backend.

## Public colors

`henia::ui::Color` is straight alpha in linear-light space. RGB may be HDR or
outside `[0, 1]`; normal validation therefore requires finite values but does
not clamp them. Alpha participates in final premultiplication immediately before
blending. `BlendMode::PremultipliedAlpha` uses `ONE, ONE_MINUS_SRC_ALPHA`, while
`BlendMode::Additive` uses `ONE, ONE`.

The helpers in `henia/ui/Types.h` make boundary conversions explicit:

- `srgbToLinear(float/Color)` and `linearToSrgb(float/Color)` implement the
  standard normalized sRGB transfer curve and preserve `Color::alpha`;
- `straightToPremultiplied(Color)` and `premultipliedToStraight(Color)` convert
  alpha representation in the current color space. Unpremultiplying zero alpha
  returns transparent black.

The transfer helpers clamp normalized channels to `[0, 1]`; do not use them for
HDR transport. Decode display-authored colors before passing them to Canvas:

```cpp
const henia::ui::Color button = henia::ui::srgbToLinear(
    henia::ui::Color{0.20F, 0.55F, 0.90F, 0.80F});
canvas.fillRect(bounds, button);
```

## Texture metadata

Every `TextureStore` entry has resolved `TextureAlphaMode` and
`TextureColorSpace` metadata. `TextureAlphaMode::FormatDefault` is accepted only
as a creation convenience: it resolves to `AlphaMask` for `Alpha8` and
`Straight` for `Rgba8`; `TextureView` never returns `FormatDefault`.

| Format | Alpha mode | Color space | Meaning |
| --- | --- | --- | --- |
| `Alpha8` | `AlphaMask` | `Linear` | Scalar coverage/distance data; never color-transfer encoded |
| `Rgba8` | `Straight` | `Linear` or `Srgb` | RGB is independent of alpha |
| `Rgba8` | `Premultiplied` | `Linear` or `Srgb` | Decoded RGB has already been multiplied by alpha |
| `Rgba8` | `Opaque` | `Linear` or `Srgb` | Stored alpha is ignored and sampling forces alpha to one |

`Alpha8` with sRGB or a non-mask alpha mode, and `Rgba8` with `AlphaMask`, are
rejected at creation. For an sRGB premultiplied texture, stored RGB must be the
sRGB encoding of premultiplied *linear* RGB. Premultiplying already encoded sRGB
channels is a different operation and is not accepted as equivalent.

```cpp
henia::ui::TextureCreateOptions options{
    .alphaMode = henia::ui::TextureAlphaMode::Premultiplied,
    .colorSpace = henia::ui::TextureColorSpace::Srgb,
};
auto image = textures.create(
    henia::ui::TextureFormat::Rgba8,
    width,
    height,
    rowPitch,
    pixels,
    std::move(options));
```

RGBA image and nine-patch primitives accept `Straight`, `Premultiplied`, and
`Opaque` resources. Glyph primitives require `Alpha8`/`AlphaMask`; this keeps
font coverage separate from RGBA image semantics. SDF icons read linear scalar
data from the red channel and reject sRGB or premultiplied resources. Backends
reject an incompatible packet before issuing a draw.

Linear filtering happens before alpha normalization. A premultiplied sample is
converted to straight form in the shader and premultiplied exactly once after
tint and analytic coverage. This preserves translucent colored edges without
the dark fringe caused by double premultiplication. Texture tint is a
straight-alpha, linear `Color`, just like every other Canvas color.

## Backend texture formats

CPU bytes are uploaded unchanged. Transfer decoding is expressed by native
resource/view formats:

| Texture metadata | OpenGL storage | D3D12 resource/SRV |
| --- | --- | --- |
| `Alpha8`, linear mask | `GL_R8` | `DXGI_FORMAT_R8_UNORM` |
| `Rgba8`, linear | `GL_RGBA8` | `DXGI_FORMAT_R8G8B8A8_UNORM` |
| `Rgba8`, sRGB | `GL_SRGB8_ALPHA8` | `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB` |

External GPU textures must have the exact dimensions and native format implied
by their `TextureStore` metadata. OpenGL validates level-zero internal storage;
D3D12 validates the resource description and creates the matching SRV. Alpha
mode is shader metadata and does not alter native storage.

## Render targets

Shader output is always premultiplied linear light. The host declares how the
bound render target stores that output:

- `RenderTargetColorSpace::Linear` stores linear values directly;
- `RenderTargetColorSpace::Srgb` asks the fixed-function output stage to encode
  linear RGB with the sRGB transfer curve. Alpha remains linear.

`OpenGlRenderer::render(..., targetColorSpace)` disables or enables
`GL_FRAMEBUFFER_SRGB` for the draw. The default `Preserve` state policy restores
the host's previous value; opt-in `DedicatedContext` deliberately leaves the
renderer-established state in place. The host must bind an attachment whose
internal format supports the declared mode.

`D3D12RendererConfiguration::targetColorSpace` must match the RT format passed
to `initialize()`. A linear configuration uses an `*_UNORM` RT format; an sRGB
configuration uses its `*_UNORM_SRGB` peer. Mismatches are rejected during
initialization. The host must bind an RTV with that exact configured format.

Compositing onto an HDR/linear swap-chain intermediate should use `Linear` and
leave display transfer/tone mapping to the host's final presentation pass.

## Regression coverage

The shared OpenGL/D3D12 texture golden renders the same translucent red edge
from straight and premultiplied storage, an sRGB sample into a linear target,
an opaque-alpha override, additive images, and a tinted translucent image. The
main UI golden separately covers glyph alpha masks. Both native backends consume
the same probes and tolerances.
