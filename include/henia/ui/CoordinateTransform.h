#pragma once

#include "henia/ui/Types.h"

#include <cmath>
#include <cstdint>

namespace henia::ui {

// Axis-aligned affine transform used at host integration boundaries. Positive
// scale preserves the UI top-left origin and makes transformed clip rectangles
// unambiguous. Translation supports sub-viewports and letterboxed targets.
struct AxisAlignedTransform final {
    Vec2 scale{1.0F, 1.0F};
    Vec2 translation{};

    [[nodiscard]] constexpr Vec2 point(Vec2 value) const noexcept {
        return {
            value.x * scale.x + translation.x,
            value.y * scale.y + translation.y,
        };
    }

    [[nodiscard]] constexpr Vec2 vector(Vec2 value) const noexcept {
        return {value.x * scale.x, value.y * scale.y};
    }

    [[nodiscard]] constexpr Rect rectangle(Rect value) const noexcept {
        return {point(value.min), point(value.max)};
    }

    [[nodiscard]] constexpr Vec2 inversePoint(Vec2 value) const noexcept {
        return {
            (value.x - translation.x) / scale.x,
            (value.y - translation.y) / scale.y,
        };
    }

    friend constexpr bool operator==(
        AxisAlignedTransform,
        AxisAlignedTransform) noexcept = default;
};

struct UiRenderViewport final {
    std::uint32_t framebufferWidth = 0;
    std::uint32_t framebufferHeight = 0;
    AxisAlignedTransform logicalToFramebuffer{};

    friend constexpr bool operator==(UiRenderViewport, UiRenderViewport) noexcept = default;
};

// One document may use independent input and render transforms. logicalViewport
// is measured in HeniaUI logical units; dpiScale is metadata for selecting font
// atlas resolution and other host resources, not an implicit layout multiplier.
struct UiCoordinateSpace final {
    Vec2 logicalViewport{};
    AxisAlignedTransform inputToLogical{};
    UiRenderViewport render{};
    float dpiScale = 1.0F;

    friend constexpr bool operator==(UiCoordinateSpace, UiCoordinateSpace) noexcept = default;
};

[[nodiscard]] inline bool valid(AxisAlignedTransform transform) noexcept {
    return std::isfinite(transform.scale.x) && transform.scale.x > 0.0F
        && std::isfinite(transform.scale.y) && transform.scale.y > 0.0F
        && std::isfinite(transform.translation.x)
        && std::isfinite(transform.translation.y);
}

[[nodiscard]] inline bool valid(UiRenderViewport viewport) noexcept {
    return viewport.framebufferWidth > 0 && viewport.framebufferHeight > 0
        && valid(viewport.logicalToFramebuffer);
}

[[nodiscard]] inline bool valid(UiCoordinateSpace space) noexcept {
    return std::isfinite(space.logicalViewport.x) && space.logicalViewport.x > 0.0F
        && std::isfinite(space.logicalViewport.y) && space.logicalViewport.y > 0.0F
        && valid(space.inputToLogical) && valid(space.render)
        && std::isfinite(space.dpiScale) && space.dpiScale > 0.0F;
}

[[nodiscard]] inline UiCoordinateSpace makeUiCoordinateSpace(
    Vec2 logicalViewport,
    Vec2 inputExtent,
    std::uint32_t framebufferWidth,
    std::uint32_t framebufferHeight,
    float dpiScale = 1.0F) noexcept {
    if (!std::isfinite(inputExtent.x) || inputExtent.x <= 0.0F
        || !std::isfinite(inputExtent.y) || inputExtent.y <= 0.0F
        || !std::isfinite(logicalViewport.x) || logicalViewport.x <= 0.0F
        || !std::isfinite(logicalViewport.y) || logicalViewport.y <= 0.0F) {
        return {};
    }
    return {
        .logicalViewport = logicalViewport,
        .inputToLogical = {
            .scale = {
                logicalViewport.x / inputExtent.x,
                logicalViewport.y / inputExtent.y,
            },
        },
        .render = {
            .framebufferWidth = framebufferWidth,
            .framebufferHeight = framebufferHeight,
            .logicalToFramebuffer = {
                .scale = {
                    static_cast<float>(framebufferWidth) / logicalViewport.x,
                    static_cast<float>(framebufferHeight) / logicalViewport.y,
                },
            },
        },
        .dpiScale = dpiScale,
    };
}

[[nodiscard]] constexpr Vec2 inputToFramebuffer(
    const UiCoordinateSpace& space,
    Vec2 inputPoint) noexcept {
    return space.render.logicalToFramebuffer.point(
        space.inputToLogical.point(inputPoint));
}

// Converts a requested physical-pixel width to logical units for a line with
// the supplied logical direction. This accounts for non-uniform framebuffer
// scaling; callers can keep selected strokes physically stable before recording.
[[nodiscard]] inline float logicalLineWidthForPhysicalPixels(
    float physicalPixels,
    Vec2 logicalDirection,
    AxisAlignedTransform logicalToFramebuffer) noexcept {
    if (!std::isfinite(physicalPixels) || physicalPixels < 0.0F
        || !std::isfinite(logicalDirection.x) || !std::isfinite(logicalDirection.y)
        || !valid(logicalToFramebuffer)) {
        return 0.0F;
    }
    const float directionLength = std::hypot(logicalDirection.x, logicalDirection.y);
    if (directionLength <= 0.0F) return 0.0F;
    const Vec2 tangent{
        logicalDirection.x / directionLength,
        logicalDirection.y / directionLength,
    };
    const Vec2 normal{-tangent.y, tangent.x};
    const Vec2 transformedTangent = logicalToFramebuffer.vector(tangent);
    const Vec2 transformedNormal = logicalToFramebuffer.vector(normal);
    const float transformedTangentLength = std::hypot(
        transformedTangent.x,
        transformedTangent.y);
    if (transformedTangentLength <= 0.0F) return 0.0F;
    const float physicalPerLogical = std::abs(
        transformedNormal.x * transformedTangent.y
        - transformedNormal.y * transformedTangent.x)
        / transformedTangentLength;
    return physicalPerLogical > 0.0F ? physicalPixels / physicalPerLogical : 0.0F;
}

} // namespace henia::ui
