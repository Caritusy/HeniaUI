#include "henia/ui/Canvas.h"
#include "henia/ui/Validation.h"

#include <algorithm>
#include <cmath>

namespace henia::ui {
namespace {

[[nodiscard]] bool visible(Color color) noexcept { return color.alpha > 0.0F; }

[[nodiscard]] Rect lineBounds(Vec2 from, Vec2 to, float thickness) noexcept {
    const float half = thickness * 0.5F;
    return {
        {std::min(from.x, to.x) - half, std::min(from.y, to.y) - half},
        {std::max(from.x, to.x) + half, std::max(from.y, to.y) + half},
    };
}

} // namespace

Canvas::Canvas(DisplayList& displayList) noexcept : mDisplayList(&displayList) {}

void Canvas::reset() noexcept {
    mClipDepth = 0;
    mBlendMode = BlendMode::PremultipliedAlpha;
    mRejectedCommands = 0;
    mInvalidInputCommands = 0;
    mCapacityRejectedCommands = 0;
    mLastError = {};
}

bool Canvas::pushClip(Rect rect) noexcept {
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

    if (mClipDepth != 0) {
        rect = intersect(mClips[mClipDepth - 1].area, rect);
        if (!rect.valid()) {
            ++mRejectedCommands;
            ++mInvalidInputCommands;
            mLastError = "clip.intersection";
            return false;
        }
    }

    mClips[mClipDepth++] = ClipRect{rect, true};
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
    mLastError = {};
    return true;
}

void Canvas::line(Vec2 from, Vec2 to, Color color, float thickness) noexcept {
    if (!std::isfinite(from.x)) return rejectInvalid("from.x");
    if (!std::isfinite(from.y)) return rejectInvalid("from.y");
    if (!std::isfinite(to.x)) return rejectInvalid("to.x");
    if (!std::isfinite(to.y)) return rejectInvalid("to.y");
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    if (from == to) return rejectInvalid("line.endpoints");

    DrawCommand command{};
    command.kind = PrimitiveKind::Line;
    command.bounds = lineBounds(from, to, thickness);
    command.pointA = from;
    command.pointB = to;
    command.color = color;
    command.thickness = thickness;
    append(command);
}

void Canvas::polyline(
    std::span<const Vec2> points,
    Color color,
    float thickness,
    bool closed) noexcept {
    if (points.size() < 2) return rejectInvalid("points.size");
    if (const std::string_view issue = validateColor(color); !issue.empty()) return rejectInvalid(issue);
    if (!std::isfinite(thickness) || thickness <= 0.0F) return rejectInvalid("thickness");
    if (!visible(color)) return rejectInvalid("color.alpha");
    for (const Vec2 point : points) {
        if (!std::isfinite(point.x)) return rejectInvalid("points.x");
        if (!std::isfinite(point.y)) return rejectInvalid("points.y");
    }

    for (std::size_t index = 1; index < points.size(); ++index) {
        line(points[index - 1], points[index], color, thickness);
    }
    if (closed && points.front() != points.back()) {
        line(points.back(), points.front(), color, thickness);
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
        append(command);
    }
}

void Canvas::setBlendMode(BlendMode blendMode) noexcept { mBlendMode = blendMode; }

BlendMode Canvas::blendMode() const noexcept { return mBlendMode; }

std::size_t Canvas::clipDepth() const noexcept { return mClipDepth; }

std::uint64_t Canvas::rejectedCommands() const noexcept { return mRejectedCommands; }

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
    return mClipDepth == 0 ? ClipRect{} : mClips[mClipDepth - 1];
}

void Canvas::append(DrawCommand command) noexcept {
    command.blend = mBlendMode;
    command.clip = currentClip();
    if (const std::string_view issue = validateDrawCommand(command); !issue.empty()) {
        rejectInvalid(issue);
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
