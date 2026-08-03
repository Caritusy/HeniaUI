#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <string>
#include <string_view>

namespace henia::ui {

struct ButtonStyle final {
    ThemeProperty<FontHandle> font;
    ThemeProperty<float> fontSize;
    ThemeProperty<Color> textColor;
    ThemeProperty<Color> background;
    ThemeProperty<Color> hover;
    ThemeProperty<Color> pressed;
    ThemeProperty<Color> border;
    ThemeProperty<float> borderWidth;
    ThemeProperty<float> radius;
    ThemeProperty<Insets> padding;
    ThemeProperty<float> controlHeight;
};

class Button final : public Widget {
public:
    explicit Button(std::string text = {}, ButtonStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setStyle(ButtonStyle style) noexcept;
    [[nodiscard]] const ButtonStyle& style() const noexcept;
    void setOnClick(Callback<> callback) noexcept;
    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    std::string mText;
    ButtonStyle mStyle{};
    Callback<> mOnClick{};
};

} // namespace henia::ui
