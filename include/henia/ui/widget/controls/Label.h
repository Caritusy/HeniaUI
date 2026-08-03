#pragma once

#include "henia/ui/widget/Widget.h"

#include <string>
#include <string_view>

namespace henia::ui {

struct LabelStyle final {
    ThemeProperty<FontHandle> font;
    ThemeProperty<float> size;
    ThemeProperty<Color> color;
};

class Label final : public Widget {
public:
    explicit Label(std::string text = {}, LabelStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setStyle(LabelStyle style) noexcept;
    [[nodiscard]] const LabelStyle& style() const noexcept;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    std::string mText;
    LabelStyle mStyle{};
};

} // namespace henia::ui
