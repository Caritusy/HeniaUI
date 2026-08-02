#include "henia/ui/widget/controls/Tooltip.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

Tooltip::Tooltip(std::string text, TooltipStyle style)
    : Widget(WidgetKind::Tooltip), mText(std::move(text)), mStyle(style) {}
void Tooltip::setText(std::string text) {
    if (mText == text) return;
    mText = std::move(text);
    markLayoutDirty();
}
std::string_view Tooltip::text() const noexcept { return mText; }
void Tooltip::setStyle(TooltipStyle style) noexcept { mStyle = style; markLayoutDirty(); }
Vec2 Tooltip::onMeasure(TextPainter& text, Constraints) {
    const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, mText);
    return {metrics.width + mStyle.padding.left + mStyle.padding.right,
            metrics.height + mStyle.padding.top + mStyle.padding.bottom};
}
void Tooltip::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    canvas.fillRect(frame(), mStyle.background, mStyle.radius);
    canvas.strokeRect(frame(), mStyle.border, mStyle.radius, 1.0F);
    text.draw(canvas, mStyle.font, mStyle.fontSize,
        {frame().min.x + mStyle.padding.left, frame().min.y + mStyle.padding.top},
        mStyle.text, mText);
}

} // namespace henia::ui
