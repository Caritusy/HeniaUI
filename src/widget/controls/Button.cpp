#include "henia/ui/widget/controls/Button.h"

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

struct ResolvedButtonStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color textColor{};
    Color background{};
    Color hover{};
    Color pressed{};
    Color border{};
    float borderWidth = 1.0F;
    float radius = 8.0F;
    Insets padding{};
    float minimumHeight = 0.0F;
};

[[nodiscard]] ResolvedButtonStyle resolve(
    const ButtonStyle& style,
    const Theme& theme) noexcept {
    const float scale = std::max(theme.scale, 0.0F);
    return {
        .font = style.font.value_or(theme.font),
        .fontSize = style.fontSize.value_or(theme.fontSize * scale),
        .textColor = style.textColor.value_or(theme.textPrimary),
        .background = style.background.value_or(theme.surfaceRaised),
        .hover = style.hover.value_or(theme.surfaceHover),
        .pressed = style.pressed.value_or(theme.surfacePressed),
        .border = style.border.value_or(theme.border),
        .borderWidth = style.borderWidth.value_or(theme.borderWidth * scale),
        .radius = style.radius.value_or(theme.cornerRadius * scale),
        .padding = style.padding.value_or(Insets{
            theme.controlPaddingHorizontal * scale,
            theme.controlPaddingVertical * scale,
            theme.controlPaddingHorizontal * scale,
            theme.controlPaddingVertical * scale,
        }),
        .minimumHeight = style.controlHeight.value_or(theme.controlHeight * scale),
    };
}

} // namespace

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
        && mStyle.padding == styleValue.padding
        && mStyle.controlHeight == styleValue.controlHeight;
    if (unchanged) {
        return;
    }
    const bool layoutChanged = mStyle.font != styleValue.font
        || mStyle.fontSize != styleValue.fontSize
        || !(mStyle.padding == styleValue.padding)
        || mStyle.controlHeight != styleValue.controlHeight;
    mStyle = styleValue;
    if (layoutChanged) {
        markLayoutDirty();
    } else {
        markPaintDirty();
    }
}

const ButtonStyle& Button::style() const noexcept { return mStyle; }

void Button::setOnClick(Callback<> callback) noexcept { mOnClick = callback; }

bool Button::acceptsPointerInput() const noexcept { return true; }

bool Button::acceptsKeyboardFocus() const noexcept { return true; }

bool Button::handleInput(const InputEvent& event) {
    if (!enabled()) {
        return false;
    }
    if (event.kind == InputEventKind::KeyDown && focused()
        && (event.key == KeyCode::Enter || event.key == KeyCode::Space)) {
        mOnClick();
        return true;
    }
    if (event.button != PointerButton::Primary) return false;
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
    const ResolvedButtonStyle style = resolve(mStyle, inheritedTheme());
    const TextMetrics metrics = textPainter.measure(style.font, style.fontSize, mText);
    return {
        metrics.width + style.padding.left + style.padding.right,
        std::max(
            metrics.height + style.padding.top + style.padding.bottom,
            style.minimumHeight),
    };
}

void Button::onPaint(Canvas& canvas, TextPainter& textPainter, const Theme& theme) {
    const ResolvedButtonStyle style = resolve(mStyle, theme);
    const bool active = pressed();
    const bool hot = hovered() || active;
    const Color background = active ? style.pressed : (hot ? style.hover : style.background);
    if (hot || focused()) {
        Color glow = theme.accent;
        glow.alpha *= active ? 0.18F : hot ? 0.10F : 0.08F;
        canvas.roundedGlow(frame(), glow, style.radius, active ? 6.0F : 4.0F);
    }
    canvas.fillRect(frame(), background, style.radius);
    if (style.borderWidth > 0.0F && style.border.alpha > 0.0F) {
        const Color border = focused()
            ? theme.accent
            : hot
                ? mixColor(style.border, theme.accent, active ? 0.72F : 0.45F)
                : style.border;
        canvas.strokeRect(
            frame(),
            border,
            style.radius,
            style.borderWidth + (hot ? 0.5F : 0.0F));
    }
    if (const TextLayoutResult* layout = textPainter.layout(
            style.font, style.fontSize, mText)) {
        textPainter.drawLayout(
            canvas,
            *layout,
            TextPainter::centeredVisualOrigin(*layout, frame()),
            style.textColor);
    }
}

} // namespace henia::ui
