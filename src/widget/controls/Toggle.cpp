#include "henia/ui/widget/controls/Toggle.h"

#include <algorithm>
#include <utility>

namespace henia::ui {
namespace {

[[nodiscard]] bool activation(const InputEvent& event, const Widget& widget) noexcept {
    return event.kind == InputEventKind::KeyDown && widget.focused()
        && (event.key == KeyCode::Enter || event.key == KeyCode::Space);
}

[[nodiscard]] bool pointerActivation(const InputEvent& event, const Widget& widget) noexcept {
    return event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary
        && event.position.x >= widget.frame().min.x && event.position.y >= widget.frame().min.y
        && event.position.x < widget.frame().max.x && event.position.y < widget.frame().max.y;
}

[[nodiscard]] Vec2 toggleMeasure(
    TextPainter& painter,
    const ToggleStyle& style,
    std::string_view text,
    float indicatorWidth) {
    const TextMetrics metrics = painter.measure(style.font, style.fontSize, text);
    return {
        indicatorWidth + (text.empty() ? 0.0F : style.gap + metrics.width)
            + style.padding * 2.0F,
        std::max(style.indicatorSize, metrics.height) + style.padding * 2.0F,
    };
}

} // namespace

Checkbox::Checkbox(std::string text, bool checked, ToggleStyle style)
    : Widget(WidgetKind::Checkbox), mText(std::move(text)), mStyle(style), mChecked(checked) {}

void Checkbox::setText(std::string text) {
    if (mText == text) return;
    mText = std::move(text);
    markLayoutDirty();
}
std::string_view Checkbox::text() const noexcept { return mText; }
void Checkbox::setChecked(bool checked) noexcept {
    if (mChecked == checked) return;
    mChecked = checked;
    markPaintDirty();
}
bool Checkbox::checked() const noexcept { return mChecked; }
void Checkbox::setStyle(ToggleStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void Checkbox::setOnChanged(Callback<bool> callback) noexcept { mOnChanged = callback; }
bool Checkbox::acceptsPointerInput() const noexcept { return true; }
bool Checkbox::acceptsKeyboardFocus() const noexcept { return true; }

bool Checkbox::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (activation(event, *this) || pointerActivation(event, *this)) {
        activate();
        return true;
    }
    return event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary;
}

Vec2 Checkbox::onMeasure(TextPainter& text, Constraints) {
    return toggleMeasure(text, mStyle, mText, mStyle.indicatorSize);
}

void Checkbox::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    const float size = std::max(mStyle.indicatorSize, 0.0F);
    const Rect indicator{
        {frame().min.x + mStyle.padding,
         frame().min.y + std::max((frame().height() - size) * 0.5F, 0.0F)},
        {frame().min.x + mStyle.padding + size,
         frame().min.y + std::max((frame().height() - size) * 0.5F, 0.0F) + size},
    };
    canvas.fillRect(indicator, mChecked ? mStyle.active : mStyle.background, 4.0F);
    canvas.strokeRect(indicator, focused() ? mStyle.focus : mStyle.border, 4.0F, 1.0F);
    if (mChecked) {
        const float x0 = indicator.min.x + size * 0.22F;
        const float y0 = indicator.min.y + size * 0.52F;
        canvas.line({x0, y0}, {indicator.min.x + size * 0.43F, indicator.min.y + size * 0.72F},
            mStyle.mark, 2.0F, LineCap::Round);
        canvas.line({indicator.min.x + size * 0.43F, indicator.min.y + size * 0.72F},
            {indicator.min.x + size * 0.80F, indicator.min.y + size * 0.29F},
            mStyle.mark, 2.0F, LineCap::Round);
    }
    const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, mText);
    text.draw(canvas, mStyle.font, mStyle.fontSize,
        {indicator.max.x + mStyle.gap,
         frame().min.y + std::max((frame().height() - metrics.height) * 0.5F, 0.0F)},
        mStyle.textColor, mText);
}

void Checkbox::activate() {
    mChecked = !mChecked;
    markPaintDirty();
    mOnChanged(mChecked);
}

Toggle::Toggle(std::string text, bool checked, ToggleStyle style)
    : Widget(WidgetKind::Toggle), mText(std::move(text)), mStyle(style), mChecked(checked) {}

void Toggle::setText(std::string text) {
    if (mText == text) return;
    mText = std::move(text);
    markLayoutDirty();
}
std::string_view Toggle::text() const noexcept { return mText; }
void Toggle::setChecked(bool checked) noexcept {
    if (mChecked == checked) return;
    mChecked = checked;
    markPaintDirty();
}
bool Toggle::checked() const noexcept { return mChecked; }
void Toggle::setStyle(ToggleStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void Toggle::setOnChanged(Callback<bool> callback) noexcept { mOnChanged = callback; }
bool Toggle::acceptsPointerInput() const noexcept { return true; }
bool Toggle::acceptsKeyboardFocus() const noexcept { return true; }

bool Toggle::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (activation(event, *this) || pointerActivation(event, *this)) {
        activate();
        return true;
    }
    return event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary;
}

Vec2 Toggle::onMeasure(TextPainter& text, Constraints) {
    return toggleMeasure(text, mStyle, mText, mStyle.indicatorSize * 1.8F);
}

void Toggle::onPaint(Canvas& canvas, TextPainter& text, const Theme&) {
    const float height = std::max(mStyle.indicatorSize, 0.0F);
    const float width = height * 1.8F;
    const float y = frame().min.y + std::max((frame().height() - height) * 0.5F, 0.0F);
    const Rect track{{frame().min.x + mStyle.padding, y},
                     {frame().min.x + mStyle.padding + width, y + height}};
    canvas.fillRect(track, mChecked ? mStyle.active : mStyle.background, height * 0.5F);
    canvas.strokeRect(track, focused() ? mStyle.focus : mStyle.border, height * 0.5F, 1.0F);
    const float knobRadius = std::max(height * 0.5F - 3.0F, 0.0F);
    const float knobX = mChecked ? track.max.x - height * 0.5F : track.min.x + height * 0.5F;
    canvas.circle({knobX, y + height * 0.5F}, knobRadius, mStyle.mark);
    const TextMetrics metrics = text.measure(mStyle.font, mStyle.fontSize, mText);
    text.draw(canvas, mStyle.font, mStyle.fontSize,
        {track.max.x + mStyle.gap,
         frame().min.y + std::max((frame().height() - metrics.height) * 0.5F, 0.0F)},
        mStyle.textColor, mText);
}

void Toggle::activate() {
    mChecked = !mChecked;
    markPaintDirty();
    mOnChanged(mChecked);
}

} // namespace henia::ui
