#pragma once

#include "henia/RenderProfile.h"

#include <array>
#include <cstdint>

namespace henia::gfx {

struct Vec2 final {
    float x = 0.0F;
    float y = 0.0F;
    [[nodiscard]] constexpr bool operator==(const Vec2&) const noexcept = default;
};

struct Vec3 final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

struct LinearColor final {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;
    float alpha = 1.0F;
    [[nodiscard]] constexpr bool operator==(const LinearColor&) const noexcept = default;
};

struct Mat4 final {
    // Column-major clip transform: clip = viewProjection * vec4(position, 1).
    std::array<float, 16> values{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    [[nodiscard]] constexpr bool operator==(const Mat4&) const noexcept = default;
};

enum class ClipDepthRange : std::uint8_t {
    MinusOneToOne,
    ZeroToOne,
};

struct ViewParameters final {
    Mat4 viewProjection{};
    Vec2 viewport{};
    float timeSeconds = 0.0F;
    ClipDepthRange clipDepthRange = ClipDepthRange::ZeroToOne;
};

enum class CompareOp : std::uint8_t {
    Never,
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
    Always,
};

struct DepthState final {
    bool enabled = false;
    bool writeEnabled = false;
    CompareOp compare = CompareOp::LessEqual;
    [[nodiscard]] constexpr bool operator==(const DepthState&) const noexcept = default;
};

enum class BoxEffect : std::uint32_t {
    None = 0,
    HueCycle = 1U << 0U,
};

[[nodiscard]] constexpr BoxEffect operator|(BoxEffect left, BoxEffect right) noexcept {
    return static_cast<BoxEffect>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

struct BoxInstance final {
    Vec3 minimum{};
    float lineWidth = 1.5F;
    Vec3 maximum{1.0F, 1.0F, 1.0F};
    float hueOffset = 0.0F;
    LinearColor color{};
    BoxEffect effects = BoxEffect::None;
    std::array<std::uint32_t, 3> reserved{};
    [[nodiscard]] constexpr bool operator==(const BoxInstance&) const noexcept = default;
};

using RenderProfile = henia::RenderProfile;

} // namespace henia::gfx
