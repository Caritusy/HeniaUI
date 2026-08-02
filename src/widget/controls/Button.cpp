#include "henia/ui/widget/controls/Button.h"

#include <algorithm>
#include <utility>

namespace henia::ui {

Button::Button(std::string textValue, ButtonStyle style)
    : Widget(WidgetKind::Button), mText(std::move(textValue)), mStyle(style) {}

void Button::setText(std::string textValue) {
    if (mText == textValue) {
        return;
    }
    mText = std::move(textValue);
    markLayoutDirty();
}

std::string_view Button::text() const noexcept { return mText; }

void Button::setStyle(ButtonStyle styleValue) noexcept {
    const bool unchanged = mStyle.font == styleValue.font
        && mStyle.fontSize == styleValue.fontSize
        && mStyle.textColor == styleValue.textColor
        && mStyle.background == styleValue.background
        && mStyle.hover == styleValue.hover
        && mStyle.pressed == styleValue.pressed
        && mStyle.border == styleValue.border
        && mStyle.borderWidth == styleValue.borderWidth
        && mStyle.radius == styleValue.radius
        && mStyle.padding == styleValue.padding;
    if (unchanged) {
        return;
    }
    const bool layoutChanged = mStyle.font != styleValue.font
        || mStyle.fontSize != styleValue.fontSize
        || !(mStyle.padding == styleValue.padding);
    mStyle = styleValue;
    if (layoutChanged) {
        markLayoutDirty();
    } else {
        markPaintDirty();
    }
}

void Button::setOnClick(Callback<> callback) noexcept { mOnClick = callback; }

bool Button::acceptsPointerInput() const noexcept { return true; }

bool Button::acceptsKeyboardFocus() const noexcept { return true; }

bool Button::handleInput(const InputEvent& event) {
    if (!enabled() || event.button != PointerButton::Primary) {
        return false;
    }
    if (event.kind == InputEventKind::PointerDown) {
        return true;
    }
    if (event.kind == InputEventKind::PointerUp) {
        if (contains(event.position)) {
            mOnClick();
        }
        return true;
    }
    return false;
}

Vec2 Button::onMeasure(TextPainter& textPainter, Constraints) {
    const TextMetrics metrics = textPainter.measure(mStyle.font, mStyle.fontSize, mText);
    return {
        metrics.width + mStyle.padding.left + mStyle.padding.right,
        metrics.height + mStyle.padding.top + mStyle.padding.bottom,
    };
}

void Button::onPaint(Canvas& canvas, TextPainter& textPainter, const Theme&) {
    const Color background = pressed() ? mStyle.pressed : (hovered() ? mStyle.hover : mStyle.background);
    canvas.fillRect(frame(), background, mStyle.radius);
    if (mStyle.borderWidth > 0.0F && mStyle.border.alpha > 0.0F) {
        canvas.strokeRect(frame(), mStyle.border, mStyle.radius, mStyle.borderWidth);
    }
    const TextMetrics metrics = textPainter.measure(mStyle.font, mStyle.fontSize, mText);
    const Vec2 origin{
        frame().min.x + std::max((frame().width() - metrics.width) * 0.5F, 0.0F),
        frame().min.y + std::max((frame().height() - metrics.height) * 0.5F, 0.0F),
    };
    textPainter.draw(canvas, mStyle.font, mStyle.fontSize, origin, mStyle.textColor, mText);
}

} // namespace henia::ui
