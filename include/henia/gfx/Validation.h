#pragma once

#include "henia/gfx/Types.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace henia::gfx {

[[nodiscard]] inline bool finite(Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] inline bool finite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool finite(LinearColor value) noexcept {
    return std::isfinite(value.red) && std::isfinite(value.green)
        && std::isfinite(value.blue) && std::isfinite(value.alpha);
}

[[nodiscard]] inline bool finite(const Mat4& value) noexcept {
    for (const float component : value.values) {
        if (!std::isfinite(component)) return false;
    }
    return true;
}

[[nodiscard]] inline std::string_view validate(const ViewParameters& view) noexcept {
    if (!finite(view.viewProjection)) return "view.viewProjection";
    if (!std::isfinite(view.viewport.x)) return "view.viewport.x";
    if (!std::isfinite(view.viewport.y)) return "view.viewport.y";
    if (view.viewport.x <= 0.0F) return "view.viewport.x";
    if (view.viewport.y <= 0.0F) return "view.viewport.y";
    const float maximumViewport = std::nextafter(
        static_cast<float>(std::numeric_limits<std::int32_t>::max()), 0.0F);
    if (view.viewport.x > maximumViewport) return "view.viewport.x";
    if (view.viewport.y > maximumViewport) return "view.viewport.y";
    if (!std::isfinite(view.timeSeconds)) return "view.timeSeconds";
    if (!std::isfinite(view.motionScale)) return "view.motionScale";
    if (static_cast<std::uint8_t>(view.clipDepthRange)
        > static_cast<std::uint8_t>(ClipDepthRange::ZeroToOne)) {
        return "view.clipDepthRange";
    }
    return {};
}

[[nodiscard]] inline std::string_view validate(const BoxInstance& box) noexcept {
    if (!std::isfinite(box.minimum.x)) return "box.minimum.x";
    if (!std::isfinite(box.minimum.y)) return "box.minimum.y";
    if (!std::isfinite(box.minimum.z)) return "box.minimum.z";
    if (!std::isfinite(box.maximum.x)) return "box.maximum.x";
    if (!std::isfinite(box.maximum.y)) return "box.maximum.y";
    if (!std::isfinite(box.maximum.z)) return "box.maximum.z";
    if (box.minimum.x > box.maximum.x) return "box.minimum.x > box.maximum.x";
    if (box.minimum.y > box.maximum.y) return "box.minimum.y > box.maximum.y";
    if (box.minimum.z > box.maximum.z) return "box.minimum.z > box.maximum.z";
    if (!std::isfinite(box.lineWidth) || box.lineWidth <= 0.0F) return "box.lineWidth";
    if (!std::isfinite(box.hueOffset)) return "box.hueOffset";
    if (!std::isfinite(box.color.red)) return "box.color.red";
    if (!std::isfinite(box.color.green)) return "box.color.green";
    if (!std::isfinite(box.color.blue)) return "box.color.blue";
    if (!std::isfinite(box.color.alpha)) return "box.color.alpha";
    constexpr std::uint32_t validEffects = static_cast<std::uint32_t>(BoxEffect::HueCycle)
        | static_cast<std::uint32_t>(BoxEffect::MotionTranslation);
    if ((static_cast<std::uint32_t>(box.effects) & ~validEffects) != 0) return "box.effects";
    if ((static_cast<std::uint32_t>(box.effects)
            & static_cast<std::uint32_t>(BoxEffect::MotionTranslation)) != 0U
        && !finite(box.motionDelta())) {
        return "box.motionDelta";
    }
    return {};
}

[[nodiscard]] inline std::string_view validate(DepthState state) noexcept {
    if (static_cast<std::uint8_t>(state.compare) > static_cast<std::uint8_t>(CompareOp::Always)) {
        return "depth.compare";
    }
    return {};
}

} // namespace henia::gfx
