#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <string>

namespace henia::ui {

struct NumericInputStyle final {
    ThemeProperty<FontHandle> font;
    ThemeProperty<float> fontSize;
    ThemeProperty<Color> textColor;
    ThemeProperty<Color> mutedText;
    ThemeProperty<Color> background;
    ThemeProperty<Color> hover;
    ThemeProperty<Color> pressed;
    ThemeProperty<Color> border;
    ThemeProperty<Color> focus;
    ThemeProperty<float> borderWidth;
    ThemeProperty<float> radius;
    ThemeProperty<float> controlWidth;
    ThemeProperty<float> controlHeight;
    ThemeProperty<float> stepButtonWidth;
    ThemeProperty<Insets> padding;

    friend constexpr bool operator==(
        const NumericInputStyle&,
        const NumericInputStyle&) noexcept = default;
};

class NumericInput final : public Widget {
public:
    explicit NumericInput(double value = 0.0, NumericInputStyle style = {});

    void setStyle(NumericInputStyle style) noexcept;
    [[nodiscard]] const NumericInputStyle& style() const noexcept;
    void setValue(double value) noexcept;
    [[nodiscard]] double value() const noexcept;
    void setRange(double minimum, double maximum) noexcept;
    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;
    void setStep(double step) noexcept;
    [[nodiscard]] double step() const noexcept;
    void setPrecision(std::size_t precision) noexcept;
    void setOnValueChanged(Callback<double> callback) noexcept;
    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    enum class Region : std::uint8_t { Decrement, Value, Increment };

    [[nodiscard]] Region regionAt(Vec2 point) const noexcept;
    [[nodiscard]] std::string formatValue() const;
    void beginEditing();
    void commitEditing();
    void cancelEditing() noexcept;
    void adjust(double delta);

    NumericInputStyle mStyle{};
    Callback<double> mOnValueChanged{};
    std::string mEditingText;
    double mValue = 0.0;
    double mMinimum = 0.0;
    double mMaximum = 100.0;
    double mStep = 1.0;
    std::size_t mPrecision = 0;
    Region mPressedRegion = Region::Value;
    Region mHoverRegion = Region::Value;
    bool mEditing = false;
};

} // namespace henia::ui
