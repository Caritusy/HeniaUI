#pragma once

#include "henia/ui/widget/Callback.h"
#include "henia/ui/widget/Widget.h"

namespace henia::ui {

struct SliderStyle final {
    Color track{0.10F, 0.15F, 0.21F, 1.0F};
    Color fill{0.10F, 0.72F, 0.91F, 1.0F};
    Color knob{0.92F, 0.98F, 1.0F, 1.0F};
    Color focus{0.35F, 0.84F, 1.0F, 1.0F};
    float width = 220.0F;
    float height = 30.0F;
    float trackHeight = 5.0F;
    float knobRadius = 8.0F;
};

class Slider final : public Widget {
public:
    explicit Slider(
        double value = 0.0,
        double minimum = 0.0,
        double maximum = 1.0,
        double step = 0.0,
        SliderStyle style = {}) noexcept;

    void setRange(double minimum, double maximum, double step = 0.0) noexcept;
    void setValue(double value) noexcept;
    [[nodiscard]] double value() const noexcept;
    [[nodiscard]] double minimum() const noexcept;
    [[nodiscard]] double maximum() const noexcept;
    [[nodiscard]] double step() const noexcept;
    void setStyle(SliderStyle style) noexcept;
    void setOnValueChanged(Callback<double> callback) noexcept;

    [[nodiscard]] bool acceptsPointerInput() const noexcept override;
    [[nodiscard]] bool acceptsKeyboardFocus() const noexcept override;
    [[nodiscard]] bool handleInput(const InputEvent& event) override;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    [[nodiscard]] double normalized(double value) const noexcept;
    [[nodiscard]] bool update(double value, bool notify);
    [[nodiscard]] bool updateFromPointer(float x);

    SliderStyle mStyle{};
    Callback<double> mOnValueChanged{};
    double mValue = 0.0;
    double mMinimum = 0.0;
    double mMaximum = 1.0;
    double mStep = 0.0;
};

} // namespace henia::ui
