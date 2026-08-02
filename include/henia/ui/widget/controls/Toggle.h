#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <string>
#include <string_view>

namespace henia::ui {

struct ToggleStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color textColor{0.90F, 0.95F, 0.98F, 1.0F};
    Color background{0.04F, 0.06F, 0.09F, 1.0F};
    Color active{0.10F, 0.72F, 0.91F, 1.0F};
    Color border{0.14F, 0.22F, 0.30F, 1.0F};
    Color focus{0.35F, 0.84F, 1.0F, 1.0F};
    Color mark{0.96F, 0.99F, 1.0F, 1.0F};
    float indicatorSize = 18.0F;
    float gap = 9.0F;
    float padding = 5.0F;
};

class Checkbox final : public Widget {
public:
    explicit Checkbox(std::string text = {}, bool checked = false, ToggleStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setChecked(bool checked) noexcept;
    [[nodiscard]] bool checked() const noexcept;
    void setStyle(ToggleStyle style) noexcept;
    void setOnChanged(Callback<bool> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    void activate();

    std::string mText;
    ToggleStyle mStyle{};
    Callback<bool> mOnChanged{};
    bool mChecked = false;
};

class Toggle final : public Widget {
public:
    explicit Toggle(std::string text = {}, bool checked = false, ToggleStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setChecked(bool checked) noexcept;
    [[nodiscard]] bool checked() const noexcept;
    void setStyle(ToggleStyle style) noexcept;
    void setOnChanged(Callback<bool> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    void activate();

    std::string mText;
    ToggleStyle mStyle{};
    Callback<bool> mOnChanged{};
    bool mChecked = false;
};

} // namespace henia::ui
