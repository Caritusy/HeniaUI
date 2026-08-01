#include "henia/ui/widget/controls/Panel.h"

#include <algorithm>

namespace henia::ui {
namespace {

[[nodiscard]] float horizontal(const Insets& insets) noexcept { return insets.left + insets.right; }
[[nodiscard]] float vertical(const Insets& insets) noexcept { return insets.top + insets.bottom; }

} // namespace

Panel::Panel(PanelStyle style) noexcept : Widget(WidgetKind::Panel), mStyle(style) {}

void Panel::setStyle(PanelStyle styleValue) noexcept {
    mStyle = styleValue;
    markLayoutDirty();
}

const PanelStyle& Panel::style() const noexcept { return mStyle; }

Vec2 Panel::onMeasure(TextPainter& text, Constraints constraints) {
    Vec2 result{horizontal(mStyle.padding), vertical(mStyle.padding)};
    float main = 0.0F;
    float cross = 0.0F;
    std::size_t visibleChildren = 0;
    const Constraints childConstraints{
        {},
        {
            std::max(0.0F, constraints.maximum.x - horizontal(mStyle.padding)),
            std::max(0.0F, constraints.maximum.y - vertical(mStyle.padding)),
        },
    };
    for (const std::unique_ptr<Widget>& child : mChildren) {
        if (!child->visible()) {
            continue;
        }
        const Vec2 size = child->measure(text, childConstraints);
        const LayoutParameters& layout = child->layoutParameters();
        if (mStyle.direction == LayoutDirection::Column) {
            main += size.y + layout.margin.top + layout.margin.bottom;
            cross = std::max(cross, size.x + layout.margin.left + layout.margin.right);
        } else {
            main += size.x + layout.margin.left + layout.margin.right;
            cross = std::max(cross, size.y + layout.margin.top + layout.margin.bottom);
        }
        ++visibleChildren;
    }
    if (visibleChildren > 1) {
        main += static_cast<float>(visibleChildren - 1) * mStyle.gap;
    }
    if (mStyle.direction == LayoutDirection::Column) {
        result.x += cross;
        result.y += main;
    } else {
        result.x += main;
        result.y += cross;
    }
    return result;
}

void Panel::onArrange(TextPainter& text, Rect arrangedFrame) {
    const Rect content{
        {arrangedFrame.min.x + mStyle.padding.left, arrangedFrame.min.y + mStyle.padding.top},
        {arrangedFrame.max.x - mStyle.padding.right, arrangedFrame.max.y - mStyle.padding.bottom},
    };
    const Vec2 contentSize{std::max(content.width(), 0.0F), std::max(content.height(), 0.0F)};
    const Constraints childConstraints{{}, contentSize};

    float occupied = 0.0F;
    float totalGrow = 0.0F;
    std::size_t visibleChildren = 0;
    for (const std::unique_ptr<Widget>& child : mChildren) {
        if (!child->visible()) {
            continue;
        }
        const Vec2 measured = child->measure(text, childConstraints);
        const LayoutParameters& layout = child->layoutParameters();
        occupied += mStyle.direction == LayoutDirection::Column
            ? measured.y + layout.margin.top + layout.margin.bottom
            : measured.x + layout.margin.left + layout.margin.right;
        totalGrow += std::max(layout.flexGrow, 0.0F);
        ++visibleChildren;
    }
    if (visibleChildren > 1) {
        occupied += static_cast<float>(visibleChildren - 1) * mStyle.gap;
    }
    const float availableMain = mStyle.direction == LayoutDirection::Column ? contentSize.y : contentSize.x;
    const float flexible = std::max(availableMain - occupied, 0.0F);
    float cursor = mStyle.direction == LayoutDirection::Column ? content.min.y : content.min.x;

    for (const std::unique_ptr<Widget>& child : mChildren) {
        if (!child->visible()) {
            continue;
        }
        const Vec2 measured = child->measure(text, childConstraints);
        const LayoutParameters& layout = child->layoutParameters();
        const float growth = totalGrow > 0.0F
            ? flexible * std::max(layout.flexGrow, 0.0F) / totalGrow
            : 0.0F;
        Rect childFrame{};
        if (mStyle.direction == LayoutDirection::Column) {
            cursor += layout.margin.top;
            const float width = mStyle.stretchCrossAxis
                ? std::max(contentSize.x - layout.margin.left - layout.margin.right, 0.0F)
                : measured.x;
            childFrame = {
                {content.min.x + layout.margin.left, cursor},
                {content.min.x + layout.margin.left + width, cursor + measured.y + growth},
            };
            cursor = childFrame.max.y + layout.margin.bottom + mStyle.gap;
        } else {
            cursor += layout.margin.left;
            const float height = mStyle.stretchCrossAxis
                ? std::max(contentSize.y - layout.margin.top - layout.margin.bottom, 0.0F)
                : measured.y;
            childFrame = {
                {cursor, content.min.y + layout.margin.top},
                {cursor + measured.x + growth, content.min.y + layout.margin.top + height},
            };
            cursor = childFrame.max.x + layout.margin.right + mStyle.gap;
        }
        child->arrange(text, childFrame);
    }
}

void Panel::onPaint(Canvas& canvas, TextPainter&, const Theme&) {
    if (mStyle.background.alpha > 0.0F) {
        canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    }
    if (mStyle.border.alpha > 0.0F && mStyle.borderWidth > 0.0F) {
        canvas.strokeRect(frame(), mStyle.border, mStyle.radius, mStyle.borderWidth);
    }
}

} // namespace henia::ui
