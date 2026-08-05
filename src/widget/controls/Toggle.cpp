#include "henia/ui/widget/controls/Toggle.h"

#include <algorithm>
#include <utility>

namespace henia::ui {
namespace {

[[nodiscard]] constexpr Color mixColor(
    Color first,
    Color second,
    float amount) noexcept {
    const float value = std::clamp(amount, 0.0F, 1.0F);
    return {
        first.red + (second.red - first.red) * value,
        first.green + (second.green - first.green) * value,
        first.blue + (second.blue - first.blue) * value,
        first.alpha + (second.alpha - first.alpha) * value,
    };
}

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
    const bool active = pressed();
    const bool hot = hovered() || active;
    if (hot) {
        Color halo = mStyle.focus;
        halo.alpha *= active ? 0.24F : 0.13F;
        canvas.roundedGlow(indicator, halo, 4.0F, active ? 5.0F : 3.0F);
    }
    const Color fill = mChecked ? mStyle.active : mStyle.background;
    canvas.fillRect(
        indicator,
        hot ? mixColor(fill, mStyle.focus, active ? 0.20F : 0.10F) : fill,
        4.0F);
    canvas.strokeRect(
        indicator,
        focused() || hot
            ? mixColor(mStyle.border, mStyle.focus, active ? 0.86F : 0.62F)
            : mStyle.border,
        4.0F,
        hot ? 1.5F : 1.0F);
    if (mChecked) {
        const float x0 = indicator.min.x + size * 0.22F;
        const float y0 = indicator.min.y + size * 0.52F;
        canvas.line({x0, y0}, {indicator.min.x + size * 0.43F, indicator.min.y + size * 0.72F},
            mStyle.mark, 2.0F, LineCap::Round);
        canvas.line({indicator.min.x + size * 0.43F, indicator.min.y + size * 0.72F},
            {indicator.min.x + size * 0.80F, indicator.min.y + size * 0.29F},
            mStyle.mark, 2.0F, LineCap::Round);
    }
    if (const TextLayoutResult* layout = text.layout(mStyle.font, mStyle.fontSize, mText)) {
        Vec2 origin = TextPainter::centeredVisualOrigin(*layout, frame());
        origin.x = indicator.max.x + mStyle.gap;
        text.drawLayout(canvas, *layout, origin, mStyle.textColor);
    }
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
    const bool active = pressed();
    const bool hot = hovered() || active;
    if (hot) {
        Color halo = mStyle.focus;
        halo.alpha *= active ? 0.24F : 0.13F;
        canvas.roundedGlow(track, halo, height * 0.5F, active ? 5.0F : 3.0F);
    }
    const Color fill = mChecked ? mStyle.active : mStyle.background;
    canvas.fillRect(
        track,
        hot ? mixColor(fill, mStyle.focus, active ? 0.20F : 0.10F) : fill,
        height * 0.5F);
    canvas.strokeRect(
        track,
        focused() || hot
            ? mixColor(mStyle.border, mStyle.focus, active ? 0.86F : 0.62F)
            : mStyle.border,
        height * 0.5F,
        hot ? 1.5F : 1.0F);
    const float knobRadius = std::max(height * 0.5F - 3.0F, 0.0F);
    const float knobX = mChecked ? track.max.x - height * 0.5F : track.min.x + height * 0.5F;
    canvas.circle(
        {knobX, y + height * 0.5F},
        knobRadius + (active ? 0.75F : 0.0F),
        mStyle.mark);
    if (const TextLayoutResult* layout = text.layout(mStyle.font, mStyle.fontSize, mText)) {
        Vec2 origin = TextPainter::centeredVisualOrigin(*layout, frame());
        origin.x = track.max.x + mStyle.gap;
        text.drawLayout(canvas, *layout, origin, mStyle.textColor);
    }
}

void Toggle::activate() {
    mChecked = !mChecked;
    markPaintDirty();
    mOnChanged(mChecked);
}

} // namespace henia::ui
