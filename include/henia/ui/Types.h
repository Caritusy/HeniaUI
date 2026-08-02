#pragma once

#include <algorithm>
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
    float red   = 1.0F;
    float green = 1.0F;
    float blue  = 1.0F;
    float alpha = 1.0F;

    friend constexpr bool operator==(Color, Color) noexcept = default;
};

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

struct ClipRect final {
    Rect area{};
    bool enabled = false;

    friend constexpr bool operator==(ClipRect, ClipRect) noexcept = default;
};

} // namespace henia::ui
