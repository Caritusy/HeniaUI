#include "henia/ui/widget/controls/Slider.h"

#include <algorithm>
#include <cmath>

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

} // namespace

Slider::Slider(double value, double minimum, double maximum, double step, SliderStyle style) noexcept
    : Widget(WidgetKind::Slider), mStyle(style) {
    setRange(minimum, maximum, step);
    mValue = normalized(value);
}

void Slider::setRange(double minimum, double maximum, double stepValue) noexcept {
    if (!std::isfinite(minimum)) minimum = 0.0;
    if (!std::isfinite(maximum)) maximum = minimum + 1.0;
    if (minimum > maximum) std::swap(minimum, maximum);
    if (minimum == maximum) maximum = minimum + 1.0;
    mMinimum = minimum;
    mMaximum = maximum;
    mStep = std::isfinite(stepValue) && stepValue > 0.0 ? stepValue : 0.0;
    mValue = normalized(mValue);
    markPaintDirty();
}

void Slider::setValue(double value) noexcept { static_cast<void>(update(value, false)); }
double Slider::value() const noexcept { return mValue; }
double Slider::minimum() const noexcept { return mMinimum; }
double Slider::maximum() const noexcept { return mMaximum; }
double Slider::step() const noexcept { return mStep; }
void Slider::setStyle(SliderStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void Slider::setOnValueChanged(Callback<double> callback) noexcept { mOnValueChanged = callback; }
bool Slider::acceptsPointerInput() const noexcept { return true; }
bool Slider::acceptsKeyboardFocus() const noexcept { return true; }

bool Slider::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) {
        static_cast<void>(updateFromPointer(event.position.x));
        return true;
    }
    if (event.kind == InputEventKind::PointerMove && pressed()) {
        static_cast<void>(updateFromPointer(event.position.x));
        return true;
    }
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary) {
        static_cast<void>(updateFromPointer(event.position.x));
        return true;
    }
    if (event.kind != InputEventKind::KeyDown || !focused()) return false;
    const double increment = mStep > 0.0 ? mStep : (mMaximum - mMinimum) / 100.0;
    switch (event.key) {
        case KeyCode::Left:
        case KeyCode::Down: return update(mValue - increment, true);
        case KeyCode::Right:
        case KeyCode::Up: return update(mValue + increment, true);
        case KeyCode::Home: return update(mMinimum, true);
        case KeyCode::End: return update(mMaximum, true);
        case KeyCode::PageDown: return update(mValue - increment * 10.0, true);
        case KeyCode::PageUp: return update(mValue + increment * 10.0, true);
        default: return false;
    }
}

Vec2 Slider::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void Slider::onPaint(Canvas& canvas, TextPainter&, const Theme&) {
    const bool active = pressed();
    const bool hot = hovered() || active;
    const float baseRadius = std::max(mStyle.knobRadius, 0.0F);
    const float radius = baseRadius + (active ? 1.5F : hot ? 0.75F : 0.0F);
    const float left = frame().min.x + baseRadius;
    const float right = frame().max.x - baseRadius;
    const float centerY = (frame().min.y + frame().max.y) * 0.5F;
    const float usable = std::max(right - left, 0.0F);
    const float amount = static_cast<float>((mValue - mMinimum) / (mMaximum - mMinimum));
    const float knobX = left + usable * std::clamp(amount, 0.0F, 1.0F);
    const float trackHeight = mStyle.trackHeight + (hot ? 1.0F : 0.0F);
    const Rect track{{left, centerY - trackHeight * 0.5F},
                     {right, centerY + trackHeight * 0.5F}};
    canvas.fillRect(
        track,
        hot ? mixColor(mStyle.track, mStyle.focus, active ? 0.28F : 0.14F) : mStyle.track,
        trackHeight * 0.5F);
    if (knobX > left) {
        canvas.fillRect(
            {track.min, {knobX, track.max.y}},
            hot ? mixColor(mStyle.fill, mStyle.focus, active ? 0.42F : 0.20F) : mStyle.fill,
            trackHeight * 0.5F);
    }
    if (focused() || hot) {
        Color halo = mStyle.focus;
        halo.alpha *= active ? 0.42F : hovered() ? 0.24F : 0.30F;
        canvas.circle({knobX, centerY}, radius + (active ? 5.0F : 3.5F), halo);
    }
    canvas.circle(
        {knobX, centerY},
        radius,
        active ? mixColor(mStyle.knob, mStyle.focus, 0.22F) : mStyle.knob);
}

double Slider::normalized(double value) const noexcept {
    if (!std::isfinite(value)) value = mMinimum;
    value = std::clamp(value, mMinimum, mMaximum);
    // Range endpoints are part of the public contract even when the step does
    // not divide the range. Quantizing first would turn a maximum of 0 into a
    // neighbouring interior step for ranges such as [-1, 0] with step 0.3.
    if (value <= mMinimum) return mMinimum;
    if (value >= mMaximum) return mMaximum;
    if (mStep > 0.0) {
        value = mMinimum + std::round((value - mMinimum) / mStep) * mStep;
        value = std::clamp(value, mMinimum, mMaximum);
    }
    return value;
}

bool Slider::update(double value, bool notify) {
    const double next = normalized(value);
    if (next == mValue) return false;
    mValue = next;
    markPaintDirty();
    if (notify) mOnValueChanged(mValue);
    return true;
}

bool Slider::updateFromPointer(float x) {
    const double radius = std::max(static_cast<double>(mStyle.knobRadius), 0.0);
    const double left = static_cast<double>(frame().min.x) + radius;
    const double right = static_cast<double>(frame().max.x) - radius;
    const double amount = right <= left ? 0.0
        : std::clamp((static_cast<double>(x) - left) / (right - left), 0.0, 1.0);
    return update(mMinimum + (mMaximum - mMinimum) * amount, true);
}

} // namespace henia::ui
