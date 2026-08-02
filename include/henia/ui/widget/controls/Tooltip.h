#pragma once

#include "henia/ui/widget/Widget.h"

#include <string>
#include <string_view>

namespace henia::ui {

struct TooltipStyle final {
    FontHandle font{};
    float fontSize = 13.0F;
    Color text{0.94F, 0.98F, 1.0F, 1.0F};
    Color background{0.018F, 0.027F, 0.041F, 0.96F};
    Color border{0.16F, 0.25F, 0.33F, 1.0F};
    Insets padding{9.0F, 6.0F, 9.0F, 6.0F};
    float radius = 6.0F;
};

// Passive tooltip bubble intended for placement in a PopupLayer. Visibility is
// explicit, so hosts can apply their own hover delay without hidden timers.
class Tooltip final : public Widget {
public:
    explicit Tooltip(std::string text = {}, TooltipStyle style = {});

    void setText(std::string text);
    [[nodiscard]] std::string_view text() const noexcept;
    void setStyle(TooltipStyle style) noexcept;

protected:
    [[nodiscard]] Vec2 onMeasure(TextPainter& text, Constraints constraints) override;
    void onPaint(Canvas& canvas, TextPainter& text, const Theme& theme) override;

private:
    std::string mText;
    TooltipStyle mStyle{};
};

} // namespace henia::ui
