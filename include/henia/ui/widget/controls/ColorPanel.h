#pragma once

#include "henia/ui/widget/controls/Button.h"
#include "henia/ui/widget/controls/ColorPicker.h"
#include "henia/ui/widget/controls/NumericInput.h"
#include "henia/ui/widget/controls/TabBar.h"
#include "henia/ui/widget/controls/TextInput.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace henia::ui {

enum class ColorPanelInputMode : std::uint8_t {
    Hex,
    Rgb,
};

// Standard sRGB byte channels plus a linear 8-bit alpha channel. HeniaUI Color
// remains linear-light internally; ColorPanel performs the transfer conversion.
struct Srgba8 final {
    std::uint8_t red = 255;
    std::uint8_t green = 255;
    std::uint8_t blue = 255;
    std::uint8_t alpha = 255;

    friend constexpr bool operator==(Srgba8, Srgba8) noexcept = default;
};

struct ColorPanelStyle final {
    Color background{0.026F, 0.039F, 0.058F, 1.0F};
    Color border{0.14F, 0.22F, 0.30F, 1.0F};
    Color channelLabel{0.58F, 0.66F, 0.74F, 1.0F};
    Color invalid{0.93F, 0.31F, 0.36F, 1.0F};
    ColorPickerStyle picker{.width = 276.0F, .height = 184.0F};
    TabBarStyle modes{.width = 276.0F, .height = 34.0F};
    TextInputStyle hexInput{.controlWidth = 276.0F, .controlHeight = 36.0F};
    NumericInputStyle channelInput{};
    ButtonStyle confirmButton{};
    float width = 300.0F;
    float padding = 12.0F;
    float gap = 8.0F;
    float channelGap = 6.0F;
    float channelLabelHeight = 16.0F;
    float borderWidth = 1.0F;
    float radius = 10.0F;
    float confirmWidth = 104.0F;
};

// Composite RGBA editor intended for PopupLayer content. Changes are published
// live, while closure remains an explicit owner decision in the confirm callback.
// Pair it with PopupLayerStyle{.dismissOnBackdrop = false} for confirm-only
// dismissal.
class ColorPanel final : public Widget {
public:
    explicit ColorPanel(
        Color color = {},
        ColorPanelInputMode mode = ColorPanelInputMode::Hex,
        ColorPanelStyle style = {});

    void setColor(Color color);
    [[nodiscard]] Color color() const noexcept;
    void setSrgba8(Srgba8 color);
    [[nodiscard]] Srgba8 srgba8() const noexcept;
    [[nodiscard]] bool setHexValue(std::string_view value);
    [[nodiscard]] std::string_view hexValue() const noexcept;
    void setInputMode(ColorPanelInputMode mode);
    [[nodiscard]] ColorPanelInputMode inputMode() const noexcept;
    void setStyle(ColorPanelStyle style);
    [[nodiscard]] const ColorPanelStyle& style() const noexcept;
    void setClipboard(TextClipboard* clipboard) noexcept;
    void setOnColorChanged(Callback<Color> callback) noexcept;
    void setOnConfirmed(Callback<Color> callback) noexcept;
    void confirm();

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool blocksUnhandledPointerInput(Vec2 point) const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onArrange(TextPainter& text, Rect frame) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    struct ChannelBinding final {
        ColorPanel* owner = nullptr;
        std::size_t index = 0;
        void changed(double value);
    };

    void pickerChanged(Color color);
    void modeChanged(std::size_t index);
    void hexChanged(std::string_view text);
    void channelChanged(std::size_t index, double value);
    void confirmed();
    void applySrgba8(Srgba8 color, bool notify);
    void syncEditors();
    void setHexValid(bool valid);
    [[nodiscard]] static bool parseHex(std::string_view text, Srgba8& color) noexcept;
    [[nodiscard]] static std::string formatHex(Srgba8 color);

    ColorPanelStyle mStyle{};
    Callback<Color> mOnColorChanged{};
    Callback<Color> mOnConfirmed{};
    ColorPicker* mPicker = nullptr;
    TabBar* mModes = nullptr;
    TextInput* mHexInput = nullptr;
    std::array<NumericInput*, 4> mChannels{};
    std::array<ChannelBinding, 4> mChannelBindings{};
    Button* mConfirm = nullptr;
    Color mColor{};
    ColorPanelInputMode mMode = ColorPanelInputMode::Hex;
    bool mHexValid = true;
};

} // namespace henia::ui
