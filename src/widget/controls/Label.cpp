#include "henia/ui/widget/controls/Label.h"

#include <utility>

namespace henia::ui {

Label::Label(std::string textValue, LabelStyle style)
    : Widget(WidgetKind::Label), mText(std::move(textValue)), mStyle(style) {}

void Label::setText(std::string textValue) {
    if (mText == textValue) {
        return;
    }
    mText = std::move(textValue);
    markLayoutDirty();
}

std::string_view Label::text() const noexcept { return mText; }

void Label::setStyle(LabelStyle styleValue) noexcept {
    mStyle = styleValue;
    markLayoutDirty();
}

const LabelStyle& Label::style() const noexcept { return mStyle; }

Vec2 Label::onMeasure(TextPainter& textPainter, Constraints) {
    const TextMetrics metrics = textPainter.measure(mStyle.font, mStyle.size, mText);
    return {metrics.width, metrics.height};
}

void Label::onPaint(Canvas& canvas, TextPainter& textPainter, const Theme&) {
    textPainter.draw(canvas, mStyle.font, mStyle.size, frame().min, mStyle.color, mText);
}

} // namespace henia::ui
