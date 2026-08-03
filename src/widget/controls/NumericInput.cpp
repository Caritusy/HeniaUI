#include "henia/ui/widget/controls/NumericInput.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <utility>

namespace henia::ui {
namespace {

struct ResolvedNumericInputStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color textColor{};
    Color mutedText{};
    Color background{};
    Color hover{};
    Color pressed{};
    Color border{};
    Color focus{};
    float borderWidth = 1.0F;
    float radius = 8.0F;
    float controlWidth = 176.0F;
    float controlHeight = 36.0F;
    float stepButtonWidth = 40.0F;
    Insets padding{};
};

[[nodiscard]] ResolvedNumericInputStyle resolve(
    const NumericInputStyle& style,
    const Theme& theme) noexcept {
    const float scale = std::max(theme.scale, 0.0F);
    return {
        .font = style.font.value_or(theme.font),
        .fontSize = style.fontSize.value_or(theme.fontSize * scale),
        .textColor = style.textColor.value_or(theme.textPrimary),
        .mutedText = style.mutedText.value_or(theme.textMuted),
        .background = style.background.value_or(theme.surface),
        .hover = style.hover.value_or(theme.surfaceRaised),
        .pressed = style.pressed.value_or(theme.surfaceHover),
        .border = style.border.value_or(theme.border),
        .focus = style.focus.value_or(theme.accent),
        .borderWidth = style.borderWidth.value_or(theme.borderWidth * scale),
        .radius = style.radius.value_or(theme.cornerRadius * scale),
        .controlWidth = style.controlWidth.value_or(theme.controlWidth * scale),
        .controlHeight = style.controlHeight.value_or(theme.controlHeight * scale),
        .stepButtonWidth = style.stepButtonWidth.value_or(theme.stepButtonWidth * scale),
        .padding = style.padding.value_or(Insets{
            theme.controlPaddingHorizontal * scale,
            theme.controlPaddingVertical * scale,
            theme.controlPaddingHorizontal * scale,
            theme.controlPaddingVertical * scale,
        }),
    };
}

} // namespace

NumericInput::NumericInput(double initialValue, NumericInputStyle style)
    : Widget(WidgetKind::NumericInput), mStyle(style), mValue(initialValue) {
    mValue = std::clamp(mValue, mMinimum, mMaximum);
}

void NumericInput::setStyle(NumericInputStyle styleValue) noexcept {
    if (mStyle == styleValue) {
        return;
    }
    const bool layoutChanged = mStyle.font != styleValue.font
        || mStyle.fontSize != styleValue.fontSize
        || mStyle.controlWidth != styleValue.controlWidth
        || mStyle.controlHeight != styleValue.controlHeight
        || mStyle.stepButtonWidth != styleValue.stepButtonWidth
        || mStyle.padding != styleValue.padding;
    mStyle = std::move(styleValue);
    if (layoutChanged) {
        markLayoutDirty();
    } else {
        markPaintDirty();
    }
}

const NumericInputStyle& NumericInput::style() const noexcept { return mStyle; }

void NumericInput::setValue(double valueValue) noexcept {
    const double next = std::clamp(valueValue, mMinimum, mMaximum);
    if (next == mValue) {
        return;
    }
    mValue = next;
    mEditing = false;
    markPaintDirty();
}

double NumericInput::value() const noexcept { return mValue; }

void NumericInput::setRange(double minimumValue, double maximumValue) noexcept {
    if (minimumValue > maximumValue) {
        std::swap(minimumValue, maximumValue);
    }
    mMinimum = minimumValue;
    mMaximum = maximumValue;
    setValue(mValue);
    markPaintDirty();
}

double NumericInput::minimum() const noexcept { return mMinimum; }
double NumericInput::maximum() const noexcept { return mMaximum; }

void NumericInput::setStep(double valueValue) noexcept { mStep = std::max(std::abs(valueValue), 0.0); }
double NumericInput::step() const noexcept { return mStep; }

void NumericInput::setPrecision(std::size_t valueValue) noexcept {
    mPrecision = std::min<std::size_t>(valueValue, 8);
    markPaintDirty();
}

void NumericInput::setOnValueChanged(Callback<double> callback) noexcept { mOnValueChanged = callback; }

bool NumericInput::acceptsPointerInput() const noexcept { return true; }

bool NumericInput::acceptsKeyboardFocus() const noexcept { return true; }

bool NumericInput::handleInput(const InputEvent& event) {
    // Focus invalidation commits an in-progress value even when the operation
    // that caused it has already hidden or disabled this widget.
    if (event.kind == InputEventKind::FocusLost) {
        commitEditing();
        return true;
    }
    if (!enabled()) {
        return false;
    }
    if (event.kind == InputEventKind::PointerMove) {
        const Region next = regionAt(event.position);
        if (next != mHoverRegion) {
            mHoverRegion = next;
            markPaintDirty();
        }
        return contains(event.position);
    }
    if (event.kind == InputEventKind::PointerDown && event.button == PointerButton::Primary) {
        mPressedRegion = regionAt(event.position);
        if (mPressedRegion == Region::Value) {
            beginEditing();
        }
        markPaintDirty();
        return true;
    }
    if (event.kind == InputEventKind::PointerUp && event.button == PointerButton::Primary) {
        const Region released = regionAt(event.position);
        if (released == mPressedRegion) {
            if (released == Region::Decrement) {
                adjust(-mStep);
            } else if (released == Region::Increment) {
                adjust(mStep);
            }
        }
        markPaintDirty();
        return true;
    }
    if (event.kind == InputEventKind::PointerScroll && contains(event.position) && event.scrollY != 0.0F) {
        adjust(event.scrollY > 0.0F ? mStep : -mStep);
        return true;
    }
    if (!focused()) {
        return false;
    }
    if (event.kind == InputEventKind::TextInput) {
        beginEditing();
        const char32_t codepoint = event.text;
        const bool digit = codepoint >= U'0' && codepoint <= U'9';
        const bool decimal = codepoint == U'.' && mPrecision > 0 && mEditingText.find('.') == std::string::npos;
        const bool sign = codepoint == U'-' && mMinimum < 0.0 && mEditingText.empty();
        if (digit || decimal || sign) {
            mEditingText.push_back(static_cast<char>(codepoint));
            markPaintDirty();
        }
        return true;
    }
    if (event.kind != InputEventKind::KeyDown) {
        return false;
    }
    switch (event.key) {
        case KeyCode::Backspace:
        case KeyCode::Delete:
            beginEditing();
            if (!mEditingText.empty()) {
                mEditingText.pop_back();
                markPaintDirty();
            }
            return true;
        case KeyCode::Enter:
            commitEditing();
            return true;
        case KeyCode::Escape:
            cancelEditing();
            return true;
        case KeyCode::Left:
        case KeyCode::Down:
            adjust(-mStep);
            return true;
        case KeyCode::Right:
        case KeyCode::Up:
            adjust(mStep);
            return true;
        default:
            return false;
    }
}

Vec2 NumericInput::onMeasure(TextPainter&, Constraints) {
    const ResolvedNumericInputStyle style = resolve(mStyle, inheritedTheme());
    return {style.controlWidth, style.controlHeight};
}

void NumericInput::onPaint(Canvas& canvas, TextPainter& textPainter, const Theme& theme) {
    const ResolvedNumericInputStyle style = resolve(mStyle, theme);
    const Rect bounds = frame();
    canvas.fillRect(bounds, style.background, style.radius);
    canvas.strokeRect(bounds, focused() ? style.focus : style.border, style.radius, style.borderWidth);

    const float buttonWidth = std::min(style.stepButtonWidth, bounds.width() * 0.4F);
    const Rect decrement{bounds.min, {bounds.min.x + buttonWidth, bounds.max.y}};
    const Rect increment{{bounds.max.x - buttonWidth, bounds.min.y}, bounds.max};
    const Region activeRegion = pressed() ? mPressedRegion : mHoverRegion;
    if ((hovered() || pressed()) && activeRegion != Region::Value) {
        const Rect target = activeRegion == Region::Increment ? increment : decrement;
        canvas.fillRect(target, pressed() ? style.pressed : style.hover, style.radius);
    }
    canvas.line(
        {decrement.max.x, bounds.min.y + 6.0F},
        {decrement.max.x, bounds.max.y - 6.0F},
        style.border,
        1.0F);
    canvas.line(
        {increment.min.x, bounds.min.y + 6.0F},
        {increment.min.x, bounds.max.y - 6.0F},
        style.border,
        1.0F);

    const std::string valueText = mEditing ? mEditingText : formatValue();
    const TextMetrics valueMetrics = textPainter.measure(style.font, style.fontSize, valueText);
    const float valueMinX = decrement.max.x;
    const float valueWidth = std::max(increment.min.x - valueMinX, 0.0F);
    const float textY = bounds.min.y + std::max((bounds.height() - valueMetrics.height) * 0.5F, 0.0F);
    textPainter.draw(canvas, style.font, style.fontSize,
        {valueMinX + std::max((valueWidth - valueMetrics.width) * 0.5F, 0.0F), textY},
        style.textColor, valueText);

    const auto drawMinus = [&](const Rect& region) {
        const Vec2 center{
            (region.min.x + region.max.x) * 0.5F,
            (region.min.y + region.max.y) * 0.5F,
        };
        canvas.line({center.x - 5.0F, center.y}, {center.x + 5.0F, center.y}, style.mutedText, 1.5F);
    };
    drawMinus(decrement);
    drawMinus(increment);
    const Vec2 incrementCenter{
        (increment.min.x + increment.max.x) * 0.5F,
        (increment.min.y + increment.max.y) * 0.5F,
    };
    canvas.line(
        {incrementCenter.x, incrementCenter.y - 5.0F},
        {incrementCenter.x, incrementCenter.y + 5.0F},
        style.mutedText,
        1.5F);
}

NumericInput::Region NumericInput::regionAt(Vec2 point) const noexcept {
    const Rect bounds = frame();
    const ResolvedNumericInputStyle style = resolve(mStyle, inheritedTheme());
    const float buttonWidth = std::min(style.stepButtonWidth, bounds.width() * 0.4F);
    if (point.x < bounds.min.x + buttonWidth) {
        return Region::Decrement;
    }
    if (point.x >= bounds.max.x - buttonWidth) {
        return Region::Increment;
    }
    return Region::Value;
}

std::string NumericInput::formatValue() const {
    char buffer[96]{};
    const auto result = std::to_chars(
        buffer, buffer + sizeof(buffer), mValue, std::chars_format::fixed, static_cast<int>(mPrecision));
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{"0"};
}

void NumericInput::beginEditing() {
    if (mEditing) {
        return;
    }
    mEditingText = formatValue();
    mEditing = true;
    markPaintDirty();
}

void NumericInput::commitEditing() {
    if (!mEditing) {
        return;
    }
    double parsed = mValue;
    const auto result = std::from_chars(mEditingText.data(), mEditingText.data() + mEditingText.size(), parsed);
    mEditing = false;
    if (result.ec == std::errc{} && result.ptr == mEditingText.data() + mEditingText.size()) {
        adjust(parsed - mValue);
    } else {
        markPaintDirty();
    }
}

void NumericInput::cancelEditing() noexcept {
    mEditing = false;
    mEditingText.clear();
    markPaintDirty();
}

void NumericInput::adjust(double delta) {
    const double next = std::clamp(mValue + delta, mMinimum, mMaximum);
    mEditing = false;
    if (next == mValue) {
        markPaintDirty();
        return;
    }
    mValue = next;
    markPaintDirty();
    mOnValueChanged(mValue);
}

} // namespace henia::ui
