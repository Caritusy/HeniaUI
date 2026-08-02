#pragma once

#include "henia/gfx/Math.h"
#include "henia/gfx/ShapeBatch3D.h"
#include "henia/ui/Frame.h"
#include "henia/ui/resource/TextureStore.h"
#include "henia/ui/text/FontStore.h"
#include "henia/ui/text/TextLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
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

enum class GfxAaKind : std::uint8_t {
    PerspectiveWidth,
    Subpixel,
    Thick,
    TranslucentCorner,
};

struct GfxAaCase final {
    std::string_view name;
    henia::gfx::BoxInstance box{};
    GfxAaKind kind = GfxAaKind::PerspectiveWidth;
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

inline constexpr std::array kGfxAaCases{
    GfxAaCase{
        "perspective-width",
        {
            .minimum = {-0.30F, -0.35F, 0.65F},
            .lineWidth = 6.0F,
            .maximum = {0.35F, 0.40F, 3.50F},
            .color = {0.95F, 0.10F, 0.08F, 1.0F},
        },
        GfxAaKind::PerspectiveWidth,
    },
    GfxAaCase{
        "subpixel",
        {
            .minimum = {0.15F, -0.01F, 1.40F},
            .lineWidth = 0.60F,
            .maximum = {0.16F, 0.0F, 1.41F},
            .color = {0.95F, 0.80F, 0.10F, 1.0F},
        },
        GfxAaKind::Subpixel,
    },
    GfxAaCase{
        "thick",
        {
            .minimum = {-0.65F, -0.40F, 1.40F},
            .lineWidth = 14.0F,
            .maximum = {-0.20F, 0.35F, 1.45F},
            .color = {0.08F, 0.90F, 0.20F, 1.0F},
        },
        GfxAaKind::Thick,
    },
    GfxAaCase{
        "translucent-corner",
        {
            .minimum = {0.15F, -0.35F, 1.0F},
            .lineWidth = 8.0F,
            .maximum = {0.55F, 0.35F, 1.60F},
            .color = {0.10F, 0.35F, 0.95F, 0.35F},
        },
        GfxAaKind::TranslucentCorner,
    },
};

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

[[nodiscard]] inline henia::gfx::ViewParameters gfxAaView() noexcept {
    constexpr float halfPi = 1.57079632679489661923F;
    return {
        .viewProjection = henia::gfx::perspective(halfPi, 1.0F, 0.5F, 6.0F),
        .viewport = {static_cast<float>(kVisualWidth), static_cast<float>(kVisualHeight)},
    };
}

[[nodiscard]] inline henia::gfx::InstanceBatch gfxClipBatch(
    const henia::gfx::BoxInstance& box) {
    henia::gfx::ShapeBatch3D shapes;
    static_cast<void>(shapes.addBox(box));
    return shapes.snapshot();
}

[[nodiscard]] inline henia::gfx::InstanceBatch gfxAaBatch(const GfxAaCase& value) {
    return gfxClipBatch(value.box);
}

struct GfxPixelPoint final {
    float x = 0.0F;
    float y = 0.0F;
};

[[nodiscard]] inline GfxPixelPoint projectGfxPoint(
    const henia::gfx::Mat4& matrix,
    henia::gfx::Vec3 point) noexcept {
    const float clipX = matrix.values[0] * point.x + matrix.values[4] * point.y
        + matrix.values[8] * point.z + matrix.values[12];
    const float clipY = matrix.values[1] * point.x + matrix.values[5] * point.y
        + matrix.values[9] * point.z + matrix.values[13];
    const float clipW = matrix.values[3] * point.x + matrix.values[7] * point.y
        + matrix.values[11] * point.z + matrix.values[15];
    return {
        (clipX / clipW * 0.5F + 0.5F) * static_cast<float>(kVisualWidth),
        (0.5F - clipY / clipW * 0.5F) * static_cast<float>(kVisualHeight),
    };
}

[[nodiscard]] inline std::uint8_t gfxChannel(Rgba8 pixel, std::uint32_t channel) noexcept {
    if (channel == 0) return pixel.red;
    if (channel == 1) return pixel.green;
    return pixel.blue;
}

[[nodiscard]] inline std::uint8_t maxGfxChannelNear(
    std::span<const Rgba8> pixels,
    GfxPixelPoint point,
    std::uint32_t channel,
    int radius = 1) noexcept {
    std::uint8_t maximum = 0;
    const int centerX = static_cast<int>(std::lround(point.x));
    const int centerY = static_cast<int>(std::lround(point.y));
    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            if (x < 0 || y < 0 || x >= static_cast<int>(kVisualWidth)
                || y >= static_cast<int>(kVisualHeight)) {
                continue;
            }
            maximum = std::max(
                maximum,
                gfxChannel(
                    pixels[static_cast<std::size_t>(y) * kVisualWidth
                        + static_cast<std::size_t>(x)],
                    channel));
        }
    }
    return maximum;
}

[[nodiscard]] inline std::size_t gfxProfileWidth(
    std::span<const Rgba8> pixels,
    GfxPixelPoint point,
    GfxPixelPoint normal,
    std::uint32_t channel,
    std::uint8_t threshold,
    int radius) noexcept {
    std::size_t count = 0;
    for (int offset = -radius; offset <= radius; ++offset) {
        const int x = static_cast<int>(std::lround(point.x + normal.x * static_cast<float>(offset)));
        const int y = static_cast<int>(std::lround(point.y + normal.y * static_cast<float>(offset)));
        if (x < 0 || y < 0 || x >= static_cast<int>(kVisualWidth)
            || y >= static_cast<int>(kVisualHeight)) {
            continue;
        }
        const Rgba8 pixel = pixels[static_cast<std::size_t>(y) * kVisualWidth
            + static_cast<std::size_t>(x)];
        count += gfxChannel(pixel, channel) > threshold ? 1U : 0U;
    }
    return count;
}

[[nodiscard]] inline bool matchesGfxAaCase(
    std::span<const Rgba8> pixels,
    const GfxAaCase& value) noexcept {
    if (pixels.size() != static_cast<std::size_t>(kVisualWidth) * kVisualHeight) return false;
    const henia::gfx::ViewParameters view = gfxAaView();
    if (value.kind == GfxAaKind::PerspectiveWidth) {
        const GfxPixelPoint start = projectGfxPoint(view.viewProjection, value.box.minimum);
        const GfxPixelPoint finish = projectGfxPoint(
            view.viewProjection,
            {value.box.minimum.x, value.box.minimum.y, value.box.maximum.z});
        const float dx = finish.x - start.x;
        const float dy = finish.y - start.y;
        const float inverseLength = 1.0F / std::sqrt(dx * dx + dy * dy);
        const GfxPixelPoint normal{-dy * inverseLength, dx * inverseLength};
        const GfxPixelPoint nearPoint{start.x + dx * 0.30F, start.y + dy * 0.30F};
        const GfxPixelPoint farPoint{start.x + dx * 0.85F, start.y + dy * 0.85F};
        const std::size_t nearWidth = gfxProfileWidth(pixels, nearPoint, normal, 0, 24, 6);
        const std::size_t farWidth = gfxProfileWidth(pixels, farPoint, normal, 0, 24, 6);
        const std::uint8_t nearCenter = maxGfxChannelNear(pixels, nearPoint, 0, 0);
        const std::uint8_t farCenter = maxGfxChannelNear(pixels, farPoint, 0, 0);
        const std::size_t difference = nearWidth > farWidth
            ? nearWidth - farWidth
            : farWidth - nearWidth;
        return nearWidth >= 7 && nearWidth <= 9
            && farWidth >= 7 && farWidth <= 9 && difference <= 1
            && nearCenter >= 220 && farCenter >= 220;
    }
    if (value.kind == GfxAaKind::Subpixel) {
        const std::size_t visible = static_cast<std::size_t>(std::count_if(
            pixels.begin(), pixels.end(), [](Rgba8 pixel) noexcept {
                return pixel.red > 16 || pixel.green > 16 || pixel.blue > 16;
            }));
        const GfxPixelPoint center = projectGfxPoint(
            view.viewProjection,
            {
                (value.box.minimum.x + value.box.maximum.x) * 0.5F,
                (value.box.minimum.y + value.box.maximum.y) * 0.5F,
                (value.box.minimum.z + value.box.maximum.z) * 0.5F,
            });
        const std::uint8_t red = maxGfxChannelNear(pixels, center, 0, 2);
        return visible >= 1 && visible <= 24 && red >= 24 && red <= 240;
    }
    if (value.kind == GfxAaKind::Thick) {
        const GfxPixelPoint start = projectGfxPoint(view.viewProjection, value.box.minimum);
        const GfxPixelPoint finish = projectGfxPoint(
            view.viewProjection,
            {value.box.maximum.x, value.box.minimum.y, value.box.minimum.z});
        const GfxPixelPoint middle{(start.x + finish.x) * 0.5F, (start.y + finish.y) * 0.5F};
        const std::size_t width = gfxProfileWidth(pixels, middle, {0.0F, 1.0F}, 1, 24, 16);
        return width >= 13 && width <= 17;
    }
    const GfxPixelPoint edgeStart = projectGfxPoint(view.viewProjection, value.box.minimum);
    const GfxPixelPoint edgeFinish = projectGfxPoint(
        view.viewProjection,
        {value.box.maximum.x, value.box.minimum.y, value.box.minimum.z});
    const GfxPixelPoint edgeMiddle{
        (edgeStart.x + edgeFinish.x) * 0.5F,
        (edgeStart.y + edgeFinish.y) * 0.5F,
    };
    const std::uint8_t edgeBlue = maxGfxChannelNear(pixels, edgeMiddle, 2, 1);
    const std::uint8_t cornerBlue = maxGfxChannelNear(pixels, edgeFinish, 2, 1);
    return edgeBlue >= 48 && edgeBlue <= 110
        && cornerBlue >= 96 && cornerBlue <= 210
        && cornerBlue >= static_cast<std::uint8_t>(edgeBlue + 24U);
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
    GoldenProbe{7, 32, {40, 29, 60, 255}, 8},
    GoldenProbe{32, 32, {217, 31, 46, 255}, 8},
    GoldenProbe{22, 21, {31, 115, 242, 255}, 10},
    GoldenProbe{43, 21, {38, 217, 122, 255}, 10},
    GoldenProbe{20, 46, {226, 159, 69, 255}, 10},
    GoldenProbe{50, 46, {169, 44, 222, 255}, 10},
    GoldenProbe{60, 32, {10, 38, 86, 255}, 10},
    GoldenProbe{71, 32, {5, 34, 10, 255}, 6},
    GoldenProbe{96, 10, {31, 217, 64, 255}, 16},
    GoldenProbe{114, 32, {32, 161, 204, 255}, 12},
    GoldenProbe{96, 20, {208, 150, 21, 255}, 12},
    GoldenProbe{96, 32, {0, 0, 0, 255}, 2},
    GoldenProbe{40, 70, {145, 145, 153, 255}, 12},
    GoldenProbe{86, 64, {51, 179, 230, 255}, 10},
    GoldenProbe{99, 64, {0, 0, 0, 255}, 2},
    GoldenProbe{97, 74, {157, 44, 122, 255}, 15},
    GoldenProbe{109, 66, {22, 65, 108, 255}, 24},
    GoldenProbe{113, 61, {102, 204, 255, 255}, 16},
    GoldenProbe{118, 66, {36, 107, 179, 255}, 16},
    GoldenProbe{32, 88, {15, 45, 115, 255}, 12},
    GoldenProbe{22, 100, {15, 45, 115, 255}, 12},
    GoldenProbe{66, 91, {82, 18, 64, 255}, 10},
    GoldenProbe{96, 96, {230, 184, 31, 255}, 8},
    GoldenProbe{76, 96, {0, 0, 0, 255}, 2},
    GoldenProbe{116, 96, {30, 70, 220, 255}, 10},
    GoldenProbe{122, 96, {220, 45, 35, 255}, 10},
};

[[nodiscard]] inline henia::ui::RenderPacket buildUiVisualScene(
    henia::ui::TextureStore& textures,
    henia::ui::Frame& frame) {
    using namespace henia::ui;
    std::array<std::byte, 16> alpha{};
    alpha.fill(std::byte{0xFF});
    const TextureHandle atlas = textures.create(TextureFormat::Alpha8, 4, 4, 4, alpha);
    std::array<std::byte, 64> panelPixels{};
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            const bool border = x == 0 || y == 0 || x == 3 || y == 3;
            const std::array<std::uint8_t, 4> color = border
                ? std::array<std::uint8_t, 4>{220, 45, 35, 255}
                : std::array<std::uint8_t, 4>{30, 70, 220, 255};
            const std::size_t offset = (y * 4 + x) * 4;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                panelPixels[offset + channel] = static_cast<std::byte>(color[channel]);
            }
        }
    }
    const TextureHandle panel = textures.create(TextureFormat::Rgba8, 4, 4, 16, panelPixels);

    frame.reserve(32, 8);
    Canvas& canvas = frame.begin();
    canvas.roundedShadow(
        {{8.0F, 8.0F}, {56.0F, 56.0F}},
        {0.05F, 0.20F, 0.45F, 0.8F},
        8.0F,
        4.0F,
        {3.0F, 3.0F});
    canvas.tintRect({{8.0F, 8.0F}, {56.0F, 56.0F}}, {0.85F, 0.12F, 0.18F, 1.0F}, 8.0F);
    canvas.ellipse({{14.0F, 14.0F}, {30.0F, 28.0F}}, {0.12F, 0.45F, 0.95F, 1.0F});
    canvas.capsule({{34.0F, 14.0F}, {52.0F, 28.0F}}, {0.15F, 0.85F, 0.48F, 1.0F});
    canvas.animatedGradientRect(
        {{12.0F, 40.0F}, {52.0F, 52.0F}},
        {0.95F, 0.75F, 0.10F, 1.0F},
        {0.65F, 0.15F, 0.90F, 1.0F},
        {1.0F, 0.0F},
        0.0F,
        3.0F);
    canvas.roundedOutline(
        {{72.0F, 8.0F}, {120.0F, 56.0F}},
        {0.12F, 0.85F, 0.25F, 1.0F},
        6.0F,
        4.0F);
    canvas.border(
        {{77.0F, 13.0F}, {115.0F, 51.0F}},
        {0.15F, 0.75F, 0.95F, 1.0F},
        {2.0F, 9.0F, 14.0F, 5.0F},
        2.0F);
    canvas.arc(
        {{84.0F, 20.0F}, {108.0F, 44.0F}},
        -1.5707963F,
        4.712389F,
        {1.0F, 0.72F, 0.10F, 1.0F},
        3.0F);
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
    const std::array iconEffects{
        EffectLayer{
            .kind = EffectLayerKind::Glow,
            .color = {0.20F, 0.60F, 1.0F, 0.70F},
            .amount = 3.0F,
        },
        EffectLayer{
            .kind = EffectLayerKind::Tint,
            .color = {1.0F, 0.0F, 0.0F, 1.0F},
            .enabled = false,
        },
    };
    canvas.effectRect({{112.0F, 60.0F}, {124.0F, 72.0F}}, 3.0F, iconEffects);
    canvas.sdfIcon(
        panel,
        {{112.0F, 60.0F}, {124.0F, 72.0F}},
        {{0.0F, 0.0F}, {1.0F, 1.0F}},
        {0.40F, 0.80F, 1.0F, 1.0F});
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
    canvas.ninePatch(
        panel,
        {{104.0F, 80.0F}, {124.0F, 120.0F}},
        {{0.0F, 0.0F}, {1.0F, 1.0F}},
        4.0F,
        0.25F);
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
