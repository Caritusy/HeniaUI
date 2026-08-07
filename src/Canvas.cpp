#include "henia/ui/Canvas.h"
#include "henia/ui/Validation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace henia::ui {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTau = kPi * 2.0F;

[[nodiscard]] bool visible(Color color) noexcept { return color.alpha > 0.0F; }

[[nodiscard]] bool validLineSegment(Vec2 from, Vec2 to) noexcept {
    const double deltaX = static_cast<double>(to.x) - from.x;
    const double deltaY = static_cast<double>(to.y) - from.y;
    const double length = std::hypot(deltaX, deltaY);
    return from != to && std::isfinite(length)
        && length <= static_cast<double>(std::numeric_limits<float>::max());
}

[[nodiscard]] Rect packed(Color color) noexcept {
    return {{color.red, color.green}, {color.blue, color.alpha}};
}

[[nodiscard]] CornerRadii normalizedRadii(Rect rect, CornerRadii radii) noexcept {
    const float width = rect.width();
    const float height = rect.height();
    float scale = 1.0F;
    const auto constrain = [&scale](float available, float required) noexcept {
        if (required > available && required > 0.0F) {
            scale = std::min(scale, available / required);
        }
    };
    constrain(width, radii.topLeft + radii.topRight);
    constrain(width, radii.bottomLeft + radii.bottomRight);
    constrain(height, radii.topLeft + radii.bottomLeft);
    constrain(height, radii.topRight + radii.bottomRight);
    radii.topLeft *= scale;
    radii.topRight *= scale;
    radii.bottomRight *= scale;
    radii.bottomLeft *= scale;
    return radii;
}

} // namespace

Canvas::ClipScope::ClipScope(Canvas& canvas, std::uint64_t token) noexcept
    : mCanvas(&canvas), mToken(token) {}

Canvas::ClipScope::~ClipScope() { static_cast<void>(reset()); }

Canvas::ClipScope::ClipScope(ClipScope&& other) noexcept
    : mCanvas(std::exchange(other.mCanvas, nullptr)),
      mToken(std::exchange(other.mToken, 0)) {}

Canvas::ClipScope& Canvas::ClipScope::operator=(ClipScope&& other) noexcept {
    if (this == &other) return *this;
    static_cast<void>(reset());
    mCanvas = std::exchange(other.mCanvas, nullptr);
    mToken = std::exchange(other.mToken, 0);
    return *this;
}

bool Canvas::ClipScope::active() const noexcept { return mCanvas != nullptr; }

bool Canvas::ClipScope::reset() noexcept {
    Canvas* canvas = std::exchange(mCanvas, nullptr);
    const std::uint64_t token = std::exchange(mToken, 0);
    return canvas != nullptr && canvas->releaseClip(token);
}

Canvas::Canvas(DisplayList& displayList) noexcept : mDisplayList(&displayList) {}

void Canvas::reset() noexcept {
    mClipDepth = 0;
    mBlendMode = BlendMode::PremultipliedAlpha;
    mRejectedCommands = 0;
    mClippedCommands = 0;
    mInvalidInputCommands = 0;
    mCapacityRejectedCommands = 0;
    mLastError = {};
}

Canvas::ClipScope Canvas::scopedClip(Rect rect) noexcept {
    std::uint64_t token = mNextClipToken++;
    if (token == 0) {
        token = mNextClipToken++;
    }
    return pushClip(rect, token) ? ClipScope(*this, token) : ClipScope{};
}

bool Canvas::pushClip(Rect rect) noexcept { return pushClip(rect, 0); }

bool Canvas::pushClip(Rect rect, std::uint64_t token) noexcept {
    if (const std::string_view issue = validateRect(rect, "clip.area"); !issue.empty()) {
        rejectInvalid(issue);
        return false;
    }
    if (mClipDepth == mClips.size()) {
        ++mRejectedCommands;
        ++mCapacityRejectedCommands;
        mLastError = "clipDepth.capacity";
        return false;
    }

    ClipEntry entry{
        .clip = {rect, true},
        .token = token,
    };
    if (mClipDepth != 0) {
        const ClipEntry& parent = mClips[mClipDepth - 1];
        if (parent.empty) {
            entry.clip = parent.clip;
            entry.empty = true;
        } else {
            const Rect intersection = intersect(parent.clip.area, rect);
            if (intersection.valid()) {
                entry.clip.area = intersection;
            } else {
                entry.empty = true;
            }
        }
    }

    mClips[mClipDepth++] = entry;
    mLastError = {};
    return true;
}

bool Canvas::popClip() noexcept {
    if (mClipDepth == 0) {
        ++mRejectedCommands;
        ++mInvalidInputCommands;
        mLastError = "clipDepth.underflow";
        return false;
    }
    --mClipDepth;
    collapseReleasedClips();
    mLastError = {};
    return true;
}

bool Canvas::releaseClip(std::uint64_t token) noexcept {
    if (token == 0) return false;
    for (std::size_t index = mClipDepth; index > 0; --index) {
        ClipEntry& entry = mClips[index - 1];
        if (entry.token != token) continue;
        entry.releasePending = true;
        collapseReleasedClips();
        return true;
    }
    return false;
}

void Canvas::collapseReleasedClips() noexcept {
    while (mClipDepth != 0 && mClips[mClipDepth - 1].releasePending) {
        --mClipDepth;
    }
}

void Canvas::line(
    Vec2 from,
    Vec2 to,
    Color color,
    float thickness,
    LineCap cap) noexcept {
    if (!std::isfinite(from.x)) return rejectInvalid("from.x");
    if (!std::isfinite(from.y)) return rejectInvalid("from.y");
    if (!std::isfinite(to.x)) return rejectInvalid("to.x");
    if (!std::isfinite(to.y)) return rejectInvalid("to.y");
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    if (!validLineSegment(from, to)) return rejectInvalid("line.endpoints");
    if (static_cast<std::uint8_t>(cap) > static_cast<std::uint8_t>(LineCap::Round)) {
        return rejectInvalid("line.cap");
    }

    appendLine(from, to, {}, {}, color, thickness, cap, LineJoin::Round, 0);
}

void Canvas::appendLine(
    Vec2 from,
    Vec2 to,
    Vec2 previous,
    Vec2 next,
    Color color,
    float thickness,
    LineCap cap,
    LineJoin join,
    std::uint8_t flags) noexcept {
    DrawCommand command{};
    command.kind = PrimitiveKind::Line;
    command.bounds = {from, to};
    command.uv = {{previous.x, previous.y}, {next.x, next.y}};
    command.color = color;
    command.thickness = thickness;
    command.lineCap = cap;
    command.lineJoin = join;
    command.lineFlags = flags;
    append(command);
}

void Canvas::polyline(
    std::span<const Vec2> points,
    Color color,
    float thickness,
    bool closed,
    LineCap cap,
    LineJoin join) noexcept {
    if (points.size() < 2) return rejectInvalid("points.size");
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    if (static_cast<std::uint8_t>(cap) > static_cast<std::uint8_t>(LineCap::Round)) {
        return rejectInvalid("line.cap");
    }
    if (static_cast<std::uint8_t>(join) > static_cast<std::uint8_t>(LineJoin::Round)) {
        return rejectInvalid("line.join");
    }
    for (const Vec2 point : points) {
        if (!std::isfinite(point.x)) return rejectInvalid("points.x");
        if (!std::isfinite(point.y)) return rejectInvalid("points.y");
    }

    std::size_t pointCount = points.size();
    if (closed && pointCount > 2 && points.front() == points.back()) {
        --pointCount;
    }
    if ((closed && pointCount < 3) || pointCount < 2) {
        return rejectInvalid("points.size");
    }
    const std::size_t segmentCount = closed ? pointCount : pointCount - 1U;
    for (std::size_t index = 0; index < segmentCount; ++index) {
        if (!validLineSegment(points[index], points[(index + 1U) % pointCount])) {
            return rejectInvalid("line.endpoints");
        }
    }

    for (std::size_t index = 0; index < segmentCount; ++index) {
        const std::size_t finish = (index + 1U) % pointCount;
        std::uint8_t flags = 0;
        Vec2 previous{};
        Vec2 next{};
        if (closed || index != 0) {
            flags |= kLineHasPrevious;
            previous = points[(index + pointCount - 1U) % pointCount];
        }
        if (closed || finish + 1U < pointCount) {
            flags |= kLineHasNext;
            next = points[(finish + 1U) % pointCount];
        }
        appendLine(
            points[index],
            points[finish],
            previous,
            next,
            color,
            thickness,
            cap,
            join,
            flags);
    }
}

void Canvas::fillRect(Rect rect, Color color, float rounding) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!visible(color)) return rejectInvalid("color.alpha");

    DrawCommand command{};
    command.kind = PrimitiveKind::SolidRect;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::strokeRect(Rect rect, Color color, float rounding, float thickness) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");

    DrawCommand command{};
    command.kind = PrimitiveKind::StrokeRect;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = std::min(thickness, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::circle(Vec2 center, float radius, Color color) noexcept {
    if (!std::isfinite(center.x)) return rejectInvalid("center.x");
    if (!std::isfinite(center.y)) return rejectInvalid("center.y");
    if (!std::isfinite(radius) || radius <= 0.0F) return rejectInvalid("radius");
    ellipse({
        {center.x - radius, center.y - radius},
        {center.x + radius, center.y + radius},
    }, color);
}

void Canvas::ellipse(Rect rect, Color color) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!visible(color)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::Ellipse;
    command.bounds = rect;
    command.color = color;
    append(command);
}

void Canvas::arc(
    Rect ellipseBounds,
    float startRadians,
    float sweepRadians,
    Color color,
    float thickness) noexcept {
    if (const std::string_view issue = validateRect(ellipseBounds, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(startRadians)) return rejectInvalid("arc.startRadians");
    if (!std::isfinite(sweepRadians) || sweepRadians == 0.0F) {
        return rejectInvalid("arc.sweepRadians");
    }
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    float start = std::fmod(startRadians, kTau);
    if (start < 0.0F) start += kTau;
    const float sweep = std::clamp(sweepRadians, -kTau, kTau);
    DrawCommand command{};
    command.kind = PrimitiveKind::Arc;
    command.bounds = ellipseBounds;
    command.uv = {{start, sweep}, {0.0F, 0.0F}};
    command.color = color;
    command.thickness = std::min(
        thickness,
        std::min(ellipseBounds.width(), ellipseBounds.height()));
    append(command);
}

void Canvas::capsule(Rect rect, Color color) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!visible(color)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::Capsule;
    command.bounds = rect;
    command.color = color;
    append(command);
}

void Canvas::gradientRect(
    Rect rect,
    Color start,
    Color finish,
    Vec2 direction,
    float rounding) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(start); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateColor(finish); !issue.empty()) {
        return rejectInvalid("gradient.finish");
    }
    if (!std::isfinite(direction.x)) return rejectInvalid("gradient.direction.x");
    if (!std::isfinite(direction.y)) return rejectInvalid("gradient.direction.y");
    if (direction == Vec2{}) return rejectInvalid("gradient.direction");
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!visible(start) && !visible(finish)) return rejectInvalid("color.alpha");
    float angle = std::atan2(direction.y, direction.x);
    if (angle < 0.0F) angle += kTau;
    DrawCommand command{};
    command.kind = PrimitiveKind::GradientRect;
    command.bounds = rect;
    command.uv = packed(finish);
    command.color = start;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = angle;
    append(command);
}

void Canvas::tintRect(Rect rect, Color color, float rounding) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!visible(color)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::TintRect;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::animatedGradientRect(
    Rect rect,
    Color start,
    Color finish,
    Vec2 direction,
    float phase,
    float rounding) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(start); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateColor(finish); !issue.empty()) {
        return rejectInvalid("gradient.finish");
    }
    if (!std::isfinite(direction.x)) return rejectInvalid("gradient.direction.x");
    if (!std::isfinite(direction.y)) return rejectInvalid("gradient.direction.y");
    if (direction == Vec2{}) return rejectInvalid("gradient.direction");
    if (!std::isfinite(phase)) return rejectInvalid("gradient.phase");
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!visible(start) && !visible(finish)) return rejectInvalid("color.alpha");
    float angle = std::atan2(direction.y, direction.x);
    if (angle < 0.0F) angle += kTau;
    float normalizedPhase = phase - std::floor(phase);
    const auto parameter = static_cast<std::uint8_t>(
        std::clamp(std::lround(normalizedPhase * 255.0F), 0L, 255L));
    DrawCommand command{};
    command.kind = PrimitiveKind::AnimatedGradientRect;
    command.bounds = rect;
    command.uv = packed(finish);
    command.color = start;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = angle;
    command.lineFlags = parameter;
    append(command);
}

void Canvas::roundedGlow(
    Rect rect,
    Color color,
    float rounding,
    float glowRadius) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!std::isfinite(glowRadius) || glowRadius <= 0.0F) return rejectInvalid("glow.radius");
    if (!visible(color)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::RoundedGlow;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = glowRadius;
    append(command);
}

void Canvas::roundedOutline(
    Rect rect,
    Color color,
    float rounding,
    float thickness) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::RoundedOutline;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = std::min(thickness, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::sdfIcon(
    TextureHandle texture,
    Rect rect,
    Rect sourceUv,
    Color tint,
    float edge,
    float softness) noexcept {
    if (!texture.valid()) return rejectInvalid("texture");
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateRect(sourceUv, "uv"); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateColor(tint); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(edge) || edge < 0.0F || edge > 1.0F) return rejectInvalid("sdf.edge");
    if (!std::isfinite(softness) || softness <= 0.0F || softness > 0.5F) {
        return rejectInvalid("sdf.softness");
    }
    if (!visible(tint)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::SdfIcon;
    command.texture = texture;
    command.bounds = rect;
    command.uv = sourceUv;
    command.color = tint;
    command.radius = edge;
    command.thickness = softness;
    append(command);
}

void Canvas::effectRect(
    Rect rect,
    float rounding,
    std::span<const EffectLayer> layers) noexcept {
    for (const EffectLayer& layer : layers) {
        if (!layer.enabled) continue;
        switch (layer.kind) {
        case EffectLayerKind::Tint:
            tintRect(rect, layer.color, rounding);
            break;
        case EffectLayerKind::Gradient:
            gradientRect(rect, layer.color, layer.secondaryColor, layer.vector, rounding);
            break;
        case EffectLayerKind::AnimatedGradient:
            animatedGradientRect(
                rect, layer.color, layer.secondaryColor, layer.vector, layer.phase, rounding);
            break;
        case EffectLayerKind::Glow:
            roundedGlow(rect, layer.color, rounding, layer.amount);
            break;
        case EffectLayerKind::SoftShadow:
            roundedShadow(rect, layer.color, rounding, layer.amount, layer.vector);
            break;
        case EffectLayerKind::Outline:
            roundedOutline(rect, layer.color, rounding, layer.amount);
            break;
        default:
            rejectInvalid("effect.kind");
            break;
        }
    }
}

void Canvas::roundedShadow(
    Rect rect,
    Color color,
    float rounding,
    float blurRadius,
    Vec2 offset) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(rounding)) return rejectInvalid("rounding");
    if (!std::isfinite(blurRadius) || blurRadius <= 0.0F) return rejectInvalid("shadow.blurRadius");
    if (!std::isfinite(offset.x)) return rejectInvalid("shadow.offset.x");
    if (!std::isfinite(offset.y)) return rejectInvalid("shadow.offset.y");
    if (!visible(color)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::RoundedShadow;
    command.bounds = rect;
    command.uv = {offset, {0.0F, 0.0F}};
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = blurRadius;
    append(command);
}

void Canvas::border(
    Rect rect,
    Color color,
    CornerRadii radii,
    float thickness) noexcept {
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    const std::array values{radii.topLeft, radii.topRight, radii.bottomRight, radii.bottomLeft};
    for (float value : values) {
        if (!std::isfinite(value) || value < 0.0F) return rejectInvalid("border.radii");
    }
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    radii = normalizedRadii(rect, radii);
    DrawCommand command{};
    command.kind = PrimitiveKind::BorderRadii;
    command.bounds = rect;
    command.uv = {{radii.topLeft, radii.topRight}, {radii.bottomRight, radii.bottomLeft}};
    command.color = color;
    command.thickness = std::min(thickness, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::ninePatch(
    TextureHandle texture,
    Rect rect,
    Rect sourceUv,
    float destinationBorder,
    float sourceBorder,
    Color tint) noexcept {
    if (!texture.valid()) return rejectInvalid("texture");
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateRect(sourceUv, "uv"); !issue.empty()) {
        return rejectInvalid(issue);
    }
    if (const std::string_view issue = validateColor(tint); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(destinationBorder) || destinationBorder <= 0.0F) {
        return rejectInvalid("ninePatch.destinationBorder");
    }
    if (!std::isfinite(sourceBorder) || sourceBorder <= 0.0F || sourceBorder >= 0.5F) {
        return rejectInvalid("ninePatch.sourceBorder");
    }
    if (!visible(tint)) return rejectInvalid("color.alpha");
    DrawCommand command{};
    command.kind = PrimitiveKind::NinePatch;
    command.texture = texture;
    command.bounds = rect;
    command.uv = sourceUv;
    command.color = tint;
    command.radius = std::min(destinationBorder, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = sourceBorder;
    append(command);
}

void Canvas::image(TextureHandle texture, Rect rect, Color tint) noexcept {
    if (!texture.valid()) return rejectInvalid("texture");
    if (const std::string_view issue = validateRect(rect, "bounds"); !issue.empty()) return rejectInvalid(issue);
    if (const std::string_view issue = validateColor(tint); !issue.empty()) return rejectInvalid(issue);
    if (!visible(tint)) return rejectInvalid("color.alpha");

    DrawCommand command{};
    command.kind = PrimitiveKind::Image;
    command.texture = texture;
    command.bounds = rect;
    command.color = tint;
    append(command);
}

void Canvas::glyphs(
    TextureHandle atlas,
    std::span<const GlyphQuad> glyphQuads,
    Color color) noexcept {
    glyphs(atlas, {}, glyphQuads, color);
}

void Canvas::glyphs(
    TextureHandle atlas,
    Vec2 origin,
    std::span<const GlyphQuad> glyphQuads,
    Color color) noexcept {
    if (!atlas.valid()) return rejectInvalid("atlas");
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(origin.x)) return rejectInvalid("origin.x");
    if (!std::isfinite(origin.y)) return rejectInvalid("origin.y");
    if (!visible(color)) return rejectInvalid("color.alpha");

    for (const GlyphQuad& glyph : glyphQuads) {
        std::string_view issue = validateRect(glyph.bounds, "glyph.bounds");
        if (issue.empty()) {
            issue = validateRect(glyph.uv, "glyph.uv");
        }
        if (issue.empty()
            && glyph.rasterPlacement != GlyphRasterPlacement::Smooth
            && glyph.rasterPlacement != GlyphRasterPlacement::PixelAligned) {
            issue = "glyph.rasterPlacement";
        }
        if (!issue.empty()) {
            ++mRejectedCommands;
            ++mInvalidInputCommands;
            mLastError = issue;
            continue;
        }

        DrawCommand command{};
        command.kind = PrimitiveKind::Glyph;
        command.texture = atlas;
        command.bounds = {
            {glyph.bounds.min.x + origin.x, glyph.bounds.min.y + origin.y},
            {glyph.bounds.max.x + origin.x, glyph.bounds.max.y + origin.y},
        };
        command.uv = glyph.uv;
        command.color = color;
        // Smooth glyphs snap the shared run origin. Pixel-aligned coverage
        // glyphs instead snap each cumulative glyph position in the shader.
        command.radius = origin.x;
        command.thickness = origin.y;
        command.lineFlags = glyph.rasterPlacement == GlyphRasterPlacement::PixelAligned ? 1U : 0U;
        append(command);
    }
}

void Canvas::setBlendMode(BlendMode blendMode) noexcept { mBlendMode = blendMode; }

BlendMode Canvas::blendMode() const noexcept { return mBlendMode; }

std::size_t Canvas::clipDepth() const noexcept { return mClipDepth; }

std::uint64_t Canvas::rejectedCommands() const noexcept { return mRejectedCommands; }

std::uint64_t Canvas::clippedCommands() const noexcept { return mClippedCommands; }

std::uint64_t Canvas::invalidInputCommands() const noexcept { return mInvalidInputCommands; }

std::uint64_t Canvas::capacityRejectedCommands() const noexcept {
    return mCapacityRejectedCommands;
}

std::string_view Canvas::lastError() const noexcept { return mLastError; }

void Canvas::rejectInvalid(std::string_view field) noexcept {
    ++mRejectedCommands;
    ++mInvalidInputCommands;
    mLastError = field;
}

ClipRect Canvas::currentClip() const noexcept {
    return mClipDepth == 0 ? ClipRect{} : mClips[mClipDepth - 1].clip;
}

bool Canvas::commandOverlapsCurrentClip(const DrawCommand& command) const noexcept {
    if (mClipDepth == 0) return true;
    const ClipEntry& entry = mClips[mClipDepth - 1];
    if (entry.empty) return false;
    return commandOverlapsClip(command);
}

void Canvas::append(DrawCommand command) noexcept {
    command.blend = mBlendMode;
    command.clip = currentClip();
    if (const std::string_view issue = validateDrawCommand(command); !issue.empty()) {
        rejectInvalid(issue);
        return;
    }
    if (!commandOverlapsCurrentClip(command)) {
        ++mRejectedCommands;
        ++mClippedCommands;
        mLastError = {};
        return;
    }
    if (!mDisplayList->append(command)) {
        ++mRejectedCommands;
        ++mCapacityRejectedCommands;
        mLastError = "displayList.capacity";
    } else {
        mLastError = {};
    }
}

} // namespace henia::ui
