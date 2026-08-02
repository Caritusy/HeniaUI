#include "henia/ui/widget/controls/ColorPicker.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace henia::ui {
namespace {

[[nodiscard]] Color hsv(float hue, float saturation, float value, float alpha = 1.0F) noexcept {
    hue -= std::floor(hue);
    saturation = std::clamp(saturation, 0.0F, 1.0F);
    value = std::clamp(value, 0.0F, 1.0F);
    const float scaled = hue * 6.0F;
    const int sector = static_cast<int>(scaled) % 6;
    const float fraction = scaled - std::floor(scaled);
    const float p = value * (1.0F - saturation);
    const float q = value * (1.0F - saturation * fraction);
    const float t = value * (1.0F - saturation * (1.0F - fraction));
    switch (sector) {
        case 0: return {value, t, p, alpha}; case 1: return {q, value, p, alpha};
        case 2: return {p, value, t, alpha}; case 3: return {p, q, value, alpha};
        case 4: return {t, p, value, alpha}; default: return {value, p, q, alpha};
    }
}

[[nodiscard]] bool inside(Rect rect, Vec2 point) noexcept {
    return point.x >= rect.min.x && point.y >= rect.min.y
        && point.x < rect.max.x && point.y < rect.max.y;
}

} // namespace

ColorPicker::ColorPicker(Color color, ColorPickerStyle style) noexcept
    : Widget(WidgetKind::ColorPicker), mStyle(style) { setColor(color); }

void ColorPicker::setColor(Color input) noexcept {
    input.red = std::clamp(std::isfinite(input.red) ? input.red : 0.0F, 0.0F, 1.0F);
    input.green = std::clamp(std::isfinite(input.green) ? input.green : 0.0F, 0.0F, 1.0F);
    input.blue = std::clamp(std::isfinite(input.blue) ? input.blue : 0.0F, 0.0F, 1.0F);
    mAlpha = std::clamp(std::isfinite(input.alpha) ? input.alpha : 1.0F, 0.0F, 1.0F);
    const float maximum = std::max({input.red, input.green, input.blue});
    const float minimum = std::min({input.red, input.green, input.blue});
    const float delta = maximum - minimum;
    mValue = maximum;
    mSaturation = maximum <= 0.0F ? 0.0F : delta / maximum;
    if (delta <= 0.0F) mHue = 0.0F;
    else if (maximum == input.red) mHue = std::fmod((input.green - input.blue) / delta, 6.0F) / 6.0F;
    else if (maximum == input.green) mHue = ((input.blue - input.red) / delta + 2.0F) / 6.0F;
    else mHue = ((input.red - input.green) / delta + 4.0F) / 6.0F;
    if (mHue < 0.0F) mHue += 1.0F;
    markPaintDirty();
}

Color ColorPicker::color() const noexcept { return hsv(mHue, mSaturation, mValue, mAlpha); }
void ColorPicker::setStyle(ColorPickerStyle style) noexcept { mStyle = style; markLayoutDirty(); }
void ColorPicker::setOnColorChanged(Callback<Color> callback) noexcept { mOnColorChanged = callback; }
bool ColorPicker::acceptsPointerInput() const noexcept { return true; }
bool ColorPicker::acceptsKeyboardFocus() const noexcept { return true; }

bool ColorPicker::handleInput(const InputEvent& event) {
    if (!enabled()) return false;
    if (event.kind == InputEventKind::PointerDown
        && event.button == PointerButton::Primary) {
        return updateFromPointer(event.position, true);
    }
    if (event.kind == InputEventKind::PointerMove && pressed()) {
        return updateFromPointer(event.position, false);
    }
    if (event.kind == InputEventKind::PointerUp
        && event.button == PointerButton::Primary) {
        const bool handled = updateFromPointer(event.position, false);
        mDrag = Drag::None;
        return handled;
    }
    if (event.kind != InputEventKind::KeyDown || !focused()) return false;
    const float fine = event.shift ? 0.005F : 0.02F;
    switch (event.key) {
        case KeyCode::Left: mHue = std::max(mHue - fine, 0.0F); break;
        case KeyCode::Right: mHue = std::min(mHue + fine, 1.0F); break;
        case KeyCode::Down: mValue = std::max(mValue - fine, 0.0F); break;
        case KeyCode::Up: mValue = std::min(mValue + fine, 1.0F); break;
        case KeyCode::Home: mSaturation = 0.0F; break;
        case KeyCode::End: mSaturation = 1.0F; break;
        default: return false;
    }
    publish();
    return true;
}

Vec2 ColorPicker::onMeasure(TextPainter&, Constraints) { return {mStyle.width, mStyle.height}; }

void ColorPicker::onPaint(Canvas& canvas, TextPainter&, const Theme&) {
    const Rect square = saturationRect();
    canvas.fillRect(square, hsv(mHue, 1.0F, 1.0F), mStyle.radius);
    canvas.gradientRect(square, {1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 0.0F},
        {1.0F, 0.0F}, mStyle.radius);
    canvas.gradientRect(square, {0.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
        {0.0F, 1.0F}, mStyle.radius);
    canvas.strokeRect(square, focused() ? mStyle.focus : mStyle.border, mStyle.radius, 1.0F);
    const Rect hue = hueRect();
    constexpr std::array<Color, 7> colors{{
        {1,0,0,1}, {1,1,0,1}, {0,1,0,1}, {0,1,1,1},
        {0,0,1,1}, {1,0,1,1}, {1,0,0,1},
    }};
    const float segment = hue.width() / 6.0F;
    for (std::size_t index = 0; index < 6; ++index) {
        canvas.gradientRect({{hue.min.x + segment * static_cast<float>(index), hue.min.y},
            {hue.min.x + segment * static_cast<float>(index + 1U), hue.max.y}},
            colors[index], colors[index + 1U], {1.0F, 0.0F}, 0.0F);
    }
    canvas.strokeRect(hue, mStyle.border, mStyle.radius, 1.0F);
    const Vec2 svMarker{square.min.x + square.width() * mSaturation,
                        square.min.y + square.height() * (1.0F - mValue)};
    canvas.circle(svMarker, 5.0F, mStyle.border);
    canvas.circle(svMarker, 3.0F, mStyle.marker);
    const float hueX = hue.min.x + hue.width() * mHue;
    canvas.strokeRect({{hueX - 2.0F, hue.min.y - 2.0F}, {hueX + 2.0F, hue.max.y + 2.0F}},
        mStyle.marker, 1.0F, 1.0F);
}

Rect ColorPicker::saturationRect() const noexcept {
    return {{frame().min.x, frame().min.y},
            {frame().max.x, std::max(frame().min.y, frame().max.y - mStyle.hueHeight - mStyle.gap)}};
}
Rect ColorPicker::hueRect() const noexcept {
    return {{frame().min.x, std::max(frame().min.y, frame().max.y - mStyle.hueHeight)}, frame().max};
}

bool ColorPicker::updateFromPointer(Vec2 position, bool chooseRegion) {
    if (chooseRegion) {
        if (inside(hueRect(), position)) mDrag = Drag::Hue;
        else if (inside(saturationRect(), position)) mDrag = Drag::SaturationValue;
        else return false;
    }
    if (mDrag == Drag::Hue) {
        const Rect rect = hueRect();
        mHue = rect.width() <= 0.0F ? 0.0F
            : std::clamp((position.x - rect.min.x) / rect.width(), 0.0F, 1.0F);
    } else if (mDrag == Drag::SaturationValue) {
        const Rect rect = saturationRect();
        mSaturation = rect.width() <= 0.0F ? 0.0F
            : std::clamp((position.x - rect.min.x) / rect.width(), 0.0F, 1.0F);
        mValue = rect.height() <= 0.0F ? 0.0F
            : 1.0F - std::clamp((position.y - rect.min.y) / rect.height(), 0.0F, 1.0F);
    } else return false;
    publish();
    return true;
}

void ColorPicker::publish() {
    markPaintDirty();
    mOnColorChanged(color());
}

} // namespace henia::ui
