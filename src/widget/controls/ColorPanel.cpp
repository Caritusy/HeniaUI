#include "henia/ui/widget/controls/ColorPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace henia::ui {
namespace {

[[nodiscard]] float finiteNonnegative(float value) noexcept {
    return std::isfinite(value) ? std::max(value, 0.0F) : 0.0F;
}

[[nodiscard]] std::uint8_t byteFromUnit(float value) noexcept {
    const float unit = std::clamp(std::isfinite(value) ? value : 0.0F, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(unit * 255.0F));
}

[[nodiscard]] float unitFromByte(std::uint8_t value) noexcept {
    return static_cast<float>(value) / 255.0F;
}

[[nodiscard]] Srgba8 encodeSrgba8(Color color) noexcept {
    return {
        byteFromUnit(linearToSrgb(color.red)),
        byteFromUnit(linearToSrgb(color.green)),
        byteFromUnit(linearToSrgb(color.blue)),
        byteFromUnit(color.alpha),
    };
}

[[nodiscard]] Color decodeSrgba8(Srgba8 color) noexcept {
    return {
        srgbToLinear(unitFromByte(color.red)),
        srgbToLinear(unitFromByte(color.green)),
        srgbToLinear(unitFromByte(color.blue)),
        unitFromByte(color.alpha),
    };
}

[[nodiscard]] int nibble(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] float resolvedControlHeight(
    const ButtonStyle& style,
    const Theme& theme) noexcept {
    return finiteNonnegative(style.controlHeight.value_or(
        theme.controlHeight * finiteNonnegative(theme.scale)));
}

[[nodiscard]] float resolvedControlHeight(
    const NumericInputStyle& style,
    const Theme& theme) noexcept {
    return finiteNonnegative(style.controlHeight.value_or(
        theme.controlHeight * finiteNonnegative(theme.scale)));
}

[[nodiscard]] Rect takeRow(
    Rect content,
    float& y,
    float requestedHeight) noexcept {
    const float height = std::min(finiteNonnegative(requestedHeight), std::max(content.max.y - y, 0.0F));
    const Rect result{{content.min.x, y}, {content.max.x, y + height}};
    y += height;
    return result;
}

void arrangeExact(Widget& widget, TextPainter& text, Rect frame) {
    const Vec2 size{std::max(frame.width(), 0.0F), std::max(frame.height(), 0.0F)};
    static_cast<void>(widget.measure(text, {size, size}));
    widget.arrange(text, frame);
}

} // namespace

ColorPanel::ColorPanel(Color colorValue, ColorPanelInputMode mode, ColorPanelStyle style)
    : Widget(WidgetKind::ColorPanel), mStyle(std::move(style)), mMode(mode) {
    mPicker = &emplaceChild<ColorPicker>(colorValue, mStyle.picker);
    mPicker->setOnColorChanged(
        Callback<Color>::bind<ColorPanel, &ColorPanel::pickerChanged>(*this));

    mModes = &emplaceChild<TabBar>(
        std::vector<std::string>{"HEX", "RGB"},
        mode == ColorPanelInputMode::Hex ? 0U : 1U,
        mStyle.modes);
    mModes->setOnSelectionChanged(
        Callback<std::size_t>::bind<ColorPanel, &ColorPanel::modeChanged>(*this));

    mHexInput = &emplaceChild<TextInput>(std::string{}, mStyle.hexInput);
    mHexInput->setPlaceholder("#RRGGBBAA");
    mHexInput->setOnTextChanged(
        Callback<std::string_view>::bind<ColorPanel, &ColorPanel::hexChanged>(*this));

    for (std::size_t index = 0; index < mChannels.size(); ++index) {
        NumericInput& input = emplaceChild<NumericInput>(0.0, mStyle.channelInput);
        input.setRange(0.0, 255.0);
        input.setStep(1.0);
        input.setPrecision(0);
        mChannels[index] = &input;
        mChannelBindings[index] = {.owner = this, .index = index};
        input.setOnValueChanged(Callback<double>::bind<
            ChannelBinding, &ChannelBinding::changed>(mChannelBindings[index]));
    }

    mConfirm = &emplaceChild<Button>("Confirm", mStyle.confirmButton);
    mConfirm->setOnClick(Callback<>::bind<ColorPanel, &ColorPanel::confirmed>(*this));

    setInputMode(mode);
    setColor(colorValue);
}

void ColorPanel::setColor(Color colorValue) {
    mPicker->setColor(colorValue);
    applySrgba8(encodeSrgba8(mPicker->color()), false);
}

Color ColorPanel::color() const noexcept { return mColor; }

void ColorPanel::setSrgba8(Srgba8 colorValue) {
    applySrgba8(colorValue, false);
}

Srgba8 ColorPanel::srgba8() const noexcept { return encodeSrgba8(mColor); }

bool ColorPanel::setHexValue(std::string_view value) {
    Srgba8 parsed{};
    if (!parseHex(value, parsed)) {
        return false;
    }
    applySrgba8(parsed, false);
    return true;
}

std::string_view ColorPanel::hexValue() const noexcept { return mHexInput->text(); }

void ColorPanel::setInputMode(ColorPanelInputMode mode) {
    mMode = mode;
    mModes->setSelectedIndex(mode == ColorPanelInputMode::Hex ? 0U : 1U);
    mHexInput->setVisible(mode == ColorPanelInputMode::Hex);
    for (NumericInput* input : mChannels) {
        input->setVisible(mode == ColorPanelInputMode::Rgb);
    }
    syncEditors();
    markLayoutDirty();
}

ColorPanelInputMode ColorPanel::inputMode() const noexcept { return mMode; }

void ColorPanel::setStyle(ColorPanelStyle styleValue) {
    mStyle = std::move(styleValue);
    mPicker->setStyle(mStyle.picker);
    mModes->setStyle(mStyle.modes);
    TextInputStyle hexStyle = mStyle.hexInput;
    if (!mHexValid) {
        hexStyle.border = mStyle.invalid;
        hexStyle.focus = mStyle.invalid;
    }
    mHexInput->setStyle(std::move(hexStyle));
    for (NumericInput* input : mChannels) {
        input->setStyle(mStyle.channelInput);
    }
    mConfirm->setStyle(mStyle.confirmButton);
    markLayoutDirty();
}

const ColorPanelStyle& ColorPanel::style() const noexcept { return mStyle; }

void ColorPanel::setClipboard(TextClipboard* clipboard) noexcept {
    mHexInput->setClipboard(clipboard);
}

void ColorPanel::setOnColorChanged(Callback<Color> callback) noexcept {
    mOnColorChanged = callback;
}

void ColorPanel::setOnConfirmed(Callback<Color> callback) noexcept {
    mOnConfirmed = callback;
}

void ColorPanel::confirm() {
    if (mMode == ColorPanelInputMode::Hex && !mHexValid) {
        return;
    }
    syncEditors();
    mOnConfirmed(mColor);
}

bool ColorPanel::acceptsPointerInput() const noexcept { return true; }

bool ColorPanel::blocksUnhandledPointerInput(Vec2 point) const noexcept {
    return contains(point);
}

bool ColorPanel::handleInput(const InputEvent& event) {
    if (!enabled() || !contains(event.position)) {
        return false;
    }
    switch (event.kind) {
        case InputEventKind::PointerDown:
        case InputEventKind::PointerUp:
        case InputEventKind::PointerMove:
        case InputEventKind::PointerScroll:
            return true;
        default:
            return false;
    }
}

Vec2 ColorPanel::onMeasure(TextPainter&, Constraints) {
    const Theme& theme = inheritedTheme();
    const float padding = finiteNonnegative(mStyle.padding);
    const float gap = finiteNonnegative(mStyle.gap);
    const float editorHeight = mMode == ColorPanelInputMode::Hex
        ? finiteNonnegative(mStyle.hexInput.controlHeight)
        : finiteNonnegative(mStyle.channelLabelHeight)
            + resolvedControlHeight(mStyle.channelInput, theme);
    const float height = padding * 2.0F
        + finiteNonnegative(mStyle.picker.height)
        + finiteNonnegative(mStyle.modes.height)
        + editorHeight
        + resolvedControlHeight(mStyle.confirmButton, theme)
        + gap * 3.0F;
    return {finiteNonnegative(mStyle.width), height};
}

void ColorPanel::onArrange(TextPainter& text, Rect arrangedFrame) {
    const Theme& theme = inheritedTheme();
    const float padding = std::min(
        finiteNonnegative(mStyle.padding),
        std::min(std::max(arrangedFrame.width(), 0.0F), std::max(arrangedFrame.height(), 0.0F)) * 0.5F);
    const Rect content{
        {arrangedFrame.min.x + padding, arrangedFrame.min.y + padding},
        {arrangedFrame.max.x - padding, arrangedFrame.max.y - padding},
    };
    const float gap = finiteNonnegative(mStyle.gap);
    float y = content.min.y;

    arrangeExact(*mPicker, text, takeRow(content, y, mStyle.picker.height));
    y = std::min(y + gap, content.max.y);
    arrangeExact(*mModes, text, takeRow(content, y, mStyle.modes.height));
    y = std::min(y + gap, content.max.y);

    if (mMode == ColorPanelInputMode::Hex) {
        arrangeExact(*mHexInput, text, takeRow(content, y, mStyle.hexInput.controlHeight));
    } else {
        y = std::min(y + finiteNonnegative(mStyle.channelLabelHeight), content.max.y);
        const Rect row = takeRow(content, y, resolvedControlHeight(mStyle.channelInput, theme));
        const float channelGap = finiteNonnegative(mStyle.channelGap);
        const float available = std::max(row.width() - channelGap * 3.0F, 0.0F);
        const float width = available * 0.25F;
        for (std::size_t index = 0; index < mChannels.size(); ++index) {
            const float x = row.min.x + static_cast<float>(index) * (width + channelGap);
            arrangeExact(*mChannels[index], text, {{x, row.min.y}, {x + width, row.max.y}});
        }
    }

    y = std::min(y + gap, content.max.y);
    const float confirmHeight = resolvedControlHeight(mStyle.confirmButton, theme);
    const float confirmWidth = std::min(finiteNonnegative(mStyle.confirmWidth), content.width());
    const Rect confirmRow = takeRow(content, y, confirmHeight);
    arrangeExact(
        *mConfirm,
        text,
        {{confirmRow.max.x - confirmWidth, confirmRow.min.y}, confirmRow.max});
}

void ColorPanel::onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) {
    const float radius = finiteNonnegative(mStyle.radius);
    canvas.fillRect(frame(), mStyle.background, radius);
    if (mStyle.borderWidth > 0.0F && mStyle.border.alpha > 0.0F) {
        canvas.strokeRect(frame(), mStyle.border, radius, mStyle.borderWidth);
    }
    if (mMode != ColorPanelInputMode::Rgb || mChannels.front() == nullptr) {
        return;
    }
    constexpr std::array<std::string_view, 4> labels{"R", "G", "B", "A"};
    const float labelHeight = finiteNonnegative(mStyle.channelLabelHeight);
    for (std::size_t index = 0; index < mChannels.size(); ++index) {
        const Rect input = mChannels[index]->frame();
        const Rect labelBounds{{input.min.x, std::max(input.min.y - labelHeight, frame().min.y)},
                               {input.max.x, input.min.y}};
        if (const TextLayoutResult* layout = text.layout(theme.font, theme.fontSize, labels[index])) {
            text.drawLayout(
                canvas,
                *layout,
                TextPainter::centeredVisualOrigin(*layout, labelBounds),
                mStyle.channelLabel);
        }
    }
}

void ColorPanel::ChannelBinding::changed(double value) {
    owner->channelChanged(index, value);
}

void ColorPanel::pickerChanged(Color colorValue) {
    applySrgba8(encodeSrgba8(colorValue), true);
}

void ColorPanel::modeChanged(std::size_t index) {
    setInputMode(index == 0U ? ColorPanelInputMode::Hex : ColorPanelInputMode::Rgb);
}

void ColorPanel::hexChanged(std::string_view text) {
    Srgba8 parsed{};
    if (!parseHex(text, parsed)) {
        setHexValid(false);
        return;
    }
    setHexValid(true);
    applySrgba8(parsed, true);
}

void ColorPanel::channelChanged(std::size_t index, double value) {
    if (index >= mChannels.size()) {
        return;
    }
    Srgba8 channels = srgba8();
    const auto byte = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 255.0)));
    switch (index) {
        case 0: channels.red = byte; break;
        case 1: channels.green = byte; break;
        case 2: channels.blue = byte; break;
        default: channels.alpha = byte; break;
    }
    applySrgba8(channels, true);
}

void ColorPanel::confirmed() { confirm(); }

void ColorPanel::applySrgba8(Srgba8 colorValue, bool notify) {
    const Color next = decodeSrgba8(colorValue);
    const bool changed = next != mColor;
    mColor = next;
    mPicker->setColor(mColor);
    syncEditors();
    if (notify && changed) {
        mOnColorChanged(mColor);
    }
}

void ColorPanel::syncEditors() {
    const Srgba8 channels = srgba8();
    mHexInput->setText(formatHex(channels));
    setHexValid(true);
    mChannels[0]->setValue(channels.red);
    mChannels[1]->setValue(channels.green);
    mChannels[2]->setValue(channels.blue);
    mChannels[3]->setValue(channels.alpha);
}

void ColorPanel::setHexValid(bool valid) {
    if (mHexValid == valid) {
        return;
    }
    mHexValid = valid;
    TextInputStyle style = mStyle.hexInput;
    if (!valid) {
        style.border = mStyle.invalid;
        style.focus = mStyle.invalid;
    }
    mHexInput->setStyle(std::move(style));
}

bool ColorPanel::parseHex(std::string_view text, Srgba8& color) noexcept {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1U);
    }
    if (text.size() != 6U && text.size() != 8U) {
        return false;
    }
    std::array<std::uint8_t, 4> values{0U, 0U, 0U, 255U};
    for (std::size_t index = 0; index < text.size() / 2U; ++index) {
        const int high = nibble(text[index * 2U]);
        const int low = nibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        values[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    color = {values[0], values[1], values[2], values[3]};
    return true;
}

std::string ColorPanel::formatHex(Srgba8 color) {
    constexpr char digits[] = "0123456789ABCDEF";
    const std::array<std::uint8_t, 4> values{color.red, color.green, color.blue, color.alpha};
    std::string result(9U, '#');
    for (std::size_t index = 0; index < values.size(); ++index) {
        result[index * 2U + 1U] = digits[values[index] >> 4U];
        result[index * 2U + 2U] = digits[values[index] & 0x0FU];
    }
    return result;
}

} // namespace henia::ui
