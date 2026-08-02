#pragma once

#include "henia/ui/DisplayList.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace henia::ui {

// Public 2D rectangles use a reject-inverted policy. Minima must be strictly
// smaller than maxima; callers that want normalization must do it explicitly.
[[nodiscard]] inline std::string_view validateRect(Rect value, std::string_view field) noexcept {
    const auto component = [field](std::string_view suffix) noexcept -> std::string_view {
        if (field == "bounds") {
            if (suffix == "min.x") return "bounds.min.x";
            if (suffix == "min.y") return "bounds.min.y";
            if (suffix == "max.x") return "bounds.max.x";
            return "bounds.max.y";
        }
        if (field == "clip.area") {
            if (suffix == "min.x") return "clip.area.min.x";
            if (suffix == "min.y") return "clip.area.min.y";
            if (suffix == "max.x") return "clip.area.max.x";
            return "clip.area.max.y";
        }
        if (field == "uv") {
            if (suffix == "min.x") return "uv.min.x";
            if (suffix == "min.y") return "uv.min.y";
            if (suffix == "max.x") return "uv.max.x";
            return "uv.max.y";
        }
        if (field == "glyph.bounds") {
            if (suffix == "min.x") return "glyph.bounds.min.x";
            if (suffix == "min.y") return "glyph.bounds.min.y";
            if (suffix == "max.x") return "glyph.bounds.max.x";
            return "glyph.bounds.max.y";
        }
        if (field == "glyph.uv") {
            if (suffix == "min.x") return "glyph.uv.min.x";
            if (suffix == "min.y") return "glyph.uv.min.y";
            if (suffix == "max.x") return "glyph.uv.max.x";
            return "glyph.uv.max.y";
        }
        return field;
    };
    if (!std::isfinite(value.min.x)) return component("min.x");
    if (!std::isfinite(value.min.y)) return component("min.y");
    if (!std::isfinite(value.max.x)) return component("max.x");
    if (!std::isfinite(value.max.y)) return component("max.y");
    if (!value.valid()) return field;
    return {};
}

[[nodiscard]] inline std::string_view validateColor(Color value) noexcept {
    if (!std::isfinite(value.red)) return "color.red";
    if (!std::isfinite(value.green)) return "color.green";
    if (!std::isfinite(value.blue)) return "color.blue";
    if (!std::isfinite(value.alpha)) return "color.alpha";
    return {};
}

[[nodiscard]] inline std::string_view validateDrawCommand(const DrawCommand& command) noexcept {
    if (static_cast<std::uint8_t>(command.kind) > static_cast<std::uint8_t>(PrimitiveKind::Glyph)) {
        return "kind";
    }
    if (static_cast<std::uint8_t>(command.blend) > static_cast<std::uint8_t>(BlendMode::Additive)) {
        return "blend";
    }
    if (const std::string_view issue = validateRect(command.bounds, "bounds"); !issue.empty()) {
        return issue;
    }
    if (const std::string_view issue = validateColor(command.color); !issue.empty()) return issue;
    if (!std::isfinite(command.radius) || command.radius < 0.0F) return "radius";
    if (!std::isfinite(command.thickness) || command.thickness < 0.0F) return "thickness";
    if (static_cast<std::uint8_t>(command.lineCap) > static_cast<std::uint8_t>(LineCap::Round)) {
        return "line.cap";
    }
    if (static_cast<std::uint8_t>(command.lineJoin) > static_cast<std::uint8_t>(LineJoin::Round)) {
        return "line.join";
    }
    if ((command.lineFlags & ~(kLineHasPrevious | kLineHasNext)) != 0) return "line.flags";
    if (command.clip.enabled) {
        if (const std::string_view issue = validateRect(command.clip.area, "clip.area"); !issue.empty()) {
            return issue;
        }
    }
    if (command.kind == PrimitiveKind::Line) {
        const auto validSegment = [](Vec2 start, Vec2 finish) noexcept {
            const double deltaX = static_cast<double>(finish.x) - start.x;
            const double deltaY = static_cast<double>(finish.y) - start.y;
            const double length = std::hypot(deltaX, deltaY);
            return start != finish && std::isfinite(length)
                && length <= static_cast<double>(std::numeric_limits<float>::max());
        };
        if (!std::isfinite(command.pointA.x)) return "pointA.x";
        if (!std::isfinite(command.pointA.y)) return "pointA.y";
        if (!std::isfinite(command.pointB.x)) return "pointB.x";
        if (!std::isfinite(command.pointB.y)) return "pointB.y";
        if (!validSegment(command.pointA, command.pointB) || command.thickness <= 0.0F) {
            return "line.geometry";
        }
        if ((command.lineFlags & kLineHasPrevious) != 0) {
            if (!std::isfinite(command.uv.min.x)) return "line.previous.x";
            if (!std::isfinite(command.uv.min.y)) return "line.previous.y";
            if (!validSegment(command.uv.min, command.pointA)) return "line.previous";
        }
        if ((command.lineFlags & kLineHasNext) != 0) {
            if (!std::isfinite(command.uv.max.x)) return "line.next.x";
            if (!std::isfinite(command.uv.max.y)) return "line.next.y";
            if (!validSegment(command.pointB, command.uv.max)) return "line.next";
        }
    }
    if (command.kind == PrimitiveKind::Image || command.kind == PrimitiveKind::Glyph) {
        if (!command.texture.valid()) return "texture";
        if (const std::string_view issue = validateRect(command.uv, "uv"); !issue.empty()) return issue;
    }
    return {};
}

struct ScissorRect final {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;
};

// Floor minima and ceil maxima before clamping so fractional analytic-AA
// coverage at the clip edge is preserved on both rendering backends.
[[nodiscard]] inline bool makeScissorRect(
    Rect area,
    std::uint32_t viewportWidth,
    std::uint32_t viewportHeight,
    ScissorRect& output) noexcept {
    if (!validateRect(area, "clip.area").empty()
        || viewportWidth == 0 || viewportHeight == 0
        || viewportWidth > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        || viewportHeight > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    const double width = static_cast<double>(viewportWidth);
    const double height = static_cast<double>(viewportHeight);
    output.left = static_cast<std::int32_t>(
        std::clamp(std::floor(static_cast<double>(area.min.x)), 0.0, width));
    output.top = static_cast<std::int32_t>(
        std::clamp(std::floor(static_cast<double>(area.min.y)), 0.0, height));
    output.right = static_cast<std::int32_t>(
        std::clamp(std::ceil(static_cast<double>(area.max.x)), 0.0, width));
    output.bottom = static_cast<std::int32_t>(
        std::clamp(std::ceil(static_cast<double>(area.max.y)), 0.0, height));
    return true;
}

} // namespace henia::ui
