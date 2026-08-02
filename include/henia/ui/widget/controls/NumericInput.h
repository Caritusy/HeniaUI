#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

#include <cstddef>
#include <string>

namespace henia::ui {

struct NumericInputStyle final {
    FontHandle font{};
    float fontSize = 14.0F;
    Color textColor{0.90F, 0.95F, 0.98F, 1.0F};
    Color mutedText{0.48F, 0.59F, 0.67F, 1.0F};
    Color background{0.032F, 0.047F, 0.071F, 1.0F};
    Color hover{0.046F, 0.064F, 0.092F, 1.0F};
    Color pressed{0.060F, 0.092F, 0.125F, 1.0F};
    Color border{0.12F, 0.20F, 0.28F, 1.0F};
    Color focus{0.10F, 0.72F, 0.91F, 1.0F};
    float borderWidth = 1.0F;
    float radius = 8.0F;
    float controlWidth = 176.0F;
    float controlHeight = 38.0F;
    float stepButtonWidth = 40.0F;
    Insets padding{12.0F, 9.0F, 12.0F, 9.0F};
};

class NumericInput final : public Widget {
public:
    explicit NumericInput(double value = 0.0, NumericInputStyle style = {});

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
