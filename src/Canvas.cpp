#include "henia/ui/Canvas.h"

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
}

bool Canvas::pushClip(Rect rect) noexcept {
    if (!rect.valid() || mClipDepth == mClips.size()) {
        ++mRejectedCommands;
        return false;
    }

    if (mClipDepth != 0) {
        rect = intersect(mClips[mClipDepth - 1].area, rect);
        if (!rect.valid()) {
            ++mRejectedCommands;
            return false;
        }
    }

    mClips[mClipDepth++] = ClipRect{rect, true};
    return true;
}

bool Canvas::popClip() noexcept {
    if (mClipDepth == 0) {
        ++mRejectedCommands;
        return false;
    }
    --mClipDepth;
    return true;
}

void Canvas::line(Vec2 from, Vec2 to, Color color, float thickness) noexcept {
    if (!visible(color) || thickness <= 0.0F || from == to) {
        ++mRejectedCommands;
        return;
    }

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
    if (points.size() < 2 || !visible(color) || thickness <= 0.0F) {
        ++mRejectedCommands;
        return;
    }

    for (std::size_t index = 1; index < points.size(); ++index) {
        line(points[index - 1], points[index], color, thickness);
    }
    if (closed && points.front() != points.back()) {
        line(points.back(), points.front(), color, thickness);
    }
}

void Canvas::fillRect(Rect rect, Color color, float rounding) noexcept {
    if (!rect.valid() || !visible(color)) {
        ++mRejectedCommands;
        return;
    }

    DrawCommand command{};
    command.kind = PrimitiveKind::SolidRect;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::strokeRect(Rect rect, Color color, float rounding, float thickness) noexcept {
    if (!rect.valid() || !visible(color) || thickness <= 0.0F) {
        ++mRejectedCommands;
        return;
    }

    DrawCommand command{};
    command.kind = PrimitiveKind::StrokeRect;
    command.bounds = rect;
    command.color = color;
    command.radius = std::clamp(rounding, 0.0F, std::min(rect.width(), rect.height()) * 0.5F);
    command.thickness = std::min(thickness, std::min(rect.width(), rect.height()) * 0.5F);
    append(command);
}

void Canvas::image(TextureHandle texture, Rect rect, Color tint) noexcept {
    if (!texture.valid() || !rect.valid() || !visible(tint)) {
        ++mRejectedCommands;
        return;
    }

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
    if (!atlas.valid() || !visible(color)) {
        ++mRejectedCommands;
        return;
    }

    for (const GlyphQuad& glyph : glyphQuads) {
        if (!glyph.bounds.valid() || !glyph.uv.valid()) {
            ++mRejectedCommands;
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

ClipRect Canvas::currentClip() const noexcept {
    return mClipDepth == 0 ? ClipRect{} : mClips[mClipDepth - 1];
}

void Canvas::append(DrawCommand command) noexcept {
    command.blend = mBlendMode;
    command.clip = currentClip();
    mDisplayList->append(command);
}

} // namespace henia::ui
