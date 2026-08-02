# Backend visual regression contract

`VisualRegression.h` defines a compact golden image as stable interior pixel
probes for rounded fill/stroke primitives, laid-out alpha-atlas text, a line, and clipped
content. Tests write the full actual image as PPM when a probe fails.

The same probes are used by the D3D12 WARP and desktop OpenGL output tests.
Interior RGB tolerances are 8–16 values on an 8-bit channel; clear/clipped
probes allow 2. Analytic edge pixels are excluded because derivative and sample
placement differences are expected between WARP and OpenGL drivers. A backend
whose interior output falls outside these bounds is considered visually
incompatible.

3D output uses the same generated clip-sweep scenes in the D3D12 WARP and
desktop OpenGL readback tests. For both zero-to-one and minus-one-to-one depth,
each test renders a box fully outside, crossing, and fully inside every frustum
plane. A separate perspective scene crosses the camera plane and probes the
shortened depth edges outside the surviving front face. Fully outside scenes
must remain black, intersecting scenes must retain bounded finite coverage, and
the camera-crossing probe fails if crossing edges are dropped wholesale.
