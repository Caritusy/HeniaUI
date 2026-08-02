#pragma once

#include "henia/gfx/Math.h"
#include "henia/gfx/ShapeBatch3D.h"
#include "henia/ui/Frame.h"
#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace henia::test {

struct Rgba8 final {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;
};

struct GoldenProbe final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    Rgba8 expected{};
    std::uint8_t tolerance = 0;
};

inline constexpr std::uint32_t kVisualWidth = 128;
inline constexpr std::uint32_t kVisualHeight = 128;

enum class GfxClipPosition : std::uint8_t {
    Outside,
    Crossing,
    Inside,
};

struct GfxClipFrame final {
    henia::gfx::BoxInstance box{};
    GfxClipPosition position = GfxClipPosition::Outside;
};

struct GfxClipSweep final {
    std::string_view plane;
    std::array<GfxClipFrame, 3> frames{};
};

[[nodiscard]] constexpr henia::gfx::BoxInstance gfxClipBox(
    henia::gfx::Vec3 minimum,
    henia::gfx::Vec3 maximum) noexcept {
    return {
        .minimum = minimum,
        .lineWidth = 4.0F,
        .maximum = maximum,
        .color = {0.95F, 0.95F, 0.95F, 1.0F},
    };
}

// With a 90-degree perspective, the side planes are x/y = +/-z. Each
// sequence moves one wire box from fully outside through the plane and into
// the frustum. The near/far values match gfxClipView().
inline constexpr std::array kGfxClipSweeps{
    GfxClipSweep{"left", {{
        {gfxClipBox({-1.55F, -0.08F, 0.90F}, {-1.30F, 0.08F, 1.10F}), GfxClipPosition::Outside},
        {gfxClipBox({-1.20F, -0.08F, 0.90F}, {-0.80F, 0.08F, 1.10F}), GfxClipPosition::Crossing},
        {gfxClipBox({-0.70F, -0.08F, 0.90F}, {-0.50F, 0.08F, 1.10F}), GfxClipPosition::Inside},
    }}},
    GfxClipSweep{"right", {{
        {gfxClipBox({1.30F, -0.08F, 0.90F}, {1.55F, 0.08F, 1.10F}), GfxClipPosition::Outside},
        {gfxClipBox({0.80F, -0.08F, 0.90F}, {1.20F, 0.08F, 1.10F}), GfxClipPosition::Crossing},
        {gfxClipBox({0.50F, -0.08F, 0.90F}, {0.70F, 0.08F, 1.10F}), GfxClipPosition::Inside},
    }}},
    GfxClipSweep{"bottom", {{
        {gfxClipBox({-0.08F, -1.55F, 0.90F}, {0.08F, -1.30F, 1.10F}), GfxClipPosition::Outside},
        {gfxClipBox({-0.08F, -1.20F, 0.90F}, {0.08F, -0.80F, 1.10F}), GfxClipPosition::Crossing},
        {gfxClipBox({-0.08F, -0.70F, 0.90F}, {0.08F, -0.50F, 1.10F}), GfxClipPosition::Inside},
    }}},
    GfxClipSweep{"top", {{
        {gfxClipBox({-0.08F, 1.30F, 0.90F}, {0.08F, 1.55F, 1.10F}), GfxClipPosition::Outside},
        {gfxClipBox({-0.08F, 0.80F, 0.90F}, {0.08F, 1.20F, 1.10F}), GfxClipPosition::Crossing},
        {gfxClipBox({-0.08F, 0.50F, 0.90F}, {0.08F, 0.70F, 1.10F}), GfxClipPosition::Inside},
    }}},
    GfxClipSweep{"near", {{
        {gfxClipBox({-0.05F, -0.05F, 0.10F}, {0.05F, 0.05F, 0.30F}), GfxClipPosition::Outside},
        {gfxClipBox({-0.05F, -0.05F, 0.25F}, {0.05F, 0.05F, 0.75F}), GfxClipPosition::Crossing},
        {gfxClipBox({-0.05F, -0.05F, 0.70F}, {0.05F, 0.05F, 1.00F}), GfxClipPosition::Inside},
    }}},
    GfxClipSweep{"far", {{
        {gfxClipBox({-0.20F, -0.20F, 4.20F}, {0.20F, 0.20F, 4.60F}), GfxClipPosition::Outside},
        {gfxClipBox({-0.20F, -0.20F, 3.50F}, {0.20F, 0.20F, 4.50F}), GfxClipPosition::Crossing},
        {gfxClipBox({-0.20F, -0.20F, 3.10F}, {0.20F, 0.20F, 3.50F}), GfxClipPosition::Inside},
    }}},
};

inline constexpr henia::gfx::BoxInstance kGfxCameraCrossingBox = gfxClipBox(
    {0.18F, -0.04F, -0.25F},
    {0.28F, 0.04F, 1.00F});

[[nodiscard]] inline henia::gfx::ViewParameters gfxClipView(
    henia::gfx::ClipDepthRange depthRange) noexcept {
    constexpr float halfPi = 1.57079632679489661923F;
    henia::gfx::Mat4 projection = henia::gfx::perspective(halfPi, 1.0F, 0.5F, 4.0F);
    if (depthRange == henia::gfx::ClipDepthRange::MinusOneToOne) {
        for (std::size_t column = 0; column < 4; ++column) {
            const std::size_t z = column * 4U + 2U;
            const std::size_t w = column * 4U + 3U;
            projection.values[z] = projection.values[z] * 2.0F - projection.values[w];
        }
    }
    return {
        .viewProjection = projection,
        .viewport = {static_cast<float>(kVisualWidth), static_cast<float>(kVisualHeight)},
        .clipDepthRange = depthRange,
    };
}

[[nodiscard]] inline henia::gfx::InstanceBatch gfxClipBatch(
    const henia::gfx::BoxInstance& box) {
    henia::gfx::ShapeBatch3D shapes;
    static_cast<void>(shapes.addBox(box));
    return shapes.snapshot();
}

[[nodiscard]] inline std::size_t visibleGfxPixelCount(
    std::span<const Rgba8> pixels) noexcept {
    return static_cast<std::size_t>(std::count_if(
        pixels.begin(), pixels.end(), [](Rgba8 pixel) noexcept {
            return pixel.red > 16 || pixel.green > 16 || pixel.blue > 16;
        }));
}

[[nodiscard]] inline bool matchesGfxClipFrame(
    std::span<const Rgba8> pixels,
    GfxClipPosition position) noexcept {
    if (pixels.size() != static_cast<std::size_t>(kVisualWidth) * kVisualHeight) return false;
    const std::size_t visible = visibleGfxPixelCount(pixels);
    if (position == GfxClipPosition::Outside) return visible == 0;
    return visible >= 8 && visible <= pixels.size() / 2U;
}

[[nodiscard]] inline bool matchesGfxCameraCrossing(
    std::span<const Rgba8> pixels) noexcept {
    if (!matchesGfxClipFrame(pixels, GfxClipPosition::Crossing)) return false;
    std::size_t shortenedEdgePixels = 0;
    for (std::uint32_t y = 56; y <= 60; ++y) {
        for (std::uint32_t x = 84; x <= 94; ++x) {
            const Rgba8 pixel = pixels[static_cast<std::size_t>(y) * kVisualWidth + x];
            if (pixel.red > 16 || pixel.green > 16 || pixel.blue > 16) {
                ++shortenedEdgePixels;
            }
        }
    }
    return shortenedEdgePixels >= 3;
}

// Stable interior probes plus a few explicitly tolerant AA-fringe/cap probes
// form a compact golden image. The latter catch clipped analytic geometry while
// allowing small WARP/OpenGL rasterization differences.
inline constexpr std::array kUiGolden{
    GoldenProbe{2, 2, {0, 0, 0, 255}, 2},
    GoldenProbe{7, 32, {34, 5, 7, 255}, 6},
    GoldenProbe{32, 32, {217, 31, 46, 255}, 8},
    GoldenProbe{71, 32, {5, 34, 10, 255}, 6},
    GoldenProbe{96, 10, {31, 217, 64, 255}, 16},
    GoldenProbe{40, 70, {145, 145, 153, 255}, 12},
    GoldenProbe{86, 64, {51, 179, 230, 255}, 10},
    GoldenProbe{99, 64, {0, 0, 0, 255}, 2},
    GoldenProbe{97, 74, {157, 44, 122, 255}, 15},
    GoldenProbe{32, 88, {15, 45, 115, 255}, 12},
    GoldenProbe{22, 100, {15, 45, 115, 255}, 12},
    GoldenProbe{66, 91, {82, 18, 64, 255}, 10},
    GoldenProbe{96, 96, {230, 184, 31, 255}, 8},
    GoldenProbe{76, 96, {0, 0, 0, 255}, 2},
    GoldenProbe{116, 96, {0, 0, 0, 255}, 2},
};

[[nodiscard]] inline henia::ui::RenderPacket buildUiVisualScene(
    henia::ui::TextureStore& textures,
    henia::ui::Frame& frame) {
    using namespace henia::ui;
    std::array<std::byte, 16> alpha{};
    alpha.fill(std::byte{0xFF});
    const TextureHandle atlas = textures.create(TextureFormat::Alpha8, 4, 4, 4, alpha);

    frame.reserve(16, 8);
    Canvas& canvas = frame.begin();
    canvas.fillRect({{8.0F, 8.0F}, {56.0F, 56.0F}}, {0.85F, 0.12F, 0.18F, 1.0F}, 8.0F);
    canvas.strokeRect(
        {{72.0F, 8.0F}, {120.0F, 56.0F}},
        {0.12F, 0.85F, 0.25F, 1.0F},
        6.0F,
        4.0F);
    FontStore fonts;
    std::vector<GlyphMetrics> glyphs{
        {U'A', {{0.0F, 0.0F}, {1.0F, 1.0F}}, {32.0F, 16.0F}, {0.0F, 16.0F}, 32.0F},
    };
    const FontHandle font = fonts.add({
        .atlas = atlas,
        .pixelSize = 16.0F,
        .ascent = 16.0F,
        .glyphs = std::move(glyphs),
    });
    TextRunCache cache(fonts);
    TextPainter text(cache);
    text.draw(canvas, font, 16.0F, {24.0F, 64.0F}, {0.95F, 0.95F, 1.0F, 0.6F}, "A");
    canvas.line(
        {76.0F, 64.0F},
        {96.0F, 64.0F},
        {0.20F, 0.70F, 0.90F, 1.0F},
        4.0F,
        LineCap::Butt);
    canvas.line(
        {76.0F, 74.0F},
        {96.0F, 74.0F},
        {0.90F, 0.25F, 0.70F, 1.0F},
        4.0F,
        LineCap::Square);
    const std::array polyline{
        Vec2{12.0F, 112.0F},
        Vec2{32.0F, 88.0F},
        Vec2{52.0F, 112.0F},
    };
    canvas.polyline(
        polyline,
        {0.12F, 0.35F, 0.90F, 0.5F},
        6.0F,
        false,
        LineCap::Round,
        LineJoin::Round);
    const std::array bevelPolyline{
        Vec2{56.0F, 112.0F},
        Vec2{66.0F, 92.0F},
        Vec2{76.0F, 112.0F},
    };
    canvas.polyline(
        bevelPolyline,
        {0.90F, 0.20F, 0.70F, 0.5F},
        6.0F,
        false,
        LineCap::Butt,
        LineJoin::Bevel);
    static_cast<void>(canvas.pushClip({{80.0F, 80.0F}, {112.0F, 112.0F}}));
    canvas.fillRect({{72.0F, 72.0F}, {120.0F, 120.0F}}, {0.90F, 0.72F, 0.12F, 1.0F});
    static_cast<void>(canvas.popClip());
    return frame.finish();
}

[[nodiscard]] inline bool matchesUiGolden(
    std::span<const Rgba8> pixels,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    if (width != kVisualWidth || height != kVisualHeight
        || pixels.size() != static_cast<std::size_t>(width) * height) {
        return false;
    }
    const auto difference = [](std::uint8_t left, std::uint8_t right) noexcept {
        return left > right ? left - right : right - left;
    };
    for (const GoldenProbe& probe : kUiGolden) {
        const Rgba8 actual = pixels[static_cast<std::size_t>(probe.y) * width + probe.x];
        if (difference(actual.red, probe.expected.red) > probe.tolerance
            || difference(actual.green, probe.expected.green) > probe.tolerance
            || difference(actual.blue, probe.expected.blue) > probe.tolerance
            || difference(actual.alpha, probe.expected.alpha) > probe.tolerance) {
            return false;
        }
    }
    return true;
}

inline void writePpm(
    std::string_view filename,
    std::span<const Rgba8> pixels,
    std::uint32_t width,
    std::uint32_t height) {
    std::filesystem::path directory{"out/test-artifacts"};
#ifdef _WIN32
    char* configured = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&configured, &length, "HENIAUI_TEST_ARTIFACT_DIR") == 0
        && configured != nullptr && length > 1) {
        directory = configured;
    }
    std::free(configured);
#else
    if (const char* configured = std::getenv("HENIAUI_TEST_ARTIFACT_DIR"); configured != nullptr) {
        directory = configured;
    }
#endif
    std::filesystem::create_directories(directory);
    std::ofstream output(directory / filename, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (const Rgba8 pixel : pixels) {
        const std::array bytes{
            static_cast<char>(pixel.red),
            static_cast<char>(pixel.green),
            static_cast<char>(pixel.blue),
        };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
}

} // namespace henia::test
