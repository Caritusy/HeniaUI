#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace henia::ui {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;

    friend constexpr bool operator==(Vec2, Vec2) noexcept = default;
};

struct Rect final {
    Vec2 min{};
    Vec2 max{};

    [[nodiscard]] constexpr float width() const noexcept { return max.x - min.x; }
    [[nodiscard]] constexpr float height() const noexcept { return max.y - min.y; }
    [[nodiscard]] constexpr bool valid() const noexcept {
        return min.x < max.x && min.y < max.y;
    }

    friend constexpr bool operator==(Rect, Rect) noexcept = default;
};

[[nodiscard]] constexpr Rect intersect(Rect left, Rect right) noexcept {
    return {
        {std::max(left.min.x, right.min.x), std::max(left.min.y, right.min.y)},
        {std::min(left.max.x, right.max.x), std::min(left.max.y, right.max.y)},
    };
}

struct Color final {
    // UI colors are straight-alpha values in linear-light space. Renderers
    // premultiply only the final fragment immediately before blending.
    float red   = 1.0F;
    float green = 1.0F;
    float blue  = 1.0F;
    float alpha = 1.0F;

    friend constexpr bool operator==(Color, Color) noexcept = default;
};

[[nodiscard]] inline float srgbToLinear(float channel) noexcept {
    const float value = std::clamp(channel, 0.0F, 1.0F);
    return value <= 0.04045F
        ? value / 12.92F
        : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] inline float linearToSrgb(float channel) noexcept {
    const float value = std::clamp(channel, 0.0F, 1.0F);
    return value <= 0.0031308F
        ? value * 12.92F
        : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

[[nodiscard]] inline Color srgbToLinear(Color color) noexcept {
    return {
        srgbToLinear(color.red),
        srgbToLinear(color.green),
        srgbToLinear(color.blue),
        color.alpha,
    };
}

[[nodiscard]] inline Color linearToSrgb(Color color) noexcept {
    return {
        linearToSrgb(color.red),
        linearToSrgb(color.green),
        linearToSrgb(color.blue),
        color.alpha,
    };
}

[[nodiscard]] constexpr Color straightToPremultiplied(Color color) noexcept {
    return {
        color.red * color.alpha,
        color.green * color.alpha,
        color.blue * color.alpha,
        color.alpha,
    };
}

[[nodiscard]] constexpr Color premultipliedToStraight(Color color) noexcept {
    return color.alpha != 0.0F
        ? Color{
            color.red / color.alpha,
            color.green / color.alpha,
            color.blue / color.alpha,
            color.alpha,
        }
        : Color{0.0F, 0.0F, 0.0F, 0.0F};
}

struct CornerRadii final {
    float topLeft = 0.0F;
    float topRight = 0.0F;
    float bottomRight = 0.0F;
    float bottomLeft = 0.0F;

    friend constexpr bool operator==(CornerRadii, CornerRadii) noexcept = default;
};

struct TextMetrics final {
    float width  = 0.0F;
    float height = 0.0F;
};

enum class BlendMode : std::uint8_t {
    PremultipliedAlpha,
    Additive,
};

// The host render target declares whether the fixed-function output stage
// stores linear values directly or encodes them with the sRGB transfer curve.
enum class RenderTargetColorSpace : std::uint8_t {
    Linear,
    Srgb,
};

struct ClipRect final {
    Rect area{};
    bool enabled = false;

    friend constexpr bool operator==(ClipRect, ClipRect) noexcept = default;
};

} // namespace henia::ui
