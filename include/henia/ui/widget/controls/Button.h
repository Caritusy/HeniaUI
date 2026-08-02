#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <string>
#include <string_view>

namespace henia::ui {

struct ButtonStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color textColor{0.90F, 0.95F, 0.98F, 1.0F};
    Color background{0.046F, 0.064F, 0.092F, 1.0F};
    Color hover{0.060F, 0.092F, 0.125F, 1.0F};
    Color pressed{0.075F, 0.125F, 0.165F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    float borderWidth = 1.0F;
    float radius = 8.0F;
    Insets padding{14.0F, 9.0F, 14.0F, 9.0F};
};

class Button final : public Widget {
public:
    explicit Button(std::string text = {}, ButtonStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setStyle(ButtonStyle style) noexcept;
    void setOnClick(Callback<> callback) noexcept;
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
