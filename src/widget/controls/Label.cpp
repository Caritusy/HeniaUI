#include "henia/ui/widget/controls/Label.h"

#include <algorithm>
#include <utility>

namespace henia::ui {
namespace {

struct ResolvedLabelStyle final {
    FontHandle font{};
    float size = 14.0F;
    Color color{};
};

[[nodiscard]] ResolvedLabelStyle resolve(
    const LabelStyle& style,
    const Theme& theme) noexcept {
    const float scale = std::max(theme.scale, 0.0F);
    return {
        .font = style.font.value_or(theme.font),
        .size = style.size.value_or(theme.fontSize * scale),
        .color = style.color.value_or(theme.textPrimary),
    };
}

} // namespace

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
    if (mStyle.font == styleValue.font && mStyle.size == styleValue.size
        && mStyle.color == styleValue.color) {
        return;
    }
    const bool layoutChanged = mStyle.font != styleValue.font || mStyle.size != styleValue.size;
    mStyle = styleValue;
    if (layoutChanged) {
        markLayoutDirty();
    } else {
        markPaintDirty();
    }
}

const LabelStyle& Label::style() const noexcept { return mStyle; }

Vec2 Label::onMeasure(TextPainter& textPainter, Constraints) {
    const ResolvedLabelStyle style = resolve(mStyle, inheritedTheme());
    const TextMetrics metrics = textPainter.measure(style.font, style.size, mText);
    return {metrics.width, metrics.height};
}

void Label::onPaint(Canvas& canvas, TextPainter& textPainter, const Theme& theme) {
    const ResolvedLabelStyle style = resolve(mStyle, theme);
    textPainter.draw(canvas, style.font, style.size, frame().min, style.color, mText);
}

} // namespace henia::ui
