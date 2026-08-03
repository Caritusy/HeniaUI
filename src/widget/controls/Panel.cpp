#include "henia/ui/widget/controls/Panel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace henia::ui {
namespace {

[[nodiscard]] float layoutValue(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] float boundedAdd(float left, float right) noexcept {
    const double result = static_cast<double>(left) + right;
    return static_cast<float>(std::min(
        result, static_cast<double>(std::numeric_limits<float>::max())));
}

[[nodiscard]] float horizontal(const Insets& insets) noexcept {
    return boundedAdd(layoutValue(insets.left), layoutValue(insets.right));
}

[[nodiscard]] float vertical(const Insets& insets) noexcept {
    return boundedAdd(layoutValue(insets.top), layoutValue(insets.bottom));
}

[[nodiscard]] float mainSize(Vec2 size, LayoutDirection direction) noexcept {
    return direction == LayoutDirection::Column ? size.y : size.x;
}

[[nodiscard]] float crossSize(Vec2 size, LayoutDirection direction) noexcept {
    return direction == LayoutDirection::Column ? size.x : size.y;
}

[[nodiscard]] float mainMargin(const Insets& margin, LayoutDirection direction) noexcept {
    return direction == LayoutDirection::Column ? vertical(margin) : horizontal(margin);
}

[[nodiscard]] float crossMargin(const Insets& margin, LayoutDirection direction) noexcept {
    return direction == LayoutDirection::Column ? horizontal(margin) : vertical(margin);
}

[[nodiscard]] Constraints childConstraints(
    LayoutDirection direction,
    float maximumMain,
    float maximumCross) noexcept {
    return direction == LayoutDirection::Column
        ? Constraints{{}, {maximumCross, maximumMain}}
        : Constraints{{}, {maximumMain, maximumCross}};
}

[[nodiscard]] Constraints exactChildConstraints(
    LayoutDirection direction,
    float main,
    float cross) noexcept {
    const Vec2 size = direction == LayoutDirection::Column
        ? Vec2{cross, main} : Vec2{main, cross};
    return {size, size};
}

[[nodiscard]] std::size_t visibleChildCount(
    std::span<const std::unique_ptr<Widget>> children) noexcept {
    return static_cast<std::size_t>(std::count_if(
        children.begin(), children.end(), [](const auto& child) { return child->visible(); }));
}

[[nodiscard]] float gapBudget(std::size_t childCount, float gap) noexcept {
    if (childCount < 2) {
        return 0.0F;
    }
    const double result = static_cast<double>(childCount - 1U) * layoutValue(gap);
    return static_cast<float>(std::min(
        result, static_cast<double>(std::numeric_limits<float>::max())));
}

struct ResolvedPanelStyle final {
    Color background{};
    Color border{};
    float borderWidth = 0.0F;
    float radius = 0.0F;
    Insets padding{};
    float gap = 0.0F;
    LayoutDirection direction = LayoutDirection::Column;
    bool stretchCrossAxis = true;
};

[[nodiscard]] ResolvedPanelStyle resolve(
    const PanelStyle& style,
    const Theme& theme) noexcept {
    const float scale = layoutValue(theme.scale);
    const float padding = layoutValue(theme.panelPadding) * scale;
    return {
        .background = style.background.value_or(theme.panelBackground),
        .border = style.border.value_or(theme.panelBorder),
        .borderWidth = style.borderWidth.value_or(theme.borderWidth * scale),
        .radius = style.radius.value_or(theme.cornerRadius * scale),
        .padding = style.padding.value_or(Insets{padding, padding, padding, padding}),
        .gap = style.gap.value_or(theme.panelGap * scale),
        .direction = style.direction,
        .stretchCrossAxis = style.stretchCrossAxis,
    };
}

} // namespace

Panel::Panel(PanelStyle style) noexcept : Widget(WidgetKind::Panel), mStyle(style) {}

void Panel::setStyle(PanelStyle styleValue) noexcept {
    const bool unchanged = mStyle.background == styleValue.background
        && mStyle.border == styleValue.border
        && mStyle.borderWidth == styleValue.borderWidth
        && mStyle.radius == styleValue.radius
        && mStyle.padding == styleValue.padding
        && mStyle.gap == styleValue.gap
        && mStyle.direction == styleValue.direction
        && mStyle.stretchCrossAxis == styleValue.stretchCrossAxis;
    if (unchanged) {
        return;
    }
    const bool layoutChanged = !(mStyle.padding == styleValue.padding)
        || mStyle.gap != styleValue.gap
        || mStyle.direction != styleValue.direction
        || mStyle.stretchCrossAxis != styleValue.stretchCrossAxis;
    mStyle = styleValue;
    if (layoutChanged) {
        markLayoutDirty();
    } else {
        markPaintDirty();
    }
}

const PanelStyle& Panel::style() const noexcept { return mStyle; }

Vec2 Panel::onMeasure(TextPainter& text, Constraints constraints) {
    const ResolvedPanelStyle style = resolve(mStyle, inheritedTheme());
    const float horizontalPadding = horizontal(style.padding);
    const float verticalPadding = vertical(style.padding);
    const Vec2 contentMaximum{
        std::max(constraints.maximum.x - horizontalPadding, 0.0F),
        std::max(constraints.maximum.y - verticalPadding, 0.0F),
    };
    const float availableMain = mainSize(contentMaximum, style.direction);
    const float availableCross = crossSize(contentMaximum, style.direction);
    const std::size_t visibleChildren = visibleChildCount(mChildren);
    float main = gapBudget(visibleChildren, style.gap);
    float cross = 0.0F;
    float remainingMain = std::max(availableMain - main, 0.0F);
    for (const std::unique_ptr<Widget>& child : mChildren) {
        if (!child->visible()) {
            continue;
        }
        const LayoutParameters& layout = child->layoutParameters();
        const float childMainMargin = mainMargin(layout.margin, style.direction);
        const float childCrossMargin = crossMargin(layout.margin, style.direction);
        const Vec2 size = child->measure(text, childConstraints(
            style.direction,
            std::max(remainingMain - childMainMargin, 0.0F),
            std::max(availableCross - childCrossMargin, 0.0F)));
        const float consumed = boundedAdd(mainSize(size, style.direction), childMainMargin);
        main = boundedAdd(main, consumed);
        remainingMain = std::max(remainingMain - consumed, 0.0F);
        cross = std::max(
            cross,
            boundedAdd(crossSize(size, style.direction), childCrossMargin));
    }
    if (style.direction == LayoutDirection::Column) {
        return {boundedAdd(horizontalPadding, cross), boundedAdd(verticalPadding, main)};
    }
    return {boundedAdd(horizontalPadding, main), boundedAdd(verticalPadding, cross)};
}

void Panel::onArrange(TextPainter& text, Rect arrangedFrame) {
    const ResolvedPanelStyle style = resolve(mStyle, inheritedTheme());
    const float frameWidth = std::max(arrangedFrame.width(), 0.0F);
    const float frameHeight = std::max(arrangedFrame.height(), 0.0F);
    const float leftPadding = std::min(layoutValue(style.padding.left), frameWidth);
    const float topPadding = std::min(layoutValue(style.padding.top), frameHeight);
    const float rightPadding = std::min(
        layoutValue(style.padding.right), frameWidth - leftPadding);
    const float bottomPadding = std::min(
        layoutValue(style.padding.bottom), frameHeight - topPadding);
    const Vec2 contentSize{
        frameWidth - leftPadding - rightPadding,
        frameHeight - topPadding - bottomPadding,
    };
    const Rect content{
        {arrangedFrame.min.x + leftPadding, arrangedFrame.min.y + topPadding},
        {arrangedFrame.min.x + leftPadding + contentSize.x,
         arrangedFrame.min.y + topPadding + contentSize.y},
    };
    const float availableMain = mainSize(contentSize, style.direction);
    const float availableCross = crossSize(contentSize, style.direction);
    const std::size_t visibleChildren = visibleChildCount(mChildren);
    const float gap = layoutValue(style.gap);
    const float gaps = gapBudget(visibleChildren, gap);
    float occupied = gaps;
    float remainingMain = std::max(availableMain - gaps, 0.0F);
    float totalGrow = 0.0F;
    for (const std::unique_ptr<Widget>& child : mChildren) {
        if (!child->visible()) {
            continue;
        }
        const LayoutParameters& layout = child->layoutParameters();
        const float childMainMargin = mainMargin(layout.margin, style.direction);
        const float childCrossMargin = crossMargin(layout.margin, style.direction);
        const Vec2 measured = child->measure(text, childConstraints(
            style.direction,
            std::max(remainingMain - childMainMargin, 0.0F),
            std::max(availableCross - childCrossMargin, 0.0F)));
        const float consumed = boundedAdd(mainSize(measured, style.direction), childMainMargin);
        occupied = boundedAdd(occupied, consumed);
        remainingMain = std::max(remainingMain - consumed, 0.0F);
        totalGrow = boundedAdd(totalGrow, layoutValue(layout.flexGrow));
    }
    const float flexible = std::max(availableMain - occupied, 0.0F);
    float cursor = style.direction == LayoutDirection::Column ? content.min.y : content.min.x;
    const float contentMainEnd = style.direction == LayoutDirection::Column
        ? content.max.y : content.max.x;
    remainingMain = std::max(availableMain - gaps, 0.0F);

    for (const std::unique_ptr<Widget>& child : mChildren) {
        if (!child->visible()) {
            continue;
        }
        const LayoutParameters& layout = child->layoutParameters();
        const float beforeMain = style.direction == LayoutDirection::Column
            ? layoutValue(layout.margin.top) : layoutValue(layout.margin.left);
        const float afterMain = style.direction == LayoutDirection::Column
            ? layoutValue(layout.margin.bottom) : layoutValue(layout.margin.right);
        const float beforeCross = style.direction == LayoutDirection::Column
            ? layoutValue(layout.margin.left) : layoutValue(layout.margin.top);
        const float childMainMargin = boundedAdd(beforeMain, afterMain);
        const float childCrossMargin = crossMargin(layout.margin, style.direction);
        const float maximumBaseMain = std::max(remainingMain - childMainMargin, 0.0F);
        const float maximumCross = std::max(availableCross - childCrossMargin, 0.0F);
        const Vec2 measured = child->measure(text, childConstraints(
            style.direction, maximumBaseMain, maximumCross));
        const float baseMain = mainSize(measured, style.direction);
        remainingMain = std::max(
            remainingMain - boundedAdd(baseMain, childMainMargin), 0.0F);
        const float growth = totalGrow > 0.0F
            ? static_cast<float>(static_cast<double>(flexible)
                * layoutValue(layout.flexGrow) / totalGrow)
            : 0.0F;
        const bool explicitCross = style.direction == LayoutDirection::Column
            ? layout.width >= 0.0F : layout.height >= 0.0F;
        const float finalMain = baseMain + growth;
        const float finalCross = style.stretchCrossAxis && !explicitCross
            ? maximumCross
            : std::min(crossSize(measured, style.direction), maximumCross);
        const Vec2 finalSize = child->measure(text, exactChildConstraints(
            style.direction, finalMain, finalCross));
        cursor = std::min(cursor + beforeMain, contentMainEnd);
        Rect childFrame{};
        if (style.direction == LayoutDirection::Column) {
            childFrame = {
                {content.min.x + beforeCross, cursor},
                {content.min.x + beforeCross + finalSize.x,
                 std::min(cursor + finalSize.y, content.max.y)},
            };
            cursor = std::min(childFrame.max.y + afterMain + gap, content.max.y);
        } else {
            childFrame = {
                {cursor, content.min.y + beforeCross},
                {std::min(cursor + finalSize.x, content.max.x),
                 content.min.y + beforeCross + finalSize.y},
            };
            cursor = std::min(childFrame.max.x + afterMain + gap, content.max.x);
        }
        child->arrange(text, childFrame);
    }
}

void Panel::onPaint(Canvas& canvas, TextPainter&, const Theme& theme) {
    const ResolvedPanelStyle style = resolve(mStyle, theme);
    if (style.background.alpha > 0.0F) {
        canvas.fillRect(frame(), style.background, style.radius);
    }
    if (style.border.alpha > 0.0F && style.borderWidth > 0.0F) {
        canvas.strokeRect(frame(), style.border, style.radius, style.borderWidth);
    }
}

} // namespace henia::ui
