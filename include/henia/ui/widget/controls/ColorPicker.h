#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

namespace henia::ui {

struct ColorPickerStyle final {
    Color border{0.14F, 0.22F, 0.30F, 1.0F};
    Color focus{0.35F, 0.84F, 1.0F, 1.0F};
    Color marker{1.0F, 1.0F, 1.0F, 1.0F};
    Color alphaLight{0.72F, 0.75F, 0.78F, 1.0F};
    Color alphaDark{0.30F, 0.33F, 0.36F, 1.0F};
    float width = 220.0F;
    float height = 184.0F;
    float hueHeight = 16.0F;
    float alphaHeight = 16.0F;
    float gap = 8.0F;
    float radius = 6.0F;
};

class ColorPicker final : public Widget {
public:
    explicit ColorPicker(Color color = {}, ColorPickerStyle style = {}) noexcept;

    void setColor(Color color) noexcept;
    [[nodiscard]] Color color() const noexcept;
    void setStyle(ColorPickerStyle style) noexcept;
    void setOnColorChanged(Callback<Color> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    enum class Drag : std::uint8_t { None, SaturationValue, Hue, Alpha };
    [[nodiscard]] Rect saturationRect() const noexcept;
    [[nodiscard]] Rect hueRect() const noexcept;
    [[nodiscard]] Rect alphaRect() const noexcept;
    [[nodiscard]] bool updateFromPointer(Vec2 position, bool chooseRegion);
    void publish();

    ColorPickerStyle mStyle{};
    Callback<Color> mOnColorChanged{};
    float mHue = 0.0F;
    float mSaturation = 0.0F;
    float mValue = 1.0F;
    float mAlpha = 1.0F;
    Drag mDrag = Drag::None;
};

} // namespace henia::ui
