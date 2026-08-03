#pragma once

#include "henia/ui/widget/Widget.h"

namespace henia::ui {

enum class LayoutDirection : std::uint8_t {
    Row,
    Column,
};

struct PanelStyle final {
    // Unset properties inherit the document Theme; set values are stable
    // widget-local overrides.
    ThemeProperty<Color> background;
    ThemeProperty<Color> border;
    ThemeProperty<float> borderWidth;
    ThemeProperty<float> radius;
    ThemeProperty<Insets> padding;
    ThemeProperty<float> gap;
    LayoutDirection direction = LayoutDirection::Column;
    bool stretchCrossAxis = true;
};

class Panel final : public Widget {
public:
    explicit Panel(PanelStyle style = {}) noexcept;

    void setStyle(PanelStyle style) noexcept;
    [[nodiscard]] const PanelStyle& style() const noexcept;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onArrange(TextPainter& text, Rect frame) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    PanelStyle mStyle{};
};

} // namespace henia::ui
