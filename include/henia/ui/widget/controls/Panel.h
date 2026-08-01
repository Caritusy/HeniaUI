#pragma once

#include "henia/ui/widget/Widget.h"

namespace henia::ui {

enum class LayoutDirection : std::uint8_t {
    Row,
    Column,
};

struct PanelStyle final {
    Color background{0.0F, 0.0F, 0.0F, 0.0F};
    Color border{0.0F, 0.0F, 0.0F, 0.0F};
    float borderWidth = 0.0F;
    float radius = 0.0F;
    Insets padding{};
    float gap = 0.0F;
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
